/*
 * DB25 Parser - DDL Statement Implementations
 * This file contains proper implementations for CREATE, ALTER, DROP statements
 * with full column, constraint, and option parsing.
 */

#include "db25/parser/parser.hpp"
#include "db25/parser/tokenizer_adapter.hpp"

#include <algorithm>  // std::min (clamp DECIMAL precision/scale to a byte)
#include <charconv>

namespace db25::parser {

namespace {
// Non-throwing string_view -> unsigned parse. Exceptions are disabled
// (-fno-exceptions), so std::stoi/std::stol would std::terminate() the whole
// process on an out-of-range literal; std::from_chars just reports an error.
[[nodiscard]] uint32_t parse_uint_or(std::string_view text, uint32_t fallback) noexcept {
    uint32_t value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return fallback;
    }
    return value;
}

[[nodiscard]] std::string_view trim_ws(std::string_view s) noexcept {
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!s.empty() && ws(s.front())) s.remove_prefix(1);
    while (!s.empty() && ws(s.back())) s.remove_suffix(1);
    return s;
}

// Return the trimmed source text of an expression that started at `expr_begin`
// and has just been fully consumed. The end boundary is the END of the last
// consumed token, obtained from the tokenizer's position - NOT the current
// (lookahead) token's data().
//
// Using the current token as the end is unsafe: when an expression runs to the
// end of input the current token is the synthetic EOF token, whose `value` is a
// "" string literal living in read-only data, not a view into the SQL buffer.
// Subtracting pointers across those two distinct objects is undefined behaviour
// and in practice yields a multi-terabyte length, which then aborts the arena
// allocator. The previous token is always a real token viewing into the same
// SQL buffer as `expr_begin`, so its end pointer is well-defined.
[[nodiscard]] std::string_view expr_source_span(
        const tokenizer::Tokenizer* tok, const char* expr_begin) noexcept {
    if (expr_begin == nullptr || tok == nullptr) return {};
    const std::size_t pos = tok->position();
    if (pos == 0) return {};                       // nothing consumed
    const auto& tokens = tok->get_tokens();
    if (pos > tokens.size()) return {};
    const auto& last = tokens[pos - 1];            // last consumed token
    const char* expr_end = last.value.data() + last.value.size();
    if (expr_end <= expr_begin) return {};         // consumed nothing past begin
    return trim_ws(std::string_view(
        expr_begin, static_cast<std::size_t>(expr_end - expr_begin)));
}
} // namespace

// ========== DDL Entry Points ==========

ast::ASTNode* Parser::parse_create_stmt() {
    // Dispatch to specific CREATE statement based on object type
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    // Save state in case we need to reset
    const auto* start_token = current_token_;
    size_t start_pos = tokenizer_ ? tokenizer_->position() : 0;
    
    // Consume CREATE keyword
    advance();
    
    if (!current_token_ || current_token_->type != tokenizer::TokenType::Keyword) {
        return nullptr;
    }
    
    std::string_view create_type = current_token_->value;
    
    // Dispatch based on CREATE type
    if (create_type == "TABLE" || create_type == "table" ||
        create_type == "TEMPORARY" || create_type == "temporary" ||
        create_type == "TEMP" || create_type == "temp") {
        // Handle CREATE [TEMP|TEMPORARY] TABLE
        bool is_temp = false;
        if (create_type == "TEMPORARY" || create_type == "temporary" ||
            create_type == "TEMP" || create_type == "temp") {
            is_temp = true;
            advance();  // Skip TEMP/TEMPORARY
            if (!current_token_ || (current_token_->value != "TABLE" && current_token_->value != "table")) {
                return nullptr;
            }
        }
        // Now we're at TABLE - use implementation to parse columns
        auto* result = parse_create_table_impl();
        if (result && is_temp) {
            result->semantic_flags |= 0x08;  // TEMPORARY flag
        }
        return result;
    } else if (create_type == "INDEX" || create_type == "index" ||
               create_type == "UNIQUE" || create_type == "unique") {
        // Handle CREATE [UNIQUE] INDEX - use three-tier architecture
        return parse_create_index_impl();
    } else if (create_type == "VIEW" || create_type == "view" ||
               create_type == "OR" || create_type == "or") {
        // Handle CREATE [OR REPLACE] VIEW
        bool is_or_replace = false;
        if (create_type == "OR" || create_type == "or") {
            advance();  // Skip OR
            if (!current_token_ || (current_token_->value != "REPLACE" && current_token_->value != "replace")) {
                return nullptr;
            }
            advance();  // Skip REPLACE
            if (!current_token_ || (current_token_->value != "VIEW" && current_token_->value != "view")) {
                return nullptr;
            }
            is_or_replace = true;
        }
        // Use three-tier architecture
        auto* result = parse_create_view_impl();
        if (result && is_or_replace) {
            result->semantic_flags |= 0x04;  // OR REPLACE flag
        }
        return result;
    } else if (create_type == "TRIGGER" || create_type == "trigger") {
        // Handle CREATE TRIGGER
        // Reset position to CREATE for trigger parser
        current_token_ = start_token;
        if (tokenizer_) tokenizer_->set_position(start_pos);
        return parse_create_trigger();
    } else if (create_type == "SCHEMA" || create_type == "schema") {
        // Handle CREATE SCHEMA
        // Reset position to CREATE for schema parser
        current_token_ = start_token;
        if (tokenizer_) tokenizer_->set_position(start_pos);
        return parse_create_schema();
    }
    
    return nullptr;
}

// ========== Three-Tier CREATE Architecture ==========

ast::ASTNode* Parser::parse_create_table_stmt() {
    // Entry point for standalone parse_create_table_stmt calls
    // This is the second tier - skips CREATE and delegates to impl
    advance();  // Skip CREATE
    return parse_create_table_impl();
}


ast::ASTNode* Parser::parse_create_index_stmt() {
    // Entry point for standalone parse_create_index_stmt calls
    advance();  // Skip CREATE
    return parse_create_index_impl();
}

ast::ASTNode* Parser::parse_create_index_impl() {
    // We're already past CREATE, now at either UNIQUE or INDEX
    bool is_unique = false;
    if (current_token_ && current_token_->keyword_id == db25::Keyword::UNIQUE) {
        is_unique = true;
        advance();
        // Now we should be at INDEX
        if (!current_token_ || (current_token_->value != "INDEX" && current_token_->value != "index")) {
            return nullptr;
        }
    } else if (!current_token_ || (current_token_->value != "INDEX" && current_token_->value != "index")) {
        // If not UNIQUE, must be INDEX
        return nullptr;
    }
    advance();  // Skip INDEX
    
    // Create CREATE INDEX statement node
    auto* create_node = arena_.allocate<ast::ASTNode>();
    new (create_node) ast::ASTNode(ast::NodeType::CreateIndexStmt);
    create_node->node_id = next_node_id_++;
    if (is_unique) {
        create_node->semantic_flags |= 0x02;  // UNIQUE flag
    }
    
    // Check for IF NOT EXISTS
    if (current_token_ && current_token_->keyword_id == db25::Keyword::IF) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::NOT) {
            advance();
            if (current_token_ && current_token_->keyword_id == db25::Keyword::EXISTS) {
                advance();
                create_node->semantic_flags |= 0x01;  // IF NOT EXISTS flag
            }
        }
    }
    
    // Get index name
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
        create_node->primary_text = copy_to_arena(current_token_->value);
        advance();
    }
    
    // Expect ON keyword
    if (current_token_ && current_token_->keyword_id == db25::Keyword::ON) {
        advance();
        
        // Get table name
        if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
            create_node->schema_name = copy_to_arena(current_token_->value);  // Use schema_name for table
            advance();
        }
    }
    
    // Parse the indexed column list, attaching each column as an Identifier
    // child of the CREATE INDEX node.
    if (current_token_ && current_token_->value == "(") {
        parse_paren_identifier_list(create_node);
    }

    return create_node;
}

