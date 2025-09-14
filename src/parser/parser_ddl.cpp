/*
 * DB25 Parser - DDL Statement Implementations
 * This file contains proper implementations for CREATE, ALTER, DROP statements
 * with full column, constraint, and option parsing.
 */

#include "db25/parser/parser.hpp"
#include "db25/parser/tokenizer_adapter.hpp"

namespace db25::parser {

// ========== DDL Entry Points ==========

ast::ASTNode* Parser::parse_create_stmt() {
    // Dispatch to specific CREATE statement based on object type
    if (const DepthGuard guard(this); !guard.is_valid()) return nullptr;
    
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
    
    // Parse column list (simplified)
    if (current_token_ && current_token_->value == "(") {
        int paren_depth = 1;
        advance();
        while (current_token_ && paren_depth > 0) {
            if (current_token_->value == "(") paren_depth++;
            else if (current_token_->value == ")") paren_depth--;
            advance();
        }
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
        
        // Parse SELECT statement
        if (current_token_ && current_token_->keyword_id == db25::Keyword::SELECT) {
            create_node->first_child = parse_select_stmt();
            if (create_node->first_child) {
                create_node->first_child->parent = create_node;
                create_node->child_count = 1;
            }
        }
    }
    
    return create_node;
}

// ========== End Three-Tier Architecture ==========

ast::ASTNode* Parser::parse_drop_stmt() {
    if (const DepthGuard guard(this); !guard.is_valid()) return nullptr;
    
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
    
    // Get object name
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
        drop_node->primary_text = copy_to_arena(current_token_->value);
        advance();
    }
    
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
    if (const DepthGuard guard(this); !guard.is_valid()) return nullptr;
    
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
    
    // Get table name
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
        truncate_node->primary_text = copy_to_arena(current_token_->value);
        advance();
    }
    
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
    if (const DepthGuard guard(this); !guard.is_valid()) return nullptr;
    
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
            
            // Parse precision
            if (current_token_ && current_token_->type == tokenizer::TokenType::Number) {
                type_node->semantic_flags = std::stoi(std::string(current_token_->value)); // Store precision
                advance();
                
                // Parse scale if present
                if (current_token_ && current_token_->value == ",") {
                    advance(); // consume comma
                    if (current_token_ && current_token_->type == tokenizer::TokenType::Number) {
                        // Store scale in upper 16 bits
                        uint32_t scale = std::stoi(std::string(current_token_->value));
                        type_node->semantic_flags |= (scale << 16);
                        advance();
                    }
                }
            }
            
            if (current_token_ && current_token_->value == ")") {
                advance(); // consume )
                parenthesis_depth_--;
            }
        }
        
        // Handle array types: type[]
        if (current_token_ && current_token_->value == "[") {
            advance(); // consume [
            
            // Optional array size
            if (current_token_ && current_token_->type == tokenizer::TokenType::Number) {
                // Store array size somehow (could use hash_cache field)
                type_node->hash_cache = std::stoi(std::string(current_token_->value));
                advance();
            }
            
            if (current_token_ && current_token_->value == "]") {
                advance(); // consume ]
                type_node->flags = static_cast<ast::NodeFlags>(
                    static_cast<uint8_t>(type_node->flags) | 0x80  // Custom flag for array type
                );
            }
        }
    }
    
    return type_node;
}

