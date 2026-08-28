/*
 * Copyright (c) 2024 DB25 Parser Project
 * 
 * DML Statement Parser Implementation
 * Handles INSERT, UPDATE, DELETE statements
 */

#include "parser_internal.hpp"
#include "db25/ast/ast_node.hpp"
#include "db25/parser/tokenizer_adapter.hpp"

namespace db25::parser {

using namespace internal;

// ========== INSERT Statement ==========

ast::ASTNode* Parser::parse_insert_stmt() {
    DepthGuard guard(this);  // Protect against deep recursion
    if (!guard.is_valid()) return nullptr;
    
    // Consume INSERT keyword
    advance();
    
    // Create INSERT statement node
    auto* insert_node = arena_.allocate<ast::ASTNode>();
    new (insert_node) ast::ASTNode(ast::NodeType::InsertStmt);
    insert_node->node_id = next_node_id_++;
    
    // Expect INTO keyword
    if (!current_token_ || current_token_->keyword_id != db25::Keyword::INTO) {
        return nullptr;  // INSERT requires INTO
    }
    advance();  // consume INTO
    
    // Parse table name
    auto* table_ref = parse_table_reference();
    if (!table_ref) {
        return nullptr;
    }
    table_ref->parent = insert_node;
    insert_node->first_child = table_ref;
    insert_node->child_count = 1;
    
    ast::ASTNode* last_child = table_ref;
    
    // Optional column list: (col1, col2, ...)
    // NB: '(' is tokenized as a Delimiter, so match on the value only.
    if (current_token_ && current_token_->value == "(") {
        
        // Check if it's a column list or VALUES by looking ahead
        // Column list is followed by ) then VALUES/SELECT/DEFAULT
        // VALUES starts directly with expressions
        size_t save_pos = tokenizer_ ? tokenizer_->position() : 0;
        const tokenizer::Token* save_token = current_token_;
        int save_depth = parenthesis_depth_;
        
        advance();  // consume (
        parenthesis_depth_++;
        
        bool is_column_list = false;
        
        // If we see an identifier followed by comma or ), it's likely a column list
        // Also check for keyword that could be a column name  
        if (current_token_ && (current_token_->type == tokenizer::TokenType::Identifier ||
                               current_token_->type == tokenizer::TokenType::Keyword)) {
            // Save this token
            const tokenizer::Token* first_item = current_token_;
            advance();
            
            // Check what follows
            if (current_token_ && 
                (current_token_->value == "," || current_token_->value == ")")) {
                // It's a column list
                is_column_list = true;
            }
        }
        
        // Restore position
        parenthesis_depth_ = save_depth;
        current_token_ = save_token;
        if (tokenizer_) tokenizer_->set_position(save_pos);
        
        if (is_column_list) {
            // Parse column list
            advance();  // consume (
            parenthesis_depth_++;
            
            auto* column_list = arena_.allocate<ast::ASTNode>();
            new (column_list) ast::ASTNode(ast::NodeType::ColumnList);
            column_list->node_id = next_node_id_++;
            
            ast::ASTNode* first_col = nullptr;
            ast::ASTNode* last_col = nullptr;
            
            // Parse column names
            while (current_token_ && parenthesis_depth_ > save_depth) {
                if (current_token_->type == tokenizer::TokenType::Identifier ||
                    current_token_->type == tokenizer::TokenType::Keyword) {
                    auto* col = arena_.allocate<ast::ASTNode>();
                    new (col) ast::ASTNode(ast::NodeType::Identifier);
                    col->node_id = next_node_id_++;
                    col->primary_text = copy_to_arena(current_token_->value);
                    col->parent = column_list;
                    
                    if (!first_col) {
                        first_col = col;
                        column_list->first_child = first_col;
                    } else {
                        last_col->next_sibling = col;
                    }
                    last_col = col;
                    column_list->child_count++;
                    
                    advance();
                    
                    // Check for comma
                    if (current_token_ && current_token_->value == ",") {
                        advance();
                    }
                } else if (current_token_->value == ")") {
                    advance();
                    parenthesis_depth_--;
                    break;
                } else {
                    // Unexpected token in column list
                    advance();
                }
            }
            
            column_list->parent = insert_node;
            last_child->next_sibling = column_list;
            last_child = column_list;
            insert_node->child_count++;
        }
    }
    
    // Now parse VALUES or SELECT or DEFAULT VALUES. An INSERT requires exactly
    // one of these data sources; `source_added` tracks that a real one was
    // parsed so a source-less INSERT (`INSERT INTO t`, `INSERT INTO t (a,b)`,
    // a rowless `INSERT INTO t VALUES`, an empty `INSERT INTO t SELECT`, or a
    // bare `INSERT INTO t DEFAULT`) is rejected below rather than silently
    // producing a statement with nothing to insert.
    bool source_added = false;
    if (current_token_ && current_token_->keyword_id == db25::Keyword::VALUES) {
        advance();  // consume VALUES
        
        // Create VALUES node
        auto* values_node = arena_.allocate<ast::ASTNode>();
        new (values_node) ast::ASTNode(ast::NodeType::ValuesClause);  // Using existing type
        values_node->node_id = next_node_id_++;
        
        ast::ASTNode* first_value_set = nullptr;
        ast::ASTNode* last_value_set = nullptr;
        
        // Parse value sets: (val1, val2), (val3, val4), ...
        while (current_token_) {
            if (current_token_->value == "(") {
                advance();  // consume (
                parenthesis_depth_++;
                
                auto* value_set = arena_.allocate<ast::ASTNode>();
                new (value_set) ast::ASTNode(ast::NodeType::Subquery);  // Using existing type as placeholder
                value_set->node_id = next_node_id_++;
                
                ast::ASTNode* first_val = nullptr;
                ast::ASTNode* last_val = nullptr;
                
                // Parse expressions in this value set
                while (current_token_ && current_token_->value != ")") {
                    auto* expr = parse_expression(0);
                    if (expr) {
                        expr->parent = value_set;
                        if (!first_val) {
                            first_val = expr;
                            value_set->first_child = first_val;
                        } else {
                            last_val->next_sibling = expr;
                        }
                        last_val = expr;
                        value_set->child_count++;
                    }
                    
                    if (current_token_ && current_token_->value == ",") {
                        advance();
                    } else if (current_token_ && current_token_->value != ")") {
                        // Unexpected token
                        break;
                    }
                }
                
                if (current_token_ && current_token_->value == ")") {
                    advance();
                    parenthesis_depth_--;
                }

                // A row value constructor requires at least one value; an empty
                // row `VALUES ()` previously appended a childless row node with a
                // clean parse (the inner loop simply never iterated).
                if (value_set->child_count == 0) {
                    error("expected at least one value in the VALUES row");
                    return nullptr;
                }

                value_set->parent = values_node;
                if (!first_value_set) {
                    first_value_set = value_set;
                    values_node->first_child = first_value_set;
                } else {
                    last_value_set->next_sibling = value_set;
                }
                last_value_set = value_set;
                values_node->child_count++;
                
                // Check for more value sets
                if (current_token_ && current_token_->value == ",") {
                    advance();
                } else {
                    break;  // No more value sets
                }
            } else {
                break;  // Expected (
            }
        }
        
        // VALUES requires at least one row: `INSERT INTO t VALUES` (no `(...)`)
        // produced a childless ValuesClause.
        if (values_node->child_count == 0) {
            error("expected at least one row after VALUES");
            return nullptr;
        }
        values_node->parent = insert_node;
        last_child->next_sibling = values_node;
        last_child = values_node;
        insert_node->child_count++;
        source_added = true;

    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::SELECT) {
        // INSERT INTO ... SELECT
        auto* select_stmt = parse_select_stmt();
        if (select_stmt) {
            select_stmt->parent = insert_node;
            last_child->next_sibling = select_stmt;
            last_child = select_stmt;
            insert_node->child_count++;
            source_added = true;
        }
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::KW_DEFAULT) {
        advance();  // consume DEFAULT
        // DEFAULT must be followed by VALUES; a bare `INSERT INTO t DEFAULT`
        // previously consumed and dropped the DEFAULT with no source.
        if (current_token_ && current_token_->keyword_id == db25::Keyword::VALUES) {
            advance();  // consume VALUES
            // Create a marker node for DEFAULT VALUES
            auto* default_values = arena_.allocate<ast::ASTNode>();
            new (default_values) ast::ASTNode(ast::NodeType::DefaultClause);  // Using existing type
            default_values->node_id = next_node_id_++;
            default_values->primary_text = copy_to_arena("DEFAULT VALUES");
            default_values->parent = insert_node;
            last_child->next_sibling = default_values;
            last_child = default_values;
            insert_node->child_count++;
            source_added = true;
        }
    }

    // An INSERT with no data source is a syntax error.
    if (!source_added) {
        error("INSERT requires VALUES, a query, or DEFAULT VALUES");
        return nullptr;
    }

    // Optional ON CONFLICT clause (for UPSERT). Delegate to the fully
    // implemented helper, which parses the conflict target column list and the
    // DO UPDATE SET assignments (the previous inline version stubbed both out).
    if (current_token_ && current_token_->keyword_id == db25::Keyword::ON) {
        auto* on_conflict = parse_on_conflict_clause();
        if (on_conflict) {
            on_conflict->parent = insert_node;
            last_child->next_sibling = on_conflict;
            last_child = on_conflict;
            insert_node->child_count++;
        }
    }

    // Optional RETURNING clause (PostgreSQL extension). The helper already
    // gates on keyword_id, so lowercase "returning" is honored.
    if (current_token_ && current_token_->keyword_id == db25::Keyword::RETURNING) {
        auto* returning_clause = parse_returning_clause();
        if (returning_clause) {
            returning_clause->parent = insert_node;
            last_child->next_sibling = returning_clause;
            last_child = returning_clause;
            insert_node->child_count++;
        }
    }

    return insert_node;
}

// ========== UPDATE Statement ==========

ast::ASTNode* Parser::parse_update_stmt() {
    DepthGuard guard(this);  // Protect against deep recursion
    if (!guard.is_valid()) return nullptr;
    
    // Consume UPDATE keyword
    advance();
    
    // Create UPDATE statement node
    auto* update_node = arena_.allocate<ast::ASTNode>();
    new (update_node) ast::ASTNode(ast::NodeType::UpdateStmt);
    update_node->node_id = next_node_id_++;
    
    // Parse table name
    auto* table_ref = parse_table_reference();
    if (!table_ref) {
        return nullptr;
    }
    table_ref->parent = update_node;
    update_node->first_child = table_ref;
    update_node->child_count = 1;
    
    ast::ASTNode* last_child = table_ref;
    
    // Expect SET keyword
    if (!current_token_ || current_token_->keyword_id != db25::Keyword::SET) {
        return nullptr;  // UPDATE requires SET
    }
    advance();  // consume SET
    
    // Create SET clause node
    auto* set_clause = arena_.allocate<ast::ASTNode>();
    new (set_clause) ast::ASTNode(ast::NodeType::SetClause);
    set_clause->node_id = next_node_id_++;
    
    ast::ASTNode* first_assignment = nullptr;
    ast::ASTNode* last_assignment = nullptr;
    
    // Parse assignments: col1 = expr1, col2 = expr2, ...
    bool first = true;
    while (current_token_) {
        // Don't require comma for first item
        if (!first) {
            // After first item, we need a comma to continue
            if (!current_token_ || current_token_->value != ",") {
                break;
            }
            advance();  // consume comma
        }
        first = false;
        
        // Parse column name. A SET assignment target must be a bare column name.
        // If one is not present here a column was EXPECTED (this is either the
        // first target after SET, or the token right after a consumed comma), so
        // anything else is malformed - reject the statement rather than silently
        // stopping. Silently breaking left an UpdateStmt with an empty/partial
        // SET and every following clause (WHERE / FROM / RETURNING) discarded as
        // leftover tokens with no diagnostic - e.g. the SQL row-assignment form
        // `SET (a,b)=(1,2) WHERE id=1` parsed as SET-nothing and dropped the WHERE
        // filter. The parenthesized multi-column assignment is not supported; it
        // is rejected cleanly (like OVER <named-window> and DISTINCT ON).
        if (!current_token_ ||
            (current_token_->type != tokenizer::TokenType::Identifier &&
             current_token_->type != tokenizer::TokenType::Keyword)) {
            return nullptr;  // Expected a column name for the SET target
        }
        
        auto* assignment = arena_.allocate<ast::ASTNode>();
        new (assignment) ast::ASTNode(ast::NodeType::BinaryExpr);  // Using binary expr for assignment
        assignment->node_id = next_node_id_++;
        assignment->semantic_flags |= 0x1000;  // Mark as assignment
        
        // Store column name
        assignment->primary_text = copy_to_arena(current_token_->value);
        advance();
        
        // Expect = operator. A column name with no following `=` is malformed
        // (`SET a b ...`); reject rather than silently dropping this and every
        // subsequent clause.
        if (!current_token_ || current_token_->value != "=") {
            return nullptr;  // Expected '=' after the SET target
        }
        advance();  // consume =

        // Parse the value expression. The `=` was consumed, so a value MUST
        // follow. A null here (a clause keyword like WHERE/FROM/RETURNING after
        // `=`, e.g. `SET a = WHERE id=1`) does not record an error on its own,
        // so guard it - otherwise a value-less assignment node (no child) was
        // emitted and the parse succeeded, handing the analyzer an assignment
        // missing its required value. Matches the neighbouring `SET a b` /
        // parenthesized-row rejections.
        auto* expr = parse_expression(0);
        if (!expr) {
            error("expected a value expression after '=' in the SET clause");
            return nullptr;
        }
        expr->parent = assignment;
        assignment->first_child = expr;
        assignment->child_count = 1;

        assignment->parent = set_clause;
        if (!first_assignment) {
            first_assignment = assignment;
            set_clause->first_child = first_assignment;
        } else {
            last_assignment->next_sibling = assignment;
        }
        last_assignment = assignment;
        set_clause->child_count++;
        
        // Check if we've hit a clause keyword
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
            auto kw = current_token_->keyword_id;
            if (kw == db25::Keyword::WHERE || kw == db25::Keyword::FROM ||
                kw == db25::Keyword::RETURNING) {
                break;  // End of SET clause
            }
        }
    }
    