ast::ASTNode* Parser::parse_create_view_stmt() {
    // Entry point for standalone parse_create_view_stmt calls
    advance();  // Skip CREATE
    return parse_create_view_impl();
}

ast::ASTNode* Parser::parse_create_view_impl() {
    // We might be at VIEW or have already handled OR REPLACE in dispatcher
    
    // Expect VIEW keyword
    if (!current_token_ || (current_token_->value != "VIEW" && current_token_->value != "view")) {
        return nullptr;
    }
    advance();  // Skip VIEW
    
    // Create CREATE VIEW statement node
    auto* create_node = arena_.allocate<ast::ASTNode>();
    new (create_node) ast::ASTNode(ast::NodeType::CreateViewStmt);
    create_node->node_id = next_node_id_++;
    
    // Get view name
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
        create_node->primary_text = copy_to_arena(current_token_->value);
        advance();
    }
    
    // Optional column list
    if (current_token_ && current_token_->value == "(") {
        // Skip column list for now
        int paren_depth = 1;
        advance();
        while (current_token_ && paren_depth > 0) {
            if (current_token_->value == "(") paren_depth++;
            else if (current_token_->value == ")") paren_depth--;
            advance();
        }
    }
    
    // Expect AS keyword
    if (current_token_ && current_token_->keyword_id == db25::Keyword::AS) {
        advance();

        // The view body is any query expression - SELECT, a set operation, WITH,
        // or a VALUES list (`CREATE VIEW v AS VALUES (1),(2)` /
        // `... AS VALUES (1) UNION SELECT 2`). Dispatch through the shared
        // query-body parser; previously only a leading SELECT was accepted, so a
        // VALUES body was silently dropped (the view had no query child).
        create_node->first_child = parse_query_body();
        if (create_node->first_child) {
            create_node->first_child->parent = create_node;
            create_node->child_count = 1;
        }
    }
    
    return create_node;
}

// ========== End Three-Tier Architecture ==========

ast::ASTNode* Parser::parse_drop_stmt() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    // Consume DROP keyword
    advance();
    
    // Create DROP statement node
    auto* drop_node = arena_.allocate<ast::ASTNode>();
    new (drop_node) ast::ASTNode(ast::NodeType::DropStmt);
    drop_node->node_id = next_node_id_++;
    
    // Get object type (TABLE, INDEX, VIEW, etc.)
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
        std::string_view obj_type = current_token_->value;
        
        if (obj_type == "TABLE" || obj_type == "table") {
            drop_node->semantic_flags |= 0x10;  // DROP TABLE
        } else if (obj_type == "INDEX" || obj_type == "index") {
            drop_node->semantic_flags |= 0x20;  // DROP INDEX
        } else if (obj_type == "VIEW" || obj_type == "view") {
            drop_node->semantic_flags |= 0x30;  // DROP VIEW
        }
        advance();
    }
    
    // Check for IF EXISTS
    if (current_token_ && current_token_->keyword_id == db25::Keyword::IF) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::EXISTS) {
            advance();
            drop_node->semantic_flags |= 0x01;  // IF EXISTS flag
        }
    }
    
    // Get object name (mandatory): `DROP TABLE` with no name previously
    // produced a DropStmt with an empty object name.
    if (!current_token_ || current_token_->type != tokenizer::TokenType::Identifier) {
        error("expected an object name in DROP");
        return nullptr;
    }
    drop_node->primary_text = copy_to_arena(current_token_->value);
    advance();

    // Handle CASCADE/RESTRICT
    if (current_token_ && current_token_->keyword_id == db25::Keyword::CASCADE) {
        drop_node->semantic_flags |= 0x04;  // CASCADE flag
        advance();
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::RESTRICT) {
        drop_node->semantic_flags |= 0x08;  // RESTRICT flag
        advance();
    }
    
    return drop_node;
}

ast::ASTNode* Parser::parse_truncate_stmt() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    // Consume TRUNCATE keyword
    advance();
    
    // Expect TABLE keyword (optional in some dialects)
    if (current_token_ && current_token_->keyword_id == db25::Keyword::TABLE) {
        advance();
    }
    
    // Create TRUNCATE statement node
    auto* truncate_node = arena_.allocate<ast::ASTNode>();
    new (truncate_node) ast::ASTNode(ast::NodeType::TruncateStmt);
    truncate_node->node_id = next_node_id_++;
    
    // Get table name (mandatory): `TRUNCATE` / `TRUNCATE TABLE` with no name
    // previously produced a TruncateStmt with an empty table name.
    if (!current_token_ || current_token_->type != tokenizer::TokenType::Identifier) {
        error("expected a table name in TRUNCATE");
        return nullptr;
    }
    truncate_node->primary_text = copy_to_arena(current_token_->value);
    advance();

    // Handle CASCADE/RESTRICT (PostgreSQL)
    if (current_token_ && current_token_->keyword_id == db25::Keyword::CASCADE) {
        truncate_node->semantic_flags |= 0x04;  // CASCADE flag
        advance();
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::RESTRICT) {
        truncate_node->semantic_flags |= 0x08;  // RESTRICT flag
        advance();
    }

    return truncate_node;
}

ast::ASTNode* Parser::parse_alter_table_stmt() {
    // Entry point for ALTER TABLE
    return parse_alter_table_full();
}

