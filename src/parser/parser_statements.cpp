/*
 * DB25 Parser - Statement Implementations
 * This file contains implementations for transaction control, utility statements,
 * and other SQL statement parsers.
 */

#include "db25/parser/parser.hpp"
#include "db25/parser/tokenizer_adapter.hpp"

namespace db25::parser {

// ========== Transaction Control Statements ==========

ast::ASTNode* Parser::parse_transaction_stmt() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;

    if (const auto keyword_id = current_token_->keyword_id; keyword_id == db25::Keyword::BEGIN || keyword_id == db25::Keyword::START) {
        // BEGIN [TRANSACTION|WORK] [isolation_level]
        auto* begin_node = arena_.allocate<ast::ASTNode>();
        new (begin_node) ast::ASTNode(ast::NodeType::BeginStmt);
        begin_node->node_id = next_node_id_++;
        
        advance(); // consume BEGIN/START
        
        // Check for TRANSACTION or WORK
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
            if (current_token_->keyword_id == db25::Keyword::TRANSACTION ||
                current_token_->value == "WORK" || current_token_->value == "work") {
                advance(); // consume TRANSACTION/WORK
            }
        }
        
        // TODO: Parse transaction modes (ISOLATION LEVEL, READ WRITE/ONLY, etc.)
        
        return begin_node;
        
    } else if (keyword_id == db25::Keyword::COMMIT) {
        // COMMIT [TRANSACTION|WORK] [AND [NO] CHAIN]
        auto* commit_node = arena_.allocate<ast::ASTNode>();
        new (commit_node) ast::ASTNode(ast::NodeType::CommitStmt);
        commit_node->node_id = next_node_id_++;
        
        advance(); // consume COMMIT
        
        // Check for TRANSACTION or WORK
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
            if (current_token_->keyword_id == db25::Keyword::TRANSACTION ||
                current_token_->value == "WORK" || current_token_->value == "work") {
                advance(); // consume TRANSACTION/WORK
            }
        }
        
        // Check for AND CHAIN
        if (current_token_ && current_token_->keyword_id == db25::Keyword::AND) {
            advance(); // consume AND
            if (current_token_ && (current_token_->value == "CHAIN" || current_token_->value == "chain")) {
                commit_node->semantic_flags |= 0x01; // Set CHAIN flag
                advance(); // consume CHAIN
            }
        }
        
        return commit_node;
        
    } else if (keyword_id == db25::Keyword::ROLLBACK) {
        // ROLLBACK [TRANSACTION|WORK] [TO [SAVEPOINT] name]
        auto* rollback_node = arena_.allocate<ast::ASTNode>();
        new (rollback_node) ast::ASTNode(ast::NodeType::RollbackStmt);
        rollback_node->node_id = next_node_id_++;
        
        advance(); // consume ROLLBACK
        
        // Check for TRANSACTION or WORK
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
            if (current_token_->keyword_id == db25::Keyword::TRANSACTION ||
                current_token_->value == "WORK" || current_token_->value == "work") {
                advance(); // consume TRANSACTION/WORK
            }
        }
        
        // Check for TO SAVEPOINT
        if (current_token_ && current_token_->keyword_id == db25::Keyword::TO) {
            advance(); // consume TO

            // Optional SAVEPOINT keyword
            if (current_token_ && current_token_->keyword_id == db25::Keyword::SAVEPOINT) {
                advance(); // consume SAVEPOINT
            }

            // Parse savepoint name (required once TO is present). `ROLLBACK TO`
            // with no name was previously accepted with an empty target. A
            // keyword is accepted as a name (parser-wide keyword-as-name leniency).
            if (!current_token_ || (current_token_->type != tokenizer::TokenType::Identifier &&
                                    current_token_->type != tokenizer::TokenType::Keyword)) {
                error("expected a savepoint name after ROLLBACK TO");
                return nullptr;
            }
            rollback_node->primary_text = copy_to_arena(current_token_->value);
            advance();
        }

        return rollback_node;
        
    } else if (keyword_id == db25::Keyword::SAVEPOINT) {
        // SAVEPOINT name
        auto* savepoint_node = arena_.allocate<ast::ASTNode>();
        new (savepoint_node) ast::ASTNode(ast::NodeType::SavepointStmt);
        savepoint_node->node_id = next_node_id_++;
        
        advance(); // consume SAVEPOINT

        // Parse savepoint name (required). `SAVEPOINT` with no name was
        // previously accepted as a nameless SavepointStmt. A keyword is accepted
        // as a name (parser-wide keyword-as-name leniency).
        if (!current_token_ || (current_token_->type != tokenizer::TokenType::Identifier &&
                                current_token_->type != tokenizer::TokenType::Keyword)) {
            error("expected a savepoint name after SAVEPOINT");
            return nullptr;
        }
        savepoint_node->primary_text = copy_to_arena(current_token_->value);
        advance();

        return savepoint_node;
        
    } else if (keyword_id == db25::Keyword::RELEASE) {
        // RELEASE [SAVEPOINT] name
        auto* release_node = arena_.allocate<ast::ASTNode>();
        new (release_node) ast::ASTNode(ast::NodeType::ReleaseSavepointStmt);
        release_node->node_id = next_node_id_++;
        
        advance(); // consume RELEASE
        
        // Optional SAVEPOINT keyword
        if (current_token_ && current_token_->keyword_id == db25::Keyword::SAVEPOINT) {
            advance(); // consume SAVEPOINT
        }

        // Parse savepoint name (required). `RELEASE` / `RELEASE SAVEPOINT` with
        // no name was previously accepted as a nameless ReleaseSavepointStmt. A
        // keyword is accepted as a name (parser-wide keyword-as-name leniency).
        if (!current_token_ || (current_token_->type != tokenizer::TokenType::Identifier &&
                                current_token_->type != tokenizer::TokenType::Keyword)) {
            error("expected a savepoint name after RELEASE");
            return nullptr;
        }
        release_node->primary_text = copy_to_arena(current_token_->value);
        advance();

        return release_node;
    }
    
    return nullptr;
}