    set_clause->parent = update_node;
    last_child->next_sibling = set_clause;
    last_child = set_clause;
    update_node->child_count++;
    
    // Optional FROM clause (PostgreSQL extension). parse_from_clause() begins at
    // the first table reference (as in SELECT), so the FROM keyword must be
    // consumed first - without this the clause failed to parse and was dropped,
    // leaving `... FROM extra WHERE ...` columns unresolved.
    if (current_token_ && current_token_->keyword_id == db25::Keyword::FROM) {
        advance();  // consume FROM
        auto* from_clause = parse_from_clause();
        if (from_clause) {
            from_clause->parent = update_node;
            last_child->next_sibling = from_clause;
            last_child = from_clause;
            update_node->child_count++;
        }
    }
    
    // Optional WHERE clause. Once WHERE is present a predicate is required;
    // `UPDATE t SET a=1 WHERE` with no condition was previously accepted with
    // the dangling WHERE silently dropped.
    if (current_token_ && current_token_->keyword_id == db25::Keyword::WHERE) {
        advance();  // consume WHERE (parse_where_clause parses only the condition)
        auto* where_clause = parse_where_clause();
        if (!where_clause) {
            error("expected a condition after WHERE in UPDATE");
            return nullptr;
        }
        where_clause->parent = update_node;
        last_child->next_sibling = where_clause;
        last_child = where_clause;
        update_node->child_count++;
    }
    