// Helper function to parse data type
ast::ASTNode* Parser::parse_data_type() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    auto* type_node = arena_.allocate<ast::ASTNode>();
    new (type_node) ast::ASTNode(ast::NodeType::DataTypeNode);
    type_node->node_id = next_node_id_++;
    
    // Parse base type - can be keyword or identifier (for custom types)
    if (current_token_) {
        auto keyword_id = current_token_->keyword_id;
        
        // Map SQL types to DataType enum
        if (keyword_id == db25::Keyword::INTEGER || current_token_->value == "INT") {
            type_node->data_type = ast::DataType::Integer;
            type_node->primary_text = copy_to_arena("INTEGER");
        } else if (keyword_id == db25::Keyword::BIGINT) {
            type_node->data_type = ast::DataType::BigInt;
            type_node->primary_text = copy_to_arena("BIGINT");
        } else if (keyword_id == db25::Keyword::SMALLINT) {
            type_node->data_type = ast::DataType::SmallInt;
            type_node->primary_text = copy_to_arena("SMALLINT");
        } else if (current_token_->value == "VARCHAR") {
            type_node->data_type = ast::DataType::VarChar;
            type_node->primary_text = copy_to_arena("VARCHAR");
        } else if (current_token_->value == "CHAR") {
            type_node->data_type = ast::DataType::Char;
            type_node->primary_text = copy_to_arena("CHAR");
        } else if (keyword_id == db25::Keyword::TEXT || current_token_->value == "TEXT") {
            type_node->data_type = ast::DataType::Text;
            type_node->primary_text = copy_to_arena("TEXT");
        } else if (current_token_->value == "REAL") {
            type_node->data_type = ast::DataType::Real;
            type_node->primary_text = copy_to_arena("REAL");
        } else if (keyword_id == db25::Keyword::DOUBLE) {
            type_node->data_type = ast::DataType::Double;
            type_node->primary_text = copy_to_arena("DOUBLE");
        } else if (current_token_->value == "BOOLEAN" || current_token_->value == "BOOL") {
            type_node->data_type = ast::DataType::Boolean;
            type_node->primary_text = copy_to_arena("BOOLEAN");
        } else if (keyword_id == db25::Keyword::DATE) {
            type_node->data_type = ast::DataType::Date;
            type_node->primary_text = copy_to_arena("DATE");
        } else if (keyword_id == db25::Keyword::TIME) {
            type_node->data_type = ast::DataType::Time;
            type_node->primary_text = copy_to_arena("TIME");
        } else if (keyword_id == db25::Keyword::TIMESTAMP) {
            type_node->data_type = ast::DataType::Timestamp;
            type_node->primary_text = copy_to_arena("TIMESTAMP");
        } else if (current_token_->value == "DECIMAL" || current_token_->value == "NUMERIC") {
            type_node->data_type = ast::DataType::Decimal;
            type_node->primary_text = copy_to_arena("DECIMAL");
        } else if (keyword_id == db25::Keyword::INTERVAL) {
            type_node->data_type = ast::DataType::Interval;
            type_node->primary_text = copy_to_arena("INTERVAL");
        } else if (current_token_->value == "JSON" || current_token_->value == "JSONB") {
            type_node->data_type = ast::DataType::Any; // Using Any for JSON for now
            type_node->primary_text = copy_to_arena(current_token_->value);
        } else {
            // Unknown type - store as text
            type_node->primary_text = copy_to_arena(current_token_->value);
        }
        advance();
        
        // Parse precision/scale for numeric types
        if (current_token_ && current_token_->value == "(") {
            advance(); // consume (
            parenthesis_depth_++;
            
            // Store precision and scale packed into the 16-bit semantic_flags:
            // precision in the low byte, scale in the high byte. `semantic_flags`
            // is uint16_t, so the previous `scale << 16` shifted entirely out of
            // the field and was truncated to 0 -- DECIMAL(10,2) silently recorded
            // scale=0. A byte each covers all real DECIMAL/NUMERIC types (ANSI max
            // precision 38, and scale <= precision); both are clamped to 255 so an
            // out-of-range literal cannot spill into the other field.
            if (current_token_ && current_token_->type == tokenizer::TokenType::Number) {
                const uint32_t precision = parse_uint_or(current_token_->value, 0);
                type_node->semantic_flags = static_cast<uint16_t>(std::min<uint32_t>(precision, 0xFF));
                advance();

                // Parse scale if present
                if (current_token_ && current_token_->value == ",") {
                    advance(); // consume comma
                    if (current_token_ && current_token_->type == tokenizer::TokenType::Number) {
                        const uint32_t scale = parse_uint_or(current_token_->value, 0);
                        type_node->semantic_flags |= static_cast<uint16_t>(
                            std::min<uint32_t>(scale, 0xFF) << 8);  // high byte
                        advance();
                    }
                }
            }
            
            if (current_token_ && current_token_->value == ")") {
                advance(); // consume )
                parenthesis_depth_--;
            }
        }
        
        // Handle array types, including MULTI-DIMENSIONAL `type[][]`: consume
        // every `[<size>?]` suffix. Only a single `[]` was handled before, so a
        // column type like `INTEGER[][]` left the second `[]` unconsumed for the
        // caller to trip over.
        bool is_array = false;
        while (current_token_ && current_token_->value == "[") {
            advance(); // consume [

            // Optional array size
            if (current_token_ && current_token_->type == tokenizer::TokenType::Number) {
                // Store array size somehow (could use hash_cache field)
                type_node->hash_cache = parse_uint_or(current_token_->value, 0);
                advance();
            }

            if (current_token_ && current_token_->value == "]") {
                advance(); // consume ]
                is_array = true;
                // Record array-ness in the type text so it is visible in the AST
                type_node->primary_text =
                    copy_to_arena(std::string(type_node->primary_text) + "[]");
            } else {
                break;  // malformed suffix; leave for the caller to reject
            }
        }
        if (is_array) {
            type_node->flags = static_cast<ast::NodeFlags>(
                static_cast<uint8_t>(type_node->flags) | 0x80  // Custom flag for array type
            );
        }
    }
    
    return type_node;
}