// ========== EXPLAIN Statement ==========

ast::ASTNode* Parser::parse_explain_stmt() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    auto* explain_node = arena_.allocate<ast::ASTNode>();
    new (explain_node) ast::ASTNode(ast::NodeType::ExplainStmt);
    explain_node->node_id = next_node_id_++;
    
    advance(); // consume EXPLAIN
    
    // Check for QUERY PLAN
    if (current_token_ && current_token_->value == "QUERY") {
        advance(); // consume QUERY
        if (current_token_ && current_token_->value == "PLAN") {
            explain_node->semantic_flags |= 0x01; // Set QUERY PLAN flag
            advance(); // consume PLAN
        }
    } else if (current_token_ && current_token_->value == "ANALYZE") {
        explain_node->semantic_flags |= 0x02; // Set ANALYZE flag
        advance(); // consume ANALYZE
    }
    
    // Parse the statement to explain. EXPLAIN requires one; `EXPLAIN` (or
    // `EXPLAIN ANALYZE`) with no statement previously produced a childless
    // ExplainStmt and a clean parse of an incomplete statement.
    auto* stmt = parse_statement();
    if (!stmt) {
        error("expected a statement to EXPLAIN");
        return nullptr;
    }
    stmt->parent = explain_node;
    explain_node->first_child = stmt;
    explain_node->child_count = 1;

    return explain_node;
}

// ========== VALUES Statement ==========