    // Optional RETURNING clause (PostgreSQL extension). Parse each item as a
    // full expression via the shared helper (as INSERT does). The previous
    // inline loop used parse_column_ref(), which parsed only a leading bare
    // column and dropped the rest: `RETURNING id, k+1, name` truncated to
    // [id, k] (the `+1` desynced the comma loop, dropping `name`), silently
    // losing RETURNING output columns.
    if (current_token_ && current_token_->keyword_id == db25::Keyword::RETURNING) {
        auto* returning_clause = parse_returning_clause();
        if (!returning_clause) {
            return nullptr;  // helper already emitted the error
        }
        returning_clause->parent = update_node;
        last_child->next_sibling = returning_clause;
        update_node->child_count++;
    }

    return update_node;
}

// ========== DELETE Statement ==========

ast::ASTNode* Parser::parse_delete_stmt() {
    DepthGuard guard(this);  // Protect against deep recursion
    if (!guard.is_valid()) return nullptr;
    
    // Consume DELETE keyword
    advance();
    
    // Create DELETE statement node
    auto* delete_node = arena_.allocate<ast::ASTNode>();
    new (delete_node) ast::ASTNode(ast::NodeType::DeleteStmt);
    delete_node->node_id = next_node_id_++;
    
    // Expect FROM keyword
    if (!current_token_ || current_token_->keyword_id != db25::Keyword::FROM) {
        return nullptr;  // DELETE requires FROM
    }
    advance();  // consume FROM
    
    // Parse table name
    auto* table_ref = parse_table_reference();
    if (!table_ref) {
        return nullptr;
    }
    table_ref->parent = delete_node;
    delete_node->first_child = table_ref;
    delete_node->child_count = 1;
    
    ast::ASTNode* last_child = table_ref;
    
    // Optional USING clause (PostgreSQL extension)
    if (current_token_ && current_token_->keyword_id == db25::Keyword::USING) {
        advance();  // consume USING
        
        auto* using_clause = arena_.allocate<ast::ASTNode>();
        new (using_clause) ast::ASTNode(ast::NodeType::UsingClause);
        using_clause->node_id = next_node_id_++;
        
        // Parse table references
        ast::ASTNode* first_table = nullptr;
        ast::ASTNode* last_table = nullptr;
        
        while (current_token_) {
            auto* table = parse_table_reference();
            if (!table) break;
            
            table->parent = using_clause;
            if (!first_table) {
                first_table = table;
                using_clause->first_child = first_table;
            } else {
                last_table->next_sibling = table;
            }
            last_table = table;
            using_clause->child_count++;
            
            if (current_token_ && current_token_->value == ",") {
                advance();
            } else {
                break;
            }
        }
        
        using_clause->parent = delete_node;
        last_child->next_sibling = using_clause;
        last_child = using_clause;
        delete_node->child_count++;
    }
    
    // Optional WHERE clause. Once WHERE is present a predicate is required;
    // `DELETE FROM t WHERE` with no condition was previously accepted with the
    // dangling WHERE silently dropped.
    if (current_token_ && current_token_->keyword_id == db25::Keyword::WHERE) {
        advance();  // consume WHERE (parse_where_clause parses only the condition)
        auto* where_clause = parse_where_clause();
        if (!where_clause) {
            error("expected a condition after WHERE in DELETE");
            return nullptr;
        }
        where_clause->parent = delete_node;
        last_child->next_sibling = where_clause;
        last_child = where_clause;
        delete_node->child_count++;
    }

    // Optional RETURNING clause (PostgreSQL extension). Parse each item as a
    // full expression via the shared helper (as INSERT does); the previous
    // inline parse_column_ref() loop truncated expression items and dropped
    // list items after the first non-bare-column one.
    if (current_token_ && current_token_->keyword_id == db25::Keyword::RETURNING) {
        auto* returning_clause = parse_returning_clause();
        if (!returning_clause) {
            return nullptr;  // helper already emitted the error
        }
        returning_clause->parent = delete_node;
        last_child->next_sibling = returning_clause;
        delete_node->child_count++;
    }

    return delete_node;
}