// Parse column constraint (NOT NULL, PRIMARY KEY, etc.)
ast::ASTNode* Parser::parse_column_constraint() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    ast::ASTNode* constraint = nullptr;
    
    if (current_token_ && current_token_->keyword_id == db25::Keyword::NOT) {
        advance();
        // NOT must be followed by NULL. `NOT` alone previously consumed the
        // keyword and returned null, so the caller's constraint loop broke and
        // `a INT NOT` parsed cleanly (silent accept of a truncated constraint).
        if (!current_token_ || current_token_->keyword_id != db25::Keyword::KW_NULL) {
            error("expected NULL after NOT in a column constraint");
            return nullptr;
        }
        advance();
        constraint = arena_.allocate<ast::ASTNode>();
        new (constraint) ast::ASTNode(ast::NodeType::ColumnConstraint);
        constraint->node_id = next_node_id_++;
        constraint->primary_text = copy_to_arena("NOT_NULL");
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::PRIMARY) {
        advance();
        // A column-level PRIMARY constraint must be PRIMARY KEY. `PRIMARY` alone
        // previously consumed the keyword and returned null, so the caller's
        // loop broke and `a INT PRIMARY` parsed cleanly (silent accept).
        if (!current_token_ || current_token_->keyword_id != db25::Keyword::KEY) {
            error("expected KEY after PRIMARY in a column constraint");
            return nullptr;
        }
        advance();
        constraint = arena_.allocate<ast::ASTNode>();
        new (constraint) ast::ASTNode(ast::NodeType::PrimaryKeyConstraint);
        constraint->node_id = next_node_id_++;
        constraint->primary_text = copy_to_arena("PRIMARY_KEY");
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::UNIQUE) {
        advance();
        constraint = arena_.allocate<ast::ASTNode>();
        new (constraint) ast::ASTNode(ast::NodeType::UniqueConstraint);
        constraint->node_id = next_node_id_++;
        constraint->primary_text = copy_to_arena("UNIQUE");
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::CHECK) {
        advance();
        constraint = arena_.allocate<ast::ASTNode>();
        new (constraint) ast::ASTNode(ast::NodeType::CheckConstraint);
        constraint->node_id = next_node_id_++;
        
        // CHECK requires a parenthesized condition; `CHECK` alone previously
        // produced a childless CheckConstraint and a clean parse.
        if (!current_token_ || current_token_->value != "(") {
            error("expected '(' with a condition after CHECK");
            return nullptr;
        }
        advance(); // consume (
        parenthesis_depth_++;

        // Capture the exact source text of the check expression: both tokens
        // view into the same source buffer, so the span runs from the first
        // expression token to the closing paren. Stored on primary_text for
        // faithful persistence downstream (no lossy AST reconstruction).
        const char* expr_begin = current_token_ ? current_token_->value.data() : nullptr;
        auto* expr = parse_expression(0);
        if (!expr) {
            error("expected a condition in the CHECK constraint");
            return nullptr;
        }
        expr->parent = constraint;
        constraint->first_child = expr;
        constraint->child_count = 1;
        const std::string_view text = expr_source_span(tokenizer_, expr_begin);
        if (!text.empty()) {
            constraint->primary_text = copy_to_arena(text);
        }

        if (current_token_ && current_token_->value == ")") {
            advance(); // consume )
            parenthesis_depth_--;
        }
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::KW_DEFAULT) {
        advance();
        constraint = arena_.allocate<ast::ASTNode>();
        new (constraint) ast::ASTNode(ast::NodeType::DefaultClause);
        constraint->node_id = next_node_id_++;

        // Capture the default expression's source text (see CHECK above). The
        // span runs from the first token to whatever follows the default (a
        // comma, another constraint keyword, or the closing paren). DEFAULT
        // requires a value expression; `DEFAULT` alone previously produced a
        // childless DefaultClause.
        const char* expr_begin = current_token_ ? current_token_->value.data() : nullptr;
        auto* expr = parse_primary_expression();
        if (!expr) {
            error("expected a value expression after DEFAULT");
            return nullptr;
        }
        expr->parent = constraint;
        constraint->first_child = expr;
        constraint->child_count = 1;
        const std::string_view text = expr_source_span(tokenizer_, expr_begin);
        if (!text.empty()) {
            constraint->primary_text = copy_to_arena(text);
        }
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::REFERENCES) {
        // Foreign key constraint
        advance();
        constraint = arena_.allocate<ast::ASTNode>();
        new (constraint) ast::ASTNode(ast::NodeType::ForeignKeyConstraint);
        constraint->node_id = next_node_id_++;

        // REFERENCES requires a referenced table name; `REFERENCES` alone
        // previously produced a ForeignKeyConstraint with no target.
        if (!current_token_ || current_token_->type != tokenizer::TokenType::Identifier) {
            error("expected a referenced table name after REFERENCES");
            return nullptr;
        }
        {
            constraint->primary_text = copy_to_arena(current_token_->value);
            advance();
            
            // Parse referenced columns
            if (current_token_ && current_token_->value == "(") {
                advance(); // consume (
                parenthesis_depth_++;
                
                // Parse column list
                ast::ASTNode* first_col = nullptr;
                ast::ASTNode* last_col = nullptr;
                
                while (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
                    auto* col = arena_.allocate<ast::ASTNode>();
                    new (col) ast::ASTNode(ast::NodeType::Identifier);
                    col->node_id = next_node_id_++;
                    col->primary_text = copy_to_arena(current_token_->value);
                    col->parent = constraint;
                    
                    if (!first_col) {
                        first_col = col;
                        constraint->first_child = first_col;
                    } else if (last_col) {
                        last_col->next_sibling = col;
                    }
                    last_col = col;
                    constraint->child_count++;
                    
                    advance();
                    
                    if (current_token_ && current_token_->value == ",") {
                        advance();
                    } else {
                        break;
                    }
                }
                
                if (current_token_ && current_token_->value == ")") {
                    advance(); // consume )
                    parenthesis_depth_--;
                }
            }
        }
    }
    
    return constraint;
}

// Parse column definition
int Parser::parse_paren_identifier_list(ast::ASTNode* parent) {
    int count = 0;
    if (!current_token_ || current_token_->value != "(") return 0;
    advance();  // consume '('
    parenthesis_depth_++;
    ast::ASTNode* last = parent->first_child;
    while (last != nullptr && last->next_sibling != nullptr) last = last->next_sibling;
    // A column name may be an identifier or a non-reserved keyword used as a
    // name (e.g. FIRST, LAST), matching how the rest of the parser accepts
    // keywords in name position.
    while (current_token_ &&
           (current_token_->type == tokenizer::TokenType::Identifier ||
            current_token_->type == tokenizer::TokenType::Keyword)) {
        auto* col = arena_.allocate<ast::ASTNode>();
        new (col) ast::ASTNode(ast::NodeType::Identifier);
        col->node_id = next_node_id_++;
        col->primary_text = copy_to_arena(current_token_->value);
        col->parent = parent;
        if (parent->first_child == nullptr) {
            parent->first_child = col;
        } else if (last != nullptr) {
            last->next_sibling = col;
        }
        last = col;
        parent->child_count++;
        ++count;
        advance();
        if (current_token_ && current_token_->value == ",") {
            advance();
        } else {
            break;
        }
    }
    // Consume through the closing ')', tolerating anything unexpected so a
    // malformed list cannot desynchronise the enclosing parser.
    while (current_token_ && current_token_->value != ")") advance();
    if (current_token_ && current_token_->value == ")") {
        advance();  // consume ')'
        if (parenthesis_depth_ > 0) parenthesis_depth_--;
    }
    return count;
}

ast::ASTNode* Parser::parse_column_definition() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    auto* column = arena_.allocate<ast::ASTNode>();
    new (column) ast::ASTNode(ast::NodeType::ColumnDefinition);
    column->node_id = next_node_id_++;
    
    // Get column name. Accept a bare keyword as a name too (`data`, `key`,
    // `value`, ... are keywords but common column names) - the same
    // keyword-as-identifier leniency the parser applies to table names. Table
    // constraints (PRIMARY / FOREIGN / UNIQUE / CHECK / CONSTRAINT) are
    // dispatched to parse_table_constraint by the caller before reaching here.
    if (current_token_ && (current_token_->type == tokenizer::TokenType::Identifier ||
                           current_token_->type == tokenizer::TokenType::Keyword)) {
        column->primary_text = copy_to_arena(current_token_->value);
        advance();
    } else {
        // Arena allocated - don't delete
        return nullptr;
    }
    
    // Parse data type
    auto* data_type = parse_data_type();
    if (data_type) {
        data_type->parent = column;
        column->first_child = data_type;
        column->child_count = 1;
    }
    
    // Parse column constraints
    ast::ASTNode* last_child = data_type;
    
    while (current_token_) {
        ast::ASTNode* constraint = parse_column_constraint();
        if (!constraint) break;
        
        constraint->parent = column;
        if (last_child) {
            last_child->next_sibling = constraint;
        } else {
            column->first_child = constraint;
        }
        last_child = constraint;
        column->child_count++;
    }
    
    return column;
}