ast::ASTNode* Parser::parse_values_stmt() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;

    // If this VALUES is the right-hand operand of an enclosing set operation
    // (`SELECT 1 UNION VALUES (2) ORDER BY 1`), it must NOT swallow a trailing
    // ORDER BY / LIMIT: those bind to the WHOLE set operation and are parsed
    // once, after folding (see fold_set_operations / attach_trailing_order_limit),
    // exactly as the bare-SELECT arm handles it. Capture and clear the flag so
    // any nested parse behaves normally; a parenthesized `(VALUES ...)` operand
    // re-enters with the flag already cleared and so keeps its own ORDER BY.
    const bool is_setop_rhs = in_setop_rhs_;
    in_setop_rhs_ = false;

    auto* values_node = arena_.allocate<ast::ASTNode>();
    new (values_node) ast::ASTNode(ast::NodeType::ValuesStmt);
    values_node->node_id = next_node_id_++;
    
    advance(); // consume VALUES
    
    // Parse value lists
    auto* values_clause = arena_.allocate<ast::ASTNode>();
    new (values_clause) ast::ASTNode(ast::NodeType::ValuesClause);
    values_clause->node_id = next_node_id_++;
    values_clause->parent = values_node;
    
    ast::ASTNode* first_row = nullptr;
    ast::ASTNode* last_row = nullptr;
    
    // Parse rows: (expr, expr), (expr, expr), ...
    do {
        if (current_token_ && current_token_->value == "(") {
            advance(); // consume (
            parenthesis_depth_++;
            
            // Create row node
            auto* row_node = arena_.allocate<ast::ASTNode>();
            new (row_node) ast::ASTNode(ast::NodeType::ColumnList); // Reuse for row values
            row_node->node_id = next_node_id_++;
            row_node->parent = values_clause;
            
            // Parse expressions in this row
            ast::ASTNode* first_expr = nullptr;
            ast::ASTNode* last_expr = nullptr;
            
            while (current_token_ && current_token_->value != ")") {
                auto* expr = parse_expression(0);
                if (!expr) break;
                
                expr->parent = row_node;
                if (!first_expr) {
                    first_expr = expr;
                    row_node->first_child = first_expr;
                } else if (last_expr) {
                    last_expr->next_sibling = expr;
                }
                last_expr = expr;
                row_node->child_count++;
                
                if (current_token_ && current_token_->value == ",") {
                    advance(); // consume comma
                } else {
                    break;
                }
            }
            
            if (current_token_ && current_token_->value == ")") {
                advance(); // consume )
                if (parenthesis_depth_ > 0) parenthesis_depth_--;
            }

            // A row value constructor requires at least one value; an empty row
            // `VALUES ()` previously appended a childless row with a clean parse.
            if (row_node->child_count == 0) {
                error("expected at least one value in the VALUES row");
                return nullptr;
            }

            // Add row to values clause
            if (!first_row) {
                first_row = row_node;
                values_clause->first_child = first_row;
            } else if (last_row) {
                last_row->next_sibling = row_node;
            }
            last_row = row_node;
            values_clause->child_count++;
            
            // Check for comma between rows
            if (current_token_ && current_token_->value == ",") {
                advance(); // consume comma
            } else {
                break;
            }
        } else {
            break;
        }
    } while (true);

    // VALUES requires at least one row; a bare `VALUES` with no `(...)` row
    // previously produced a childless ValuesClause and a clean parse.
    if (values_clause->child_count == 0) {
        error("expected at least one row after VALUES");
        return nullptr;
    }

    values_node->first_child = values_clause;
    values_node->child_count = 1;
    
    // Parse optional ORDER BY (unless this VALUES is a set-op RHS operand, in
    // which case a trailing ORDER BY belongs to the whole set operation).
    if (!is_setop_rhs && current_token_ && current_token_->keyword_id == db25::Keyword::ORDER) {
        advance(); // consume ORDER
        if (current_token_ && current_token_->keyword_id == db25::Keyword::BY) {
            advance(); // consume BY
            auto* order_by = parse_order_by_clause();
            if (order_by) {
                order_by->parent = values_node;
                values_clause->next_sibling = order_by;
                values_node->child_count++;
            }
        }
    }

    // Parse optional LIMIT (same set-op RHS caveat as ORDER BY above).
    if (!is_setop_rhs && current_token_ && current_token_->keyword_id == db25::Keyword::LIMIT) {
        advance(); // consume LIMIT
        auto* limit = parse_limit_clause();
        if (limit) {
            limit->parent = values_node;
            auto* last_child = values_node->first_child;
            while (last_child->next_sibling) {
                last_child = last_child->next_sibling;
            }
            last_child->next_sibling = limit;
            values_node->child_count++;
        }
    }
    
    return values_node;
}

// ========== Utility Statements ==========