// ========== DML Helper Methods ==========

ast::ASTNode* Parser::parse_returning_clause() {
    // RETURNING clause implementation
    if (!current_token_ || current_token_->keyword_id != db25::Keyword::RETURNING) {
        return nullptr;
    }
    advance();  // consume RETURNING
    
    auto* returning_clause = arena_.allocate<ast::ASTNode>();
    new (returning_clause) ast::ASTNode(ast::NodeType::ReturningClause);
    returning_clause->node_id = next_node_id_++;
    
    // Parse output expressions
    ast::ASTNode* first_item = nullptr;
    ast::ASTNode* last_item = nullptr;
    
    while (current_token_) {
        ast::ASTNode* item = nullptr;
        
        if (current_token_->value == "*") {
            item = arena_.allocate<ast::ASTNode>();
            new (item) ast::ASTNode(ast::NodeType::Star);
            item->node_id = next_node_id_++;
            advance();
        } else {
            item = parse_expression(0);
        }
        
        if (!item) break;
        
        item->parent = returning_clause;
        if (!first_item) {
            first_item = item;
            returning_clause->first_child = first_item;
        } else {
            last_item->next_sibling = item;
        }
        last_item = item;
        returning_clause->child_count++;
        
        if (current_token_ && current_token_->value == ",") {
            advance();
        } else {
            break;
        }
    }

    // RETURNING requires at least one output expression or '*'; a dangling
    // RETURNING keyword previously yielded an empty ReturningClause and a clean
    // parse (the break-on-null empty-list family).
    if (returning_clause->child_count == 0) {
        error("expected an output expression or '*' after RETURNING");
        return nullptr;
    }

    return returning_clause;
}