// Parse table constraint (multi-column constraints)
ast::ASTNode* Parser::parse_table_constraint() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    ast::ASTNode* constraint = nullptr;

    // Optional CONSTRAINT <name>: capture the name so it can be attached to the
    // constraint node below (needed for ALTER TABLE DROP CONSTRAINT <name>).
    std::string_view constraint_name;
    if (current_token_ && current_token_->keyword_id == db25::Keyword::CONSTRAINT) {
        advance();
        if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
            constraint_name = current_token_->value;
            advance();
        }
    }

    if (current_token_ && current_token_->keyword_id == db25::Keyword::PRIMARY) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::KEY) {
            advance();
            constraint = arena_.allocate<ast::ASTNode>();
            new (constraint) ast::ASTNode(ast::NodeType::PrimaryKeyConstraint);
            constraint->node_id = next_node_id_++;
            
            // Parse column list
            if (current_token_ && current_token_->value == "(") {
                advance(); // consume (
                parenthesis_depth_++;
                
                ast::ASTNode* first_col = nullptr;
                ast::ASTNode* last_col = nullptr;
                
                while (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
                    auto* col = arena_.allocate<ast::ASTNode>();
                    new (col) ast::ASTNode(ast::NodeType::Identifier);
                    col->node_id = next_node_id_++;
                    col->primary_text = copy_to_arena(current_token_->value);
                    col->parent = constraint;
                    
                    if (!first_col) {
                        first_col = col;
                        constraint->first_child = first_col;
                    } else if (last_col) {
                        last_col->next_sibling = col;
                    }
                    last_col = col;
                    constraint->child_count++;
                    
                    advance();
                    
                    if (current_token_ && current_token_->value == ",") {
                        advance();
                    } else {
                        break;
                    }
                }
                
                if (current_token_ && current_token_->value == ")") {
                    advance(); // consume )
                    parenthesis_depth_--;
                }
            }
        }
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::FOREIGN) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::KEY) {
            advance();
            constraint = arena_.allocate<ast::ASTNode>();
            new (constraint) ast::ASTNode(ast::NodeType::ForeignKeyConstraint);
            constraint->node_id = next_node_id_++;

            // Local columns: FOREIGN KEY (a, b, ...) - attached as Identifier
            // children of the constraint.
            if (current_token_ && current_token_->value == "(") {
                parse_paren_identifier_list(constraint);
            }

            // REFERENCES <table> (<cols>): the referenced table + columns live
            // under a ReferencesClause child so they are unambiguously distinct
            // from the local columns above.
            if (current_token_ && current_token_->keyword_id == db25::Keyword::REFERENCES) {
                advance();
                auto* ref = arena_.allocate<ast::ASTNode>();
                new (ref) ast::ASTNode(ast::NodeType::ReferencesClause);
                ref->node_id = next_node_id_++;
                if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
                    ref->primary_text = copy_to_arena(current_token_->value);  // ref table
                    advance();
                }
                if (current_token_ && current_token_->value == "(") {
                    parse_paren_identifier_list(ref);  // referenced columns
                }
                ref->parent = constraint;
                // Append the ReferencesClause after any local-column children.
                if (constraint->first_child == nullptr) {
                    constraint->first_child = ref;
                } else {
                    ast::ASTNode* last = constraint->first_child;
                    while (last->next_sibling != nullptr) last = last->next_sibling;
                    last->next_sibling = ref;
                }
                constraint->child_count++;
            }
        }
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::UNIQUE) {
        advance();
        constraint = arena_.allocate<ast::ASTNode>();
        new (constraint) ast::ASTNode(ast::NodeType::UniqueConstraint);
        constraint->node_id = next_node_id_++;

        // UNIQUE (a, b, ...): columns attached as Identifier children.
        if (current_token_ && current_token_->value == "(") {
            parse_paren_identifier_list(constraint);
        }
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::CHECK) {
        advance();
        constraint = arena_.allocate<ast::ASTNode>();
        new (constraint) ast::ASTNode(ast::NodeType::CheckConstraint);
        constraint->node_id = next_node_id_++;
        
        if (current_token_ && current_token_->value == "(") {
            advance(); // consume (
            parenthesis_depth_++;

            const char* expr_begin = current_token_ ? current_token_->value.data() : nullptr;
            auto* expr = parse_expression(0);
            if (expr) {
                expr->parent = constraint;
                constraint->first_child = expr;
                constraint->child_count = 1;
            }
            const std::string_view text = expr_source_span(tokenizer_, expr_begin);
            if (!text.empty()) {
                constraint->primary_text = copy_to_arena(text);
            }

            if (current_token_ && current_token_->value == ")") {
                advance(); // consume )
                parenthesis_depth_--;
            }
        }
    }

    // Attach the optional constraint name (stored on schema_name so it does not
    // collide with primary_text, which a CHECK uses for its expression text).
    if (constraint != nullptr && !constraint_name.empty()) {
        constraint->schema_name = copy_to_arena(constraint_name);
    }

    return constraint;
}

// Improved CREATE TABLE implementation
ast::ASTNode* Parser::parse_create_table_impl() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    // We're at TABLE keyword
    advance();  // Skip TABLE
    
    // Create CREATE TABLE statement node
    auto* create_node = arena_.allocate<ast::ASTNode>();
    new (create_node) ast::ASTNode(ast::NodeType::CreateTableStmt);
    create_node->node_id = next_node_id_++;
    
    // Check for IF NOT EXISTS
    if (current_token_ && current_token_->keyword_id == db25::Keyword::IF) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::NOT) {
            advance();
            if (current_token_ && current_token_->keyword_id == db25::Keyword::EXISTS) {
                advance();
                create_node->semantic_flags |= 0x01;  // IF NOT EXISTS flag
            }
        }
    }
    
    // Get table name (potentially schema-qualified)
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
        std::string_view first_name = current_token_->value;
        advance();
        
        if (current_token_ && current_token_->value == ".") {
            advance(); // consume dot
            if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
                create_node->schema_name = copy_to_arena(first_name);
                create_node->primary_text = copy_to_arena(current_token_->value);
                advance();
            }
        } else {
            create_node->primary_text = copy_to_arena(first_name);
        }
    }
    
    // Parse column definitions and constraints
    bool had_column_parens = false;
    if (current_token_ && current_token_->value == "(") {
        had_column_parens = true;
        advance(); // consume (
        parenthesis_depth_++;

        ast::ASTNode* first_child = nullptr;
        ast::ASTNode* last_child = nullptr;

        while (current_token_ && current_token_->value != ")") {
            ast::ASTNode* element = nullptr;

            // Check if it's a table constraint or column definition
            if (current_token_ && (
                current_token_->keyword_id == db25::Keyword::PRIMARY ||
                current_token_->keyword_id == db25::Keyword::FOREIGN ||
                current_token_->keyword_id == db25::Keyword::UNIQUE ||
                current_token_->keyword_id == db25::Keyword::CHECK ||
                current_token_->keyword_id == db25::Keyword::CONSTRAINT)) {
                // Table constraint
                element = parse_table_constraint();
            } else {
                // Column definition
                element = parse_column_definition();
            }

            // A column/constraint element that fails to parse is a syntax error;
            // returning here (rather than looping) also guarantees progress.
            if (!element) {
                error("expected a column definition or table constraint in CREATE TABLE");
                return nullptr;
            }
            element->parent = create_node;
            if (!first_child) {
                first_child = element;
                create_node->first_child = first_child;
            } else if (last_child) {
                last_child->next_sibling = element;
            }
            last_child = element;
            create_node->child_count++;

            // After an element only a comma (more elements) or ')' (end) is
            // valid. An unexpected token here was previously SKIPPED by a
            // recovery loop that silently swallowed invalid tokens -- and whole
            // subsequent columns -- with a clean parse (`CREATE TABLE t (a INT
            // %%% b INT)` kept only `a`). Reject instead.
            if (current_token_ && current_token_->value == ",") {
                advance(); // consume comma
            } else if (current_token_ && current_token_->value != ")") {
                error("unexpected token in the CREATE TABLE column list");
                return nullptr;
            }
        }

        if (current_token_ && current_token_->value == ")") {
            advance(); // consume )
            parenthesis_depth_--;
        }
    }

    // An empty column list `CREATE TABLE t ()` (with no CTAS body to follow)
    // defines a table with zero columns, which is invalid SQL. Reject it unless
    // an AS <query> body follows (handled below), which supplies the columns.
    if (had_column_parens && create_node->child_count == 0 &&
        !(current_token_ && current_token_->keyword_id == db25::Keyword::AS)) {
        error("expected at least one column or constraint in CREATE TABLE");
        return nullptr;
    }

    // CREATE TABLE ... AS <query> (CTAS): the table is defined by the result of a
    // query. Parse the query body and attach it as a child, exactly like
    // CREATE VIEW ... AS. Without this the table-options loop below swallowed and
    // discarded the whole SELECT, producing a childless CreateTableStmt with a
    // (misleading) parse success - silent loss of the query body.
    if (current_token_ && current_token_->keyword_id == db25::Keyword::AS) {
        advance();  // consume AS
        // The body is any query expression (SELECT, set operation, WITH, or
        // VALUES); dispatch through the shared query-body parser.
        if (ast::ASTNode* body = parse_query_body()) {
            body->parent = create_node;
            // A column-name list may precede AS in standard CTAS; append the body
            // after any such children rather than overwriting them.
            if (create_node->first_child == nullptr) {
                create_node->first_child = body;
            } else {
                ast::ASTNode* last = create_node->first_child;
                while (last->next_sibling != nullptr) {
                    last = last->next_sibling;
                }
                last->next_sibling = body;
            }
            create_node->child_count++;
        }
    }

    // Table options (WITHOUT ROWID, engine options, etc.) are not modeled yet.
    // Previously a loop consumed EVERY remaining token to ';'/EOF, which
    // silently swallowed arbitrary trailing junk -- even an entire following
    // statement (`CREATE TABLE t (a INT) SELECT * FROM x`) -- and defeated the
    // parser's trailing_token_count() diagnostic. Leave any unrecognized
    // trailing tokens in place so they surface as trailing_token_count > 0,
    // consistent with ALTER TABLE / DROP / CREATE INDEX.

    return create_node;
}