ast::ASTNode* Parser::parse_set_stmt() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    auto* set_node = arena_.allocate<ast::ASTNode>();
    new (set_node) ast::ASTNode(ast::NodeType::SetStmt);
    set_node->node_id = next_node_id_++;
    
    advance(); // consume SET
    
    // Check for SESSION/LOCAL
    if (current_token_ && (current_token_->value == "SESSION" || current_token_->value == "LOCAL")) {
        if (current_token_->value == "SESSION") {
            set_node->semantic_flags |= 0x01; // SESSION flag
        } else {
            set_node->semantic_flags |= 0x02; // LOCAL flag
        }
        advance();
    }
    
    // SET requires a parameter to set; a bare `SET` previously produced a
    // childless SetStmt and a clean parse.
    if (!current_token_ ||
        current_token_->type == tokenizer::TokenType::EndOfFile) {
        error("expected a parameter after SET");
        return nullptr;
    }

    // The `name = value` / `name TO value` form. (Keyword-led special forms such
    // as `SET TIME ZONE ...` / `SET NAMES ...` are not modeled and are left as
    // trailing tokens for trailing_token_count(), so only tighten the
    // identifier-name form here.)
    if (current_token_->type == tokenizer::TokenType::Identifier) {
        set_node->primary_text = copy_to_arena(current_token_->value);
        advance();

        // Optional `=` / `TO` before the value (`SET x = 5`, `SET x TO 5`); the
        // space form `SET name value` (`SET NAMES utf8`) omits it.
        if (current_token_ && (current_token_->value == "=" ||
                               current_token_->keyword_id == db25::Keyword::TO)) {
            advance();
        }
        // A value is required either way: `SET x` and `SET x =` (with nothing
        // after) previously parsed cleanly as truncated statements.
        auto* value = parse_expression(0);
        if (!value) {
            error("expected a value for the parameter in SET");
            return nullptr;
        }
        value->parent = set_node;
        set_node->first_child = value;
        set_node->child_count = 1;
    }

    return set_node;
}

ast::ASTNode* Parser::parse_vacuum_stmt() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    auto* vacuum_node = arena_.allocate<ast::ASTNode>();
    new (vacuum_node) ast::ASTNode(ast::NodeType::VacuumStmt);
    vacuum_node->node_id = next_node_id_++;
    
    advance(); // consume VACUUM
    
    // Parse optional schema name or INTO filename
    if (current_token_) {
        if (current_token_->keyword_id == db25::Keyword::INTO) {
            advance(); // consume INTO
            vacuum_node->semantic_flags |= 0x01; // INTO flag

            // Parse filename (required after INTO). `VACUUM INTO` with no
            // filename was previously accepted with an empty target.
            if (!current_token_ || current_token_->type != tokenizer::TokenType::String) {
                error("expected a filename string after VACUUM INTO");
                return nullptr;
            }
            vacuum_node->primary_text = copy_to_arena(current_token_->value);
            advance();
        } else if (current_token_->type == tokenizer::TokenType::Identifier) {
            // Schema name
            vacuum_node->primary_text = copy_to_arena(current_token_->value);
            advance();
        }
    }
    
    return vacuum_node;
}

ast::ASTNode* Parser::parse_analyze_stmt() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    auto* analyze_node = arena_.allocate<ast::ASTNode>();
    new (analyze_node) ast::ASTNode(ast::NodeType::AnalyzeStmt);
    analyze_node->node_id = next_node_id_++;
    
    advance(); // consume ANALYZE
    
    // Parse optional schema.table or table name
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
        auto first_name = current_token_->value;
        advance();
        
        if (current_token_ && current_token_->value == ".") {
            advance(); // consume .
            if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
                analyze_node->schema_name = copy_to_arena(first_name);
                analyze_node->primary_text = copy_to_arena(current_token_->value);
                advance();
            }
        } else {
            analyze_node->primary_text = copy_to_arena(first_name);
        }
    }
    
    return analyze_node;
}

ast::ASTNode* Parser::parse_attach_stmt() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    auto* attach_node = arena_.allocate<ast::ASTNode>();
    new (attach_node) ast::ASTNode(ast::NodeType::AttachStmt);
    attach_node->node_id = next_node_id_++;
    
    advance(); // consume ATTACH
    
    // Optional DATABASE keyword
    if (current_token_ && current_token_->keyword_id == db25::Keyword::DATABASE) {
        advance(); // consume DATABASE
    }
    
    // Parse filename expression. ATTACH requires one; `ATTACH` / `ATTACH
    // DATABASE` with no filename previously produced a childless AttachStmt and
    // a clean parse.
    auto* filename = parse_expression(0);
    if (!filename) {
        error("expected a database filename after ATTACH");
        return nullptr;
    }
    filename->parent = attach_node;
    attach_node->first_child = filename;
    attach_node->child_count = 1;

    // Parse AS schema_name
    if (current_token_ && current_token_->keyword_id == db25::Keyword::AS) {
        advance(); // consume AS
        if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
            attach_node->primary_text = copy_to_arena(current_token_->value);
            advance();
        }
    }

    return attach_node;
}