ast::ASTNode* Parser::parse_on_conflict_clause() {
    // ON CONFLICT clause for UPSERT
    if (!current_token_ || current_token_->keyword_id != db25::Keyword::ON) {
        return nullptr;
    }
    
    size_t save_pos = tokenizer_ ? tokenizer_->position() : 0;
    const tokenizer::Token* save_token = current_token_;
    
    advance();  // consume ON
    
    if (!current_token_ || current_token_->keyword_id != db25::Keyword::CONFLICT) {
        // Not ON CONFLICT, restore
        current_token_ = save_token;
        if (tokenizer_) tokenizer_->set_position(save_pos);
        return nullptr;
    }
    advance();  // consume CONFLICT
    
    auto* on_conflict = arena_.allocate<ast::ASTNode>();
    new (on_conflict) ast::ASTNode(ast::NodeType::OnConflictClause);
    on_conflict->node_id = next_node_id_++;
    
    // Optional conflict target
    if (current_token_ && current_token_->value == "(") {
        advance();  // consume (
        parenthesis_depth_++;
        
        // Parse indexed columns or constraint name
        ast::ASTNode* first_col = nullptr;
        ast::ASTNode* last_col = nullptr;
        
        while (current_token_ && current_token_->value != ")") {
            if (current_token_->type == tokenizer::TokenType::Identifier) {
                auto* col = arena_.allocate<ast::ASTNode>();
                new (col) ast::ASTNode(ast::NodeType::Identifier);
                col->node_id = next_node_id_++;
                col->primary_text = copy_to_arena(current_token_->value);
                col->parent = on_conflict;
                
                if (!first_col) {
                    first_col = col;
                    on_conflict->first_child = first_col;
                } else {
                    last_col->next_sibling = col;
                }
                last_col = col;
                on_conflict->child_count++;
                
                advance();
                
                if (current_token_ && current_token_->value == ",") {
                    advance();
                }
            } else {
                // A non-identifier in the conflict target (an index expression
                // like `(a + b)` or `(lower(a))`) was silently SKIPPED, turning
                // one expression target into a fabricated list of separate
                // columns with the operator/parens dropped -- a wrong AST that
                // changes the statement's meaning. Reject the unsupported form.
                error("expected a column name in the ON CONFLICT conflict target");
                return nullptr;
            }
        }

        if (current_token_ && current_token_->value == ")") {
            advance();
            parenthesis_depth_--;
        }
    }
    
    // Conflict action
    if (current_token_ && current_token_->keyword_id == db25::Keyword::DO) {
        advance();  // consume DO
        
        if (current_token_ && current_token_->keyword_id == db25::Keyword::NOTHING) {
            advance();  // consume NOTHING
            on_conflict->semantic_flags |= 0x01;  // DO NOTHING flag
        } else if (current_token_ && current_token_->keyword_id == db25::Keyword::UPDATE) {
            advance();  // consume UPDATE
            on_conflict->semantic_flags |= 0x02;  // DO UPDATE flag

            // DO UPDATE requires a SET clause. `ON CONFLICT (a) DO UPDATE` with
            // no SET was previously accepted as a DO-UPDATE with no assignments.
            if (!current_token_ || current_token_->keyword_id != db25::Keyword::SET) {
                error("expected SET after ON CONFLICT DO UPDATE");
                return nullptr;
            }
            {
                advance();  // consume SET
                
                // Parse SET assignments
                auto* set_clause = arena_.allocate<ast::ASTNode>();
                new (set_clause) ast::ASTNode(ast::NodeType::SetClause);
                set_clause->node_id = next_node_id_++;
                set_clause->parent = on_conflict;
                
                if (on_conflict->first_child) {
                    // Add after conflict target
                    ast::ASTNode* last = on_conflict->first_child;
                    while (last->next_sibling) last = last->next_sibling;
                    last->next_sibling = set_clause;
                } else {
                    on_conflict->first_child = set_clause;
                }
                on_conflict->child_count++;
                
                // Parse assignments
                ast::ASTNode* first_assignment = nullptr;
                ast::ASTNode* last_assignment = nullptr;
                
                while (current_token_) {
                    if (current_token_->type != tokenizer::TokenType::Identifier) break;

                    auto* assignment = arena_.allocate<ast::ASTNode>();
                    new (assignment) ast::ASTNode(ast::NodeType::BinaryExpr);  // Using BinaryExpr for assignments
                    assignment->node_id = next_node_id_++;
                    assignment->primary_text = copy_to_arena(current_token_->value);
                    advance();

                    // A DO UPDATE SET assignment requires `column = value`. The
                    // '=' and its value were previously OPTIONAL, so `... DO
                    // UPDATE SET b` (no '=') and `... SET b WHERE ...` built a
                    // value-less assignment node and returned a clean parse.
                    // Require both, matching the standalone UPDATE SET path.
                    if (!current_token_ || current_token_->value != "=") {
                        error("expected '=' after the column name in ON CONFLICT "
                              "DO UPDATE SET");
                        return nullptr;
                    }
                    advance();  // consume '='
                    auto* expr = parse_expression(0);
                    if (!expr) {
                        error("expected a value expression after '=' in ON CONFLICT "
                              "DO UPDATE SET");
                        return nullptr;
                    }
                    expr->parent = assignment;
                    assignment->first_child = expr;
                    assignment->child_count = 1;

                    assignment->parent = set_clause;
                    if (!first_assignment) {
                        first_assignment = assignment;
                        set_clause->first_child = first_assignment;
                    } else {
                        last_assignment->next_sibling = assignment;
                    }
                    last_assignment = assignment;
                    set_clause->child_count++;

                    if (current_token_ && current_token_->value == ",") {
                        advance();
                    } else {
                        break;
                    }
                }

                // DO UPDATE SET must carry at least one assignment; a bare `DO
                // UPDATE SET` (nothing, or a non-identifier like WHERE next)
                // previously yielded an empty SetClause and a clean parse.
                if (set_clause->child_count == 0) {
                    error("expected at least one 'column = value' assignment in "
                          "ON CONFLICT DO UPDATE SET");
                    return nullptr;
                }
            }
        } else {
            // DO must be followed by NOTHING or UPDATE. `ON CONFLICT ... DO`
            // (EOF) or `DO <garbage>` previously left the conflict action unset
            // and returned a clean parse.
            error("expected NOTHING or UPDATE after ON CONFLICT DO");
            return nullptr;
        }
    }

    return on_conflict;
}