// Improved ALTER TABLE implementation
ast::ASTNode* Parser::parse_alter_table_full() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    // We're at ALTER keyword
    advance();  // Skip ALTER
    
    // Expect TABLE keyword
    if (!current_token_ || current_token_->keyword_id != db25::Keyword::TABLE) {
        return nullptr;
    }
    advance();  // Skip TABLE
    
    // Create ALTER TABLE statement node
    auto* alter_node = arena_.allocate<ast::ASTNode>();
    new (alter_node) ast::ASTNode(ast::NodeType::AlterTableStmt);
    alter_node->node_id = next_node_id_++;
    
    // Get table name
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
        alter_node->primary_text = copy_to_arena(current_token_->value);
        advance();
    }
    
    // Parse ALTER action
    auto* action = arena_.allocate<ast::ASTNode>();
    new (action) ast::ASTNode(ast::NodeType::AlterTableAction);
    action->node_id = next_node_id_++;
    action->parent = alter_node;
    alter_node->first_child = action;
    alter_node->child_count = 1;
    
    if (current_token_ && current_token_->keyword_id == db25::Keyword::ADD) {
        advance();
        action->primary_text = copy_to_arena("ADD");

        // ADD [CONSTRAINT ...] PRIMARY KEY / UNIQUE / CHECK / FOREIGN KEY adds a
        // table-level constraint; anything else is ADD [COLUMN] <definition>. The
        // leading keyword disambiguates (a column name is never one of these).
        const bool is_table_constraint =
            current_token_ &&
            (current_token_->keyword_id == db25::Keyword::CONSTRAINT ||
             current_token_->keyword_id == db25::Keyword::PRIMARY ||
             current_token_->keyword_id == db25::Keyword::UNIQUE ||
             current_token_->keyword_id == db25::Keyword::CHECK ||
             current_token_->keyword_id == db25::Keyword::FOREIGN);

        if (is_table_constraint) {
            auto* constraint = parse_table_constraint();
            if (constraint) {
                constraint->parent = action;
                action->first_child = constraint;
                action->child_count = 1;
            }
        } else {
            if (current_token_ && current_token_->keyword_id == db25::Keyword::COLUMN) {
                advance(); // optional COLUMN keyword
            }
            auto* column = parse_column_definition();
            if (column) {
                column->parent = action;
                action->first_child = column;
                action->child_count = 1;
            }
        }
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::DROP) {
        advance();
        action->primary_text = copy_to_arena("DROP");

        // DROP CONSTRAINT <name> [CASCADE|RESTRICT]: flagged distinctly from
        // DROP COLUMN. The name is attached as an Identifier child.
        if (current_token_ && current_token_->keyword_id == db25::Keyword::CONSTRAINT) {
            advance(); // consume CONSTRAINT
            action->semantic_flags |= 0x20;  // DROP CONSTRAINT flag
            if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
                auto* nm = arena_.allocate<ast::ASTNode>();
                new (nm) ast::ASTNode(ast::NodeType::Identifier);
                nm->node_id = next_node_id_++;
                nm->primary_text = copy_to_arena(current_token_->value);
                nm->parent = action;
                action->first_child = nm;
                action->child_count = 1;
                advance();
            }
            if (current_token_ && current_token_->keyword_id == db25::Keyword::CASCADE) {
                action->semantic_flags |= 0x01;  // CASCADE
                advance();
            } else if (current_token_ &&
                       current_token_->keyword_id == db25::Keyword::RESTRICT) {
                action->semantic_flags |= 0x02;  // RESTRICT
                advance();
            }
            return alter_node;
        }

        if (current_token_ && current_token_->keyword_id == db25::Keyword::COLUMN) {
            advance(); // optional COLUMN keyword
        }

        // Get column name
        if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
            auto* col = arena_.allocate<ast::ASTNode>();
            new (col) ast::ASTNode(ast::NodeType::Identifier);
            col->node_id = next_node_id_++;
            col->primary_text = copy_to_arena(current_token_->value);
            col->parent = action;
            action->first_child = col;
            action->child_count = 1;
            advance();
            
            // Handle CASCADE/RESTRICT
            if (current_token_ && current_token_->keyword_id == db25::Keyword::CASCADE) {
                action->semantic_flags |= 0x01;  // CASCADE flag
                advance();
            } else if (current_token_ && current_token_->keyword_id == db25::Keyword::RESTRICT) {
                action->semantic_flags |= 0x02;  // RESTRICT flag
                advance();
            }
        }
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::ALTER) {
        advance();
        action->primary_text = copy_to_arena("ALTER");
        
        if (current_token_ && current_token_->keyword_id == db25::Keyword::COLUMN) {
            advance(); // optional COLUMN keyword
        }
        
        // Get column name
        if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
            auto* col = arena_.allocate<ast::ASTNode>();
            new (col) ast::ASTNode(ast::NodeType::Identifier);
            col->node_id = next_node_id_++;
            col->primary_text = copy_to_arena(current_token_->value);
            col->parent = action;
            action->first_child = col;
            action->child_count = 1;
            advance();
            
            // Parse alteration (SET DEFAULT, DROP DEFAULT, TYPE, etc.)
            if (current_token_ && current_token_->keyword_id == db25::Keyword::SET) {
                advance();
                if (current_token_ && current_token_->keyword_id == db25::Keyword::KW_DEFAULT) {
                    advance();
                    // Wrap the new default in a DefaultClause carrying the verbatim
                    // expression source text, matching a column-definition DEFAULT
                    // so downstream consumers read the default the same way in both
                    // places (see parse_column_constraint).
                    auto* def = arena_.allocate<ast::ASTNode>();
                    new (def) ast::ASTNode(ast::NodeType::DefaultClause);
                    def->node_id = next_node_id_++;
                    def->parent = action;
                    const char* expr_begin =
                        current_token_ ? current_token_->value.data() : nullptr;
                    auto* expr = parse_expression(0);
                    if (expr) {
                        expr->parent = def;
                        def->first_child = expr;
                        def->child_count = 1;
                    }
                    const std::string_view text = expr_source_span(tokenizer_, expr_begin);
                    if (!text.empty()) {
                        def->primary_text = copy_to_arena(text);
                    }
                    col->next_sibling = def;
                    action->child_count++;
                } else if (current_token_ && current_token_->keyword_id == db25::Keyword::NOT) {
                    advance();  // consume NOT
                    if (current_token_ && current_token_->keyword_id == db25::Keyword::KW_NULL) {
                        advance();  // consume NULL
                        action->semantic_flags |= 0x08;  // SET NOT NULL flag
                    }
                }
            } else if (current_token_ && current_token_->keyword_id == db25::Keyword::DROP) {
                advance();
                if (current_token_ && current_token_->keyword_id == db25::Keyword::KW_DEFAULT) {
                    advance();
                    action->semantic_flags |= 0x04;  // DROP DEFAULT flag
                } else if (current_token_ && current_token_->keyword_id == db25::Keyword::NOT) {
                    advance();  // consume NOT
                    if (current_token_ && current_token_->keyword_id == db25::Keyword::KW_NULL) {
                        advance();  // consume NULL
                        action->semantic_flags |= 0x10;  // DROP NOT NULL flag
                    }
                }
            } else if (current_token_ && current_token_->keyword_id == db25::Keyword::TYPE) {
                advance();
                // Parse new data type
                auto* data_type = parse_data_type();
                if (data_type) {
                    data_type->parent = action;
                    col->next_sibling = data_type;
                    action->child_count++;
                }
            }
        }
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::RENAME) {
        advance();
        action->primary_text = copy_to_arena("RENAME");
        
        if (current_token_ && current_token_->keyword_id == db25::Keyword::TO) {
            advance();
        }
        
        // Get new name
        if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
            auto* new_name = arena_.allocate<ast::ASTNode>();
            new (new_name) ast::ASTNode(ast::NodeType::Identifier);
            new_name->node_id = next_node_id_++;
            new_name->primary_text = copy_to_arena(current_token_->value);
            new_name->parent = action;
            action->first_child = new_name;
            action->child_count = 1;
            advance();
        }
    }
    
    return alter_node;
}