ast::ASTNode* Parser::parse_detach_stmt() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    auto* detach_node = arena_.allocate<ast::ASTNode>();
    new (detach_node) ast::ASTNode(ast::NodeType::DetachStmt);
    detach_node->node_id = next_node_id_++;
    
    advance(); // consume DETACH
    
    // Optional DATABASE keyword
    if (current_token_ && current_token_->keyword_id == db25::Keyword::DATABASE) {
        advance(); // consume DATABASE
    }
    
    // Parse schema name. DETACH requires one; `DETACH` / `DETACH DATABASE` with
    // no schema name previously produced a childless DetachStmt and a clean
    // parse.
    if (!current_token_ || current_token_->type != tokenizer::TokenType::Identifier) {
        error("expected a schema name after DETACH");
        return nullptr;
    }
    detach_node->primary_text = copy_to_arena(current_token_->value);
    advance();

    return detach_node;
}

ast::ASTNode* Parser::parse_reindex_stmt() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    auto* reindex_node = arena_.allocate<ast::ASTNode>();
    new (reindex_node) ast::ASTNode(ast::NodeType::ReindexStmt);
    reindex_node->node_id = next_node_id_++;
    
    advance(); // consume REINDEX
    
    // Parse optional collation name or schema.table
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
        auto first_name = current_token_->value;
        advance();
        
        if (current_token_ && current_token_->value == ".") {
            advance(); // consume .
            if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
                reindex_node->schema_name = copy_to_arena(first_name);
                reindex_node->primary_text = copy_to_arena(current_token_->value);
                advance();
            }
        } else {
            reindex_node->primary_text = copy_to_arena(first_name);
        }
    }
    
    return reindex_node;
}

ast::ASTNode* Parser::parse_pragma_stmt() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    auto* pragma_node = arena_.allocate<ast::ASTNode>();
    new (pragma_node) ast::ASTNode(ast::NodeType::PragmaStmt);
    pragma_node->node_id = next_node_id_++;
    
    advance(); // consume PRAGMA

    // Parse [schema.]pragma_name (required). `PRAGMA` with no name was
    // previously accepted as a nameless PragmaStmt. A keyword is accepted as a
    // name (parser-wide keyword-as-name leniency; many pragma names are keywords).
    if (!current_token_ || (current_token_->type != tokenizer::TokenType::Identifier &&
                            current_token_->type != tokenizer::TokenType::Keyword)) {
        error("expected a pragma name after PRAGMA");
        return nullptr;
    }
    {
        auto first_name = current_token_->value;
        advance();

        if (current_token_ && current_token_->value == ".") {
            advance(); // consume .
            if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
                pragma_node->schema_name = copy_to_arena(first_name);
                pragma_node->primary_text = copy_to_arena(current_token_->value);
                advance();
            }
        } else {
            pragma_node->primary_text = copy_to_arena(first_name);
        }

        // Parse optional value. When the `=` or `(` value syntax is present a
        // value is required; `PRAGMA foo =` / `PRAGMA foo (` with no value were
        // previously accepted with an empty (childless) pragma.
        if (current_token_) {
            if (current_token_->value == "=") {
                advance(); // consume =
                auto* value = parse_expression(0);
                if (!value) {
                    error("expected a value after '=' in PRAGMA");
                    return nullptr;
                }
                value->parent = pragma_node;
                pragma_node->first_child = value;
                pragma_node->child_count = 1;
            } else if (current_token_->value == "(") {
                advance(); // consume (
                parenthesis_depth_++;
                auto* value = parse_expression(0);
                if (!value) {
                    error("expected a value after '(' in PRAGMA");
                    return nullptr;
                }
                value->parent = pragma_node;
                pragma_node->first_child = value;
                pragma_node->child_count = 1;
                if (!current_token_ || current_token_->value != ")") {
                    error("expected ')' after the PRAGMA value");
                    return nullptr;
                }
                advance(); // consume )
                if (parenthesis_depth_ > 0) parenthesis_depth_--;
            }
        }
    }

    return pragma_node;
}

// ========== DML Clause Implementations ==========


} // namespace db25::parser