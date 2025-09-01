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
    if (const DepthGuard guard(this); !guard.is_valid()) return nullptr;

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
            if (current_token_ && current_token_->value == "SAVEPOINT") {
                advance(); // consume SAVEPOINT
            }
            
            // Parse savepoint name
            if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
                rollback_node->primary_text = copy_to_arena(current_token_->value);
                advance();
            }
        }
        
        return rollback_node;
        
    } else if (keyword_id == db25::Keyword::SAVEPOINT) {
        // SAVEPOINT name
        auto* savepoint_node = arena_.allocate<ast::ASTNode>();
        new (savepoint_node) ast::ASTNode(ast::NodeType::SavepointStmt);
        savepoint_node->node_id = next_node_id_++;
        
        advance(); // consume SAVEPOINT
        
        // Parse savepoint name
        if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
            savepoint_node->primary_text = copy_to_arena(current_token_->value);
            advance();
        }
        
        return savepoint_node;
        
    } else if (keyword_id == db25::Keyword::RELEASE) {
        // RELEASE [SAVEPOINT] name
        auto* release_node = arena_.allocate<ast::ASTNode>();
        new (release_node) ast::ASTNode(ast::NodeType::ReleaseSavepointStmt);
        release_node->node_id = next_node_id_++;
        
        advance(); // consume RELEASE
        
        // Optional SAVEPOINT keyword
        if (current_token_ && current_token_->value == "SAVEPOINT") {
            advance(); // consume SAVEPOINT
        }
        
        // Parse savepoint name
        if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
            release_node->primary_text = copy_to_arena(current_token_->value);
            advance();
        }
        
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
    
    // Parse the statement to explain
    auto* stmt = parse_statement();
    if (stmt) {
        stmt->parent = explain_node;
        explain_node->first_child = stmt;
        explain_node->child_count = 1;
    }
    
    return explain_node;
}

// ========== VALUES Statement ==========

ast::ASTNode* Parser::parse_values_stmt() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
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
    
    values_node->first_child = values_clause;
    values_node->child_count = 1;
    
    // Parse optional ORDER BY
    if (current_token_ && current_token_->keyword_id == db25::Keyword::ORDER) {
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
    
    // Parse optional LIMIT
    if (current_token_ && current_token_->keyword_id == db25::Keyword::LIMIT) {
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
    
    // Parse parameter name
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
        set_node->primary_text = copy_to_arena(current_token_->value);
        advance();
        
        // Parse = or TO
        if (current_token_ && (current_token_->value == "=" || 
                               current_token_->keyword_id == db25::Keyword::TO)) {
            advance();
            
            // Parse value
            auto* value = parse_expression(0);
            if (value) {
                value->parent = set_node;
                set_node->first_child = value;
                set_node->child_count = 1;
            }
        }
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
            
            // Parse filename
            if (current_token_ && current_token_->type == tokenizer::TokenType::String) {
                vacuum_node->primary_text = copy_to_arena(current_token_->value);
                advance();
            }
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
    
    // Parse filename expression
    auto* filename = parse_expression(0);
    if (filename) {
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
    
    // Parse schema name
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
        detach_node->primary_text = copy_to_arena(current_token_->value);
        advance();
    }
    
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
    
    // Parse [schema.]pragma_name
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
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
        
        // Parse optional value
        if (current_token_) {
            if (current_token_->value == "=") {
                advance(); // consume =
                auto* value = parse_expression(0);
                if (value) {
                    value->parent = pragma_node;
                    pragma_node->first_child = value;
                    pragma_node->child_count = 1;
                }
            } else if (current_token_->value == "(") {
                advance(); // consume (
                parenthesis_depth_++;
                auto* value = parse_expression(0);
                if (value) {
                    value->parent = pragma_node;
                    pragma_node->first_child = value;
                    pragma_node->child_count = 1;
                }
                if (current_token_ && current_token_->value == ")") {
                    advance(); // consume )
                    if (parenthesis_depth_ > 0) parenthesis_depth_--;
                }
            }
        }
    }
    
    return pragma_node;
}

// ========== DML Clause Implementations ==========


} // namespace db25::parser