// Improved CREATE INDEX implementation
ast::ASTNode* Parser::parse_create_index_full() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    // We might be at UNIQUE or INDEX
    bool is_unique = false;
    if (current_token_ && current_token_->keyword_id == db25::Keyword::UNIQUE) {
        is_unique = true;
        advance();
    }
    
    // Expect INDEX keyword
    if (!current_token_ || (current_token_->value != "INDEX" && current_token_->value != "index")) {
        return nullptr;
    }
    advance();  // Skip INDEX
    
    // Create CREATE INDEX statement node
    auto* create_node = arena_.allocate<ast::ASTNode>();
    new (create_node) ast::ASTNode(ast::NodeType::CreateIndexStmt);
    create_node->node_id = next_node_id_++;
    
    if (is_unique) {
        create_node->semantic_flags |= 0x02;  // UNIQUE flag
    }
    
    // Check for IF NOT EXISTS
    if (current_token_ && current_token_->keyword_id == db25::Keyword::IF) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::NOT) {
            advance();
            if (current_token_ && current_token_->keyword_id == db25::Keyword::EXISTS) {
                advance();
                create_node->semantic_flags |= 0x01;  // IF NOT EXISTS flag
            }
        }
    }
    
    // Get index name
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
        create_node->primary_text = copy_to_arena(current_token_->value);
        advance();
    }
    
    // Expect ON keyword
    if (current_token_ && current_token_->keyword_id == db25::Keyword::ON) {
        advance();
        
        // Get table name
        if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
            create_node->schema_name = copy_to_arena(current_token_->value); // Using schema_name for table
            advance();
        }
    }
    
    // Parse indexed columns
    if (current_token_ && current_token_->value == "(") {
        advance(); // consume (
        parenthesis_depth_++;
        
        ast::ASTNode* first_col = nullptr;
        ast::ASTNode* last_col = nullptr;
        
        while (current_token_ && current_token_->value != ")") {
            auto* index_col = arena_.allocate<ast::ASTNode>();
            new (index_col) ast::ASTNode(ast::NodeType::IndexColumn);
            index_col->node_id = next_node_id_++;
            
            // Column name or expression
            if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
                index_col->primary_text = copy_to_arena(current_token_->value);
                advance();
            } else {
                // Could be an expression
                auto* expr = parse_expression(0);
                if (expr) {
                    expr->parent = index_col;
                    index_col->first_child = expr;
                    index_col->child_count = 1;
                }
            }
            
            // Optional ASC/DESC
            if (current_token_ && current_token_->keyword_id == db25::Keyword::ASC) {
                index_col->semantic_flags |= 0x01;  // ASC flag
                advance();
            } else if (current_token_ && current_token_->keyword_id == db25::Keyword::DESC) {
                index_col->semantic_flags |= 0x02;  // DESC flag
                advance();
            }
            
            index_col->parent = create_node;
            if (!first_col) {
                first_col = index_col;
                create_node->first_child = first_col;
            } else if (last_col) {
                last_col->next_sibling = index_col;
            }
            last_col = index_col;
            create_node->child_count++;
            
            if (current_token_ && current_token_->value == ",") {
                advance(); // consume comma
            } else {
                break;
            }
        }
        
        if (current_token_ && current_token_->value == ")") {
            advance(); // consume )
            parenthesis_depth_--;
        }
    }
    
    // Parse WHERE clause for partial index
    if (current_token_ && current_token_->keyword_id == db25::Keyword::WHERE) {
        advance();
        auto* where_expr = parse_expression(0);
        if (where_expr) {
            where_expr->parent = create_node;
            // Add as last child
            if (create_node->first_child) {
                auto* last = create_node->first_child;
                while (last->next_sibling) {
                    last = last->next_sibling;
                }
                last->next_sibling = where_expr;
            } else {
                create_node->first_child = where_expr;
            }
            create_node->child_count++;
        }
    }
    
    return create_node;
}