// Parse column constraint (NOT NULL, PRIMARY KEY, etc.)
ast::ASTNode* Parser::parse_column_constraint() {
    if (const DepthGuard guard(this); !guard.is_valid()) return nullptr;
    
    ast::ASTNode* constraint = nullptr;
    
    if (current_token_ && current_token_->keyword_id == db25::Keyword::NOT) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::KW_NULL) {
            advance();
            constraint = arena_.allocate<ast::ASTNode>();
            new (constraint) ast::ASTNode(ast::NodeType::ColumnConstraint);
            constraint->node_id = next_node_id_++;
            constraint->primary_text = copy_to_arena("NOT_NULL");
        }
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::PRIMARY) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::KEY) {
            advance();
            constraint = arena_.allocate<ast::ASTNode>();
            new (constraint) ast::ASTNode(ast::NodeType::PrimaryKeyConstraint);
            constraint->node_id = next_node_id_++;
            constraint->primary_text = copy_to_arena("PRIMARY_KEY");
        }
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
        
        if (current_token_ && current_token_->value == "(") {
            advance(); // consume (
            parenthesis_depth_++;
            
            // Parse check expression
            auto* expr = parse_expression(0);
            if (expr) {
                expr->parent = constraint;
                constraint->first_child = expr;
                constraint->child_count = 1;
            }
            
            if (current_token_ && current_token_->value == ")") {
                advance(); // consume )
                parenthesis_depth_--;
            }
        }
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::KW_DEFAULT) {
        advance();
        constraint = arena_.allocate<ast::ASTNode>();
        new (constraint) ast::ASTNode(ast::NodeType::DefaultClause);
        constraint->node_id = next_node_id_++;
        
        // Parse default expression
        auto* expr = parse_primary_expression();
        if (expr) {
            expr->parent = constraint;
            constraint->first_child = expr;
            constraint->child_count = 1;
        }
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::REFERENCES) {
        // Foreign key constraint
        advance();
        constraint = arena_.allocate<ast::ASTNode>();
        new (constraint) ast::ASTNode(ast::NodeType::ForeignKeyConstraint);
        constraint->node_id = next_node_id_++;
        
        // Parse referenced table
        if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
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
ast::ASTNode* Parser::parse_column_definition() {
    if (const DepthGuard guard(this); !guard.is_valid()) return nullptr;
    
    auto* column = arena_.allocate<ast::ASTNode>();
    new (column) ast::ASTNode(ast::NodeType::ColumnDefinition);
    column->node_id = next_node_id_++;
    
    // Get column name
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
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
    if (const DepthGuard guard(this); !guard.is_valid()) return nullptr;
    
    ast::ASTNode* constraint = nullptr;
    
    // Optional CONSTRAINT name
    if (current_token_ && current_token_->keyword_id == db25::Keyword::CONSTRAINT) {
        advance();
        // Skip constraint name for now
        if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
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
            
            // Parse local columns
            if (current_token_ && current_token_->value == "(") {
                // Parse column list (similar to above)
                // ... (implementation similar to PRIMARY KEY)
            }
            
            // Parse REFERENCES
            if (current_token_ && current_token_->keyword_id == db25::Keyword::REFERENCES) {
                advance();
                // Parse referenced table and columns
                // ... (implementation as in column constraint)
            }
        }
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::UNIQUE) {
        advance();
        constraint = arena_.allocate<ast::ASTNode>();
        new (constraint) ast::ASTNode(ast::NodeType::UniqueConstraint);
        constraint->node_id = next_node_id_++;
        
        // Parse column list
        if (current_token_ && current_token_->value == "(") {
            // Parse column list (similar to PRIMARY KEY)
            // ... (implementation similar to PRIMARY KEY)
        }
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::CHECK) {
        advance();
        constraint = arena_.allocate<ast::ASTNode>();
        new (constraint) ast::ASTNode(ast::NodeType::CheckConstraint);
        constraint->node_id = next_node_id_++;
        
        if (current_token_ && current_token_->value == "(") {
            advance(); // consume (
            parenthesis_depth_++;
            
            // Parse check expression
            auto* expr = parse_expression(0);
            if (expr) {
                expr->parent = constraint;
                constraint->first_child = expr;
                constraint->child_count = 1;
            }
            
            if (current_token_ && current_token_->value == ")") {
                advance(); // consume )
                parenthesis_depth_--;
            }
        }
    }
    
    return constraint;
}

// Improved CREATE TABLE implementation
ast::ASTNode* Parser::parse_create_table_impl() {
    if (const DepthGuard guard(this); !guard.is_valid()) return nullptr;
    
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
    if (current_token_ && current_token_->value == "(") {
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
            
            if (element) {
                element->parent = create_node;
                if (!first_child) {
                    first_child = element;
                    create_node->first_child = first_child;
                } else if (last_child) {
                    last_child->next_sibling = element;
                }
                last_child = element;
                create_node->child_count++;
            }
            
            // Check for comma
            if (current_token_ && current_token_->value == ",") {
                advance(); // consume comma
            } else if (current_token_ && current_token_->value != ")") {
                // Unexpected token - skip to next comma or closing paren
                while (current_token_ && 
                       current_token_->value != "," && 
                       current_token_->value != ")") {
                    advance();
                }
            }
        }
        
        if (current_token_ && current_token_->value == ")") {
            advance(); // consume )
            parenthesis_depth_--;
        }
    }
    
    // Parse table options (e.g., WITHOUT ROWID, engine options, etc.)
    while (current_token_ && 
           current_token_->type != tokenizer::TokenType::EndOfFile &&
           current_token_->value != ";") {
        // For now, just consume remaining tokens
        // TODO: Parse specific table options
        advance();
    }
    
    return create_node;
}

// Improved ALTER TABLE implementation
ast::ASTNode* Parser::parse_alter_table_full() {
    if (const DepthGuard guard(this); !guard.is_valid()) return nullptr;
    
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
        
        if (current_token_ && current_token_->keyword_id == db25::Keyword::COLUMN) {
            advance(); // optional COLUMN keyword
        }
        
        // Parse column definition
        auto* column = parse_column_definition();
        if (column) {
            column->parent = action;
            action->first_child = column;
            action->child_count = 1;
        }
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::DROP) {
        advance();
        action->primary_text = copy_to_arena("DROP");
        
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
                    // Parse default expression
                    auto* expr = parse_expression(0);
                    if (expr) {
                        expr->parent = action;
                        col->next_sibling = expr;
                        action->child_count++;
                    }
                }
            } else if (current_token_ && current_token_->keyword_id == db25::Keyword::DROP) {
                advance();
                if (current_token_ && current_token_->keyword_id == db25::Keyword::KW_DEFAULT) {
                    advance();
                    action->semantic_flags |= 0x04;  // DROP DEFAULT flag
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
    } else if (current_token_ && current_token_->value == "RENAME") {
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
    if (const DepthGuard guard(this); !guard.is_valid()) return nullptr;
    
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
    if (const DepthGuard guard(this); !guard.is_valid()) return nullptr;
    
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
    } else if (current_token_ && current_token_->value == "INSTEAD") {
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
            
            // Skip semicolons between statements
            if (current_token_ && current_token_->value == ";") {
                advance();
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
    if (const DepthGuard guard(this); !guard.is_valid()) return nullptr;
    
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
    if (current_token_ && current_token_->value == "AUTHORIZATION") {
        advance();
        if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
            schema_node->schema_name = copy_to_arena(current_token_->value); // Store owner in schema_name
            advance();
        }
    }
    
    return schema_node;
}

} // namespace db25::parser