ast::ASTNode* Parser::parse_using_clause() {
    // USING clause for DELETE/UPDATE
    if (!current_token_ || current_token_->keyword_id != db25::Keyword::USING) {
        return nullptr;
    }
    advance();  // consume USING
    
    auto* using_clause = arena_.allocate<ast::ASTNode>();
    new (using_clause) ast::ASTNode(ast::NodeType::UsingClause);
    using_clause->node_id = next_node_id_++;
    
    // Parse table references
    ast::ASTNode* first_table = nullptr;
    ast::ASTNode* last_table = nullptr;
    
    while (current_token_) {
        auto* table = parse_table_reference();
        if (!table) break;
        
        // Check for joins
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
            auto kw = current_token_->keyword_id;
            if (kw == db25::Keyword::JOIN || kw == db25::Keyword::INNER ||
                kw == db25::Keyword::LEFT || kw == db25::Keyword::RIGHT ||
                kw == db25::Keyword::FULL || kw == db25::Keyword::CROSS) {
                // Parse join
                auto* join = parse_join_clause();
                if (join) {
                    // Make the table the left side of the join
                    table->parent = join;
                    join->first_child = table;
                    join->child_count = 1;
                    table = join;
                }
            }
        }
        
        table->parent = using_clause;
        if (!first_table) {
            first_table = table;
            using_clause->first_child = first_table;
        } else {
            last_table->next_sibling = table;
        }
        last_table = table;
        using_clause->child_count++;
        
        if (current_token_ && current_token_->value == ",") {
            advance();
        } else {
            break;
        }
    }
    
    return using_clause;
}

} // namespace db25::parser