// Parse CREATE TRIGGER statement
ast::ASTNode* Parser::parse_create_trigger() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    // We're at CREATE keyword
    advance();  // Skip CREATE
    
    // Check for TEMPORARY/TEMP
    bool is_temp = false;
    if (current_token_ && (current_token_->value == "TEMPORARY" || current_token_->value == "TEMP")) {
        is_temp = true;
        advance();
    }
    
    // Expect TRIGGER keyword
    if (!current_token_ || (current_token_->value != "TRIGGER" && current_token_->value != "trigger")) {
        return nullptr;
    }
    advance();  // Skip TRIGGER
    
    // Create CREATE TRIGGER statement node
    auto* trigger_node = arena_.allocate<ast::ASTNode>();
    new (trigger_node) ast::ASTNode(ast::NodeType::CreateTriggerStmt);
    trigger_node->node_id = next_node_id_++;
    
    if (is_temp) {
        trigger_node->semantic_flags |= 0x04;  // TEMPORARY flag
    }
    
    // Check for IF NOT EXISTS
    if (current_token_ && current_token_->keyword_id == db25::Keyword::IF) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::NOT) {
            advance();
            if (current_token_ && current_token_->keyword_id == db25::Keyword::EXISTS) {
                advance();
                trigger_node->semantic_flags |= 0x01;  // IF NOT EXISTS flag
            }
        }
    }
    
    // Get trigger name
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
        trigger_node->primary_text = copy_to_arena(current_token_->value);
        advance();
    }
    
    // Parse BEFORE/AFTER/INSTEAD OF
    if (current_token_ && current_token_->keyword_id == db25::Keyword::BEFORE) {
        trigger_node->semantic_flags |= 0x10;  // BEFORE flag
        advance();
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::AFTER) {
        trigger_node->semantic_flags |= 0x20;  // AFTER flag
        advance();
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::INSTEAD) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::OF) {
            advance();
            trigger_node->semantic_flags |= 0x30;  // INSTEAD OF flag
        }
    }
    
    // Parse trigger event (INSERT/UPDATE/DELETE)
    if (current_token_) {
        if (current_token_->keyword_id == db25::Keyword::INSERT) {
            trigger_node->semantic_flags |= 0x100;  // INSERT event
            advance();
        } else if (current_token_->keyword_id == db25::Keyword::UPDATE) {
            trigger_node->semantic_flags |= 0x200;  // UPDATE event
            advance();
            
            // Optional OF column_list
            if (current_token_ && current_token_->keyword_id == db25::Keyword::OF) {
                advance();
                // Parse column list
                // TODO: Implement column list parsing for UPDATE OF
            }
        } else if (current_token_->keyword_id == db25::Keyword::DELETE) {
            trigger_node->semantic_flags |= 0x400;  // DELETE event
            advance();
        }
    }
    
    // Parse ON table_name
    if (current_token_ && current_token_->keyword_id == db25::Keyword::ON) {
        advance();
        if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
            trigger_node->schema_name = copy_to_arena(current_token_->value); // Using schema_name for table
            advance();
        }
    }
    
    // Parse FOR EACH ROW/STATEMENT
    if (current_token_ && current_token_->keyword_id == db25::Keyword::FOR) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::EACH) {
            advance();
            if (current_token_ && current_token_->value == "ROW") {
                trigger_node->semantic_flags |= 0x1000;  // FOR EACH ROW
                advance();
            } else if (current_token_ && current_token_->value == "STATEMENT") {
                trigger_node->semantic_flags |= 0x2000;  // FOR EACH STATEMENT
                advance();
            }
        }
    }
    
    // Parse WHEN condition
    if (current_token_ && current_token_->keyword_id == db25::Keyword::WHEN) {
        advance();
        auto* when_expr = parse_expression(0);
        if (when_expr) {
            when_expr->parent = trigger_node;
            trigger_node->first_child = when_expr;
            trigger_node->child_count = 1;
        }
    }
    
    // Parse trigger body (BEGIN ... END or single statement)
    if (current_token_ && current_token_->keyword_id == db25::Keyword::BEGIN) {
        advance();
        // Parse multiple statements until END
        ast::ASTNode* first_stmt = nullptr;
        ast::ASTNode* last_stmt = nullptr;
        
        while (current_token_ && current_token_->keyword_id != db25::Keyword::END) {
            auto* stmt = parse_statement();
            if (stmt) {
                stmt->parent = trigger_node;
                if (!first_stmt) {
                    first_stmt = stmt;
                    if (!trigger_node->first_child) {
                        trigger_node->first_child = first_stmt;
                    } else {
                        // WHEN clause exists, add after it
                        trigger_node->first_child->next_sibling = first_stmt;
                    }
                } else if (last_stmt) {
                    last_stmt->next_sibling = stmt;
                }
                last_stmt = stmt;
                trigger_node->child_count++;
            }

            // Skip semicolons between statements.
            bool progressed = false;
            if (current_token_ && current_token_->value == ";") {
                advance();
                progressed = true;
            }

            // Forward-progress guard: if this iteration neither parsed a
            // statement nor consumed a separator, the current token is one
            // parse_statement() cannot start a statement from (a stray token, or
            // the recursion-depth guard tripping and returning nullptr). Without
            // this, the loop re-calls parse_statement() on the same token
            // forever -- an infinite loop e.g. on `... BEGIN @ END`, and the hang
            // that deep nested-trigger input degrades into once the depth guard
            // stops it from overflowing the stack. Stop the body here; the parser
            // is lenient about the unconsumed remainder.
            if (!stmt && !progressed) {
                break;
            }
        }
        
        if (current_token_ && current_token_->keyword_id == db25::Keyword::END) {
            advance();
        }
    } else {
        // Single statement trigger
        auto* stmt = parse_statement();
        if (stmt) {
            stmt->parent = trigger_node;
            if (trigger_node->first_child) {
                trigger_node->first_child->next_sibling = stmt;
            } else {
                trigger_node->first_child = stmt;
            }
            trigger_node->child_count++;
        }
    }
    
    return trigger_node;
}

// Parse CREATE SCHEMA statement
ast::ASTNode* Parser::parse_create_schema() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    // We're at CREATE keyword
    advance();  // Skip CREATE
    
    // Expect SCHEMA keyword
    if (!current_token_ || (current_token_->value != "SCHEMA" && current_token_->value != "schema")) {
        return nullptr;
    }
    advance();  // Skip SCHEMA
    
    // Create CREATE SCHEMA statement node
    auto* schema_node = arena_.allocate<ast::ASTNode>();
    new (schema_node) ast::ASTNode(ast::NodeType::CreateSchemaStmt);
    schema_node->node_id = next_node_id_++;
    
    // Check for IF NOT EXISTS
    if (current_token_ && current_token_->keyword_id == db25::Keyword::IF) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::NOT) {
            advance();
            if (current_token_ && current_token_->keyword_id == db25::Keyword::EXISTS) {
                advance();
                schema_node->semantic_flags |= 0x01;  // IF NOT EXISTS flag
            }
        }
    }
    
    // Get schema name
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
        schema_node->primary_text = copy_to_arena(current_token_->value);
        advance();
    }
    
    // Optional AUTHORIZATION clause
    if (current_token_ && current_token_->keyword_id == db25::Keyword::AUTHORIZATION) {
        advance();
        if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
            schema_node->schema_name = copy_to_arena(current_token_->value); // Store owner in schema_name
            advance();
        }
    }
    
    return schema_node;
}

} // namespace db25::parser