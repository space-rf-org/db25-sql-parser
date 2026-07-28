/*
 * Copyright (c) 2024 DB25 Parser Project
 * 
 * SELECT Parser Module
 * Implements SELECT statement and all related clauses
 */

#include "parser_internal.hpp"
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include "db25/parser/tokenizer_adapter.hpp"

namespace db25::parser {

// This file contains SELECT statement parsing and all its clauses
// Functions are moved from parser.cpp to improve modularity
ast::ASTNode* Parser::parse_with_statement() {
    DepthGuard guard(this);  // Protect against deep recursion
    if (!guard.is_valid()) return nullptr;
    
    // Consume WITH keyword
    advance();
    
    // Create CTE clause node
    auto* cte_clause = arena_.allocate<ast::ASTNode>();
    new (cte_clause) ast::ASTNode(ast::NodeType::CTEClause);
    cte_clause->node_id = next_node_id_++;
    
    // Check for RECURSIVE
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        current_token_->keyword_id == db25::Keyword::RECURSIVE) {
        cte_clause->semantic_flags |= 0x100;  // Set RECURSIVE flag (bit 8)
        advance();
#ifdef DEBUG_CTE
        if (current_token_) {
            std::cerr << "After RECURSIVE, token: " << current_token_->value 
                      << " type: " << static_cast<int>(current_token_->type) << std::endl;
        }
#endif
    }
    
    // Parse CTE definitions
    ast::ASTNode* first_cte = nullptr;
    ast::ASTNode* last_cte = nullptr;
    
    do {
        // Parse CTE name
        if (!current_token_) {
            error("Unexpected end of input - expected CTE name after WITH [RECURSIVE]");
#ifdef DEBUG_CTE
            // No current token for CTE name
#endif
            return nullptr;
        }
        if (current_token_->type != tokenizer::TokenType::Identifier) {
            std::string err_msg = "Expected CTE name (identifier) after WITH [RECURSIVE], but got ";
            err_msg += "token type " + std::to_string(static_cast<int>(current_token_->type));
            err_msg += " with value '" + std::string(current_token_->value) + "'";
            error(err_msg);
#ifdef DEBUG_CTE
            // std::cerr << "DEBUG: Wrong token type for CTE name: " << static_cast<int>(current_token_->type) << std::endl;
#endif
            return nullptr;  // CTE name required
        }
        
        auto* cte_def = arena_.allocate<ast::ASTNode>();
        new (cte_def) ast::ASTNode(ast::NodeType::CTEDefinition);
        cte_def->node_id = next_node_id_++;
        cte_def->primary_text = copy_to_arena(current_token_->value);
#ifdef DEBUG_CTE
        // std::cerr << "DEBUG: Parsed CTE name: " << cte_def->primary_text << std::endl;
#endif
        advance();
        
        // Check for optional column list OR AS keyword
        // After CTE name, we can have:
        //   1. (column, list) AS (query)
        //   2. AS (query)
        
        if (current_token_ && current_token_->value == "(") {
#ifdef DEBUG_CTE
            // std::cerr << "DEBUG: Found '(' after CTE name" << std::endl;
#endif
            // Option 1: Column list
            advance();  // consume '('
            parenthesis_depth_++;
            
            // Check if this is a column list by looking for identifier
            if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
#ifdef DEBUG_CTE
                // std::cerr << "DEBUG: Parsing column list" << std::endl;
#endif
                // Parse column list
                auto* col_list = arena_.allocate<ast::ASTNode>();
                new (col_list) ast::ASTNode(ast::NodeType::ColumnList);
                col_list->node_id = next_node_id_++;
                col_list->parent = cte_def;
                
                ast::ASTNode* first_col = nullptr;
                ast::ASTNode* last_col = nullptr;
                
                // Parse column names
                do {
                    auto* col_node = arena_.allocate<ast::ASTNode>();
                    new (col_node) ast::ASTNode(ast::NodeType::Identifier);
                    col_node->node_id = next_node_id_++;
                    col_node->primary_text = copy_to_arena(current_token_->value);
                    col_node->parent = col_list;
                    
                    if (!first_col) {
                        first_col = col_node;
                        col_list->first_child = first_col;
                    } else {
                        last_col->next_sibling = col_node;
                    }
                    last_col = col_node;
                    col_list->child_count++;
                    
                    advance();
                    
                    // Check for comma
                    if (current_token_ && current_token_->value == ",") {
                        advance();
                    } else {
                        break;
                    }
                } while (current_token_ && current_token_->type == tokenizer::TokenType::Identifier);
                
                // Consume closing paren
                if (current_token_ && current_token_->value == ")") {
                    advance();  // consume ')'
                    parenthesis_depth_--;
                } else {
                    error("Expected ')' after column list");
                    return nullptr;
                }
                
                // Add column list to CTE definition
                cte_def->first_child = col_list;
                cte_def->child_count = 1;
                
                // Now expect AS keyword after column list
                if (!current_token_ || current_token_->type != tokenizer::TokenType::Keyword ||
                    (current_token_->value != "AS" && current_token_->value != "as")) {
                    error("Expected AS after column list in CTE definition");
                    return nullptr;
                }
                advance();  // consume AS
                
                // Expect '(' for the query
                if (!current_token_ || current_token_->value != "(") {
                    error("Expected '(' after AS in CTE definition");
                    return nullptr;
                }
                advance();  // consume '('
                parenthesis_depth_++;
            } else {
                // Not a column list - we have '(' but it's not followed by identifier
                // This '(' must be the start of the query itself (e.g., "(SELECT ...)")
                // We're already inside the paren
            }
        } else if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                   current_token_->keyword_id == db25::Keyword::AS) {
#ifdef DEBUG_CTE
            // std::cerr << "DEBUG: Found AS after CTE name" << std::endl;
#endif
            // Option 2: No column list, just AS
            advance();  // consume AS
            
            // Expect '(' for the query
            if (!current_token_ || current_token_->value != "(") {
                error("Expected '(' after AS in CTE definition");
                return nullptr;
            }
#ifdef DEBUG_CTE
            // std::cerr << "DEBUG: Found '(' after AS, consuming it" << std::endl;
#endif
            advance();  // consume '('
            parenthesis_depth_++;
            
        } else {
            error("Expected '(' or AS after CTE name");
#ifdef DEBUG_CTE
            if (current_token_) {
                // std::cerr << "DEBUG: Unexpected token after CTE name: " << current_token_->value 
                //           << " type: " << static_cast<int>(current_token_->type) << std::endl;
            }
#endif
            return nullptr;
        }
        
        // At this point, we should be inside the CTE query parentheses
        
        // The CTE body is any query expression - SELECT, a set operation, a
        // nested WITH, or a VALUES list (`WITH t AS (VALUES (1),(2))` /
        // `WITH t AS (VALUES (1) UNION SELECT 2)`). Dispatch through the shared
        // query-body parser so it matches every other query-block site; the
        // enclosing `(` was already consumed above and the `)` is consumed below.
        ast::ASTNode* cte_query = parse_query_body();
        
        if (cte_query) {
            cte_query->parent = cte_def;
            if (cte_def->first_child) {
                // Already has column list
                cte_def->first_child->next_sibling = cte_query;
                cte_def->child_count++;
            } else {
                cte_def->first_child = cte_query;
                cte_def->child_count = 1;
            }
        }
        
        // Consume closing paren
        if (current_token_ && current_token_->value == ")") {
            if (parenthesis_depth_ > 0) parenthesis_depth_--;
            advance();
        } else {
            // If we don't find a closing paren, the CTE query might have consumed too much
            // This can happen if parse_select_stmt handles UNION incorrectly in a CTE context
            // For now, we'll continue anyway to see what token we're at
#ifdef DEBUG_CTE
            if (current_token_) {
                std::cerr << "WARNING: No closing paren after CTE query. Current token: " 
                          << current_token_->value << std::endl;
            }
#endif
        }
        
        // Add CTE definition to list
        cte_def->parent = cte_clause;
        if (!first_cte) {
            first_cte = cte_def;
            cte_clause->first_child = first_cte;
        } else {
            last_cte->next_sibling = cte_def;
        }
        last_cte = cte_def;
        cte_clause->child_count++;
        
        // Check for comma (more CTEs)
        if (current_token_ && current_token_->value == ",") {
            advance();
        } else {
            break;
        }
    } while (true);
    
    // Parse the main statement (SELECT, INSERT, UPDATE, DELETE)
    ast::ASTNode* main_stmt = nullptr;
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
        auto keyword_id = current_token_->keyword_id;
        if (keyword_id == db25::Keyword::SELECT) {
            main_stmt = parse_select_stmt();
        } else if (keyword_id == db25::Keyword::INSERT) {
            main_stmt = parse_insert_stmt();
        } else if (keyword_id == db25::Keyword::UPDATE) {
            main_stmt = parse_update_stmt();
        } else if (keyword_id == db25::Keyword::DELETE) {
            main_stmt = parse_delete_stmt();
        }
    }
    
    if (!main_stmt) {
#ifdef DEBUG_CTE
        // std::cerr << "DEBUG: No main statement after CTE. Current token: ";
        if (current_token_) {
            std::cerr << current_token_->value << " type: " << static_cast<int>(current_token_->type);
        } else {
            std::cerr << "(null)";
        }
        std::cerr << std::endl;
#endif
        return nullptr;  // Main statement required after CTE
    }
    
    // Attach CTE clause to main statement
    cte_clause->next_sibling = main_stmt->first_child;
    main_stmt->first_child = cte_clause;
    cte_clause->parent = main_stmt;
    main_stmt->child_count++;
    
    return main_stmt;
}

ast::ASTNode* Parser::parse_select_stmt() {
    DepthGuard guard(this);  // Protect against deep recursion
    if (!guard.is_valid()) return nullptr;

    // Capture whether this invocation is the right-hand branch of an enclosing
    // set operation, then clear the flag so nested parses (subqueries, the SELECT
    // list, etc.) handle their own set operations normally.
    const bool is_setop_rhs = in_setop_rhs_;
    in_setop_rhs_ = false;

    // Prefetch tokens for SELECT statement parsing
    // SELECT statements have predictable structure: SELECT [DISTINCT] columns FROM ...
    if (tokenizer_) {
        const auto& tokens = tokenizer_->get_tokens();
        size_t pos = tokenizer_->position();
        
        // Prefetch next 5 tokens for SELECT/DISTINCT/column detection
        if (pos + 1 < tokens.size()) __builtin_prefetch(&tokens[pos + 1], 0, 3);
        if (pos + 2 < tokens.size()) __builtin_prefetch(&tokens[pos + 2], 0, 3);
        if (pos + 3 < tokens.size()) __builtin_prefetch(&tokens[pos + 3], 0, 2);
        if (pos + 4 < tokens.size()) __builtin_prefetch(&tokens[pos + 4], 0, 2);
        if (pos + 5 < tokens.size()) __builtin_prefetch(&tokens[pos + 5], 0, 1);
    }
    
    // Consume SELECT keyword
    advance();  // We already checked it's SELECT in parse_statement()
    
    // Create SELECT statement node
    auto* select_node = arena_.allocate<ast::ASTNode>();
    new (select_node) ast::ASTNode(ast::NodeType::SelectStmt);
    select_node->node_id = next_node_id_++;
    
    // Check for DISTINCT or ALL
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
        if (current_token_->keyword_id == db25::Keyword::DISTINCT) {
            select_node->semantic_flags |= static_cast<uint16_t>(ast::NodeFlags::Distinct);
            advance(); // consume DISTINCT
            // `DISTINCT ON (expr, ...)` (PostgreSQL) is not supported. Reject it
            // instead of falling through to the select-list parser, which read
            // `ON (a)` as a function call `ON(a)` occupying the first output slot
            // and consumed the intended first column as its alias - silently
            // dropping a projected column.
            if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                current_token_->keyword_id == db25::Keyword::ON) {
                error("DISTINCT ON is not supported");
                return nullptr;
            }
        } else if (current_token_->keyword_id == db25::Keyword::ALL) {
            select_node->semantic_flags |= static_cast<uint16_t>(ast::NodeFlags::All);
            advance(); // consume ALL
        }
    }
    
    // Parse SELECT list
    auto* select_list = parse_select_list();
    if (!select_list) {
        // SELECT list is required
        return nullptr;
    }
    
    select_list->parent = select_node;
    select_node->first_child = select_list;
    select_node->child_count = 1;
    
    // Parse FROM clause
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        current_token_->keyword_id == db25::Keyword::FROM) {
        advance(); // consume FROM
        
        auto* from_clause = parse_from_clause();
        if (from_clause) {
            from_clause->parent = select_node;
            // Add as sibling to select_list
            select_list->next_sibling = from_clause;
            select_node->child_count++;
        }
    }
    
    // Parse WHERE clause
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        current_token_->keyword_id == db25::Keyword::WHERE) {
        advance(); // consume WHERE
        
        auto* where_clause = parse_where_clause();
        if (!where_clause) {
            // WHERE keyword was present but condition failed to parse
            return nullptr;  // Fail the entire SELECT statement
        }
        
        where_clause->parent = select_node;
        // Add to the end of child list
        auto* last_child = select_node->first_child;
        while (last_child->next_sibling) {
            last_child = last_child->next_sibling;
        }
        last_child->next_sibling = where_clause;
        select_node->child_count++;
    }
    
    // Parse GROUP BY clause
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        (current_token_->keyword_id == db25::Keyword::GROUP)) {
        advance(); // consume GROUP
        if (current_token_ && current_token_->keyword_id == db25::Keyword::BY) {
            advance(); // consume BY
            
            auto* group_by = parse_group_by_clause();
            if (group_by) {
                group_by->parent = select_node;
                // Add to the end of child list
                auto* last_child = select_node->first_child;
                while (last_child->next_sibling) {
                    last_child = last_child->next_sibling;
                }
                last_child->next_sibling = group_by;
                select_node->child_count++;
            }
        }
    }
    
    // Parse HAVING clause
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        (current_token_->keyword_id == db25::Keyword::HAVING)) {
        advance(); // consume HAVING
        
        auto* having_clause = parse_having_clause();
        if (having_clause) {
            having_clause->parent = select_node;
            // Add to the end of child list
            auto* last_child = select_node->first_child;
            while (last_child->next_sibling) {
                last_child = last_child->next_sibling;
            }
            last_child->next_sibling = having_clause;
            select_node->child_count++;
        }
    }
    
    // A bare set-operation operand (the right branch, and the left before folding)
    // must NOT swallow a trailing ORDER BY / LIMIT: those bind to the whole set
    // operation and are parsed once, after folding (see fold_set_operations).
    // `SELECT 1 UNION SELECT 2 ORDER BY 1` previously let the right branch eat
    // `ORDER BY 1`, then failed validation. A parenthesized operand keeps its own
    // ORDER BY (it re-enters this function outside setop-rhs mode), matching SQL
    // scoping.
    if (!is_setop_rhs) {
        attach_trailing_order_limit(select_node);
    }

    // Set operations fold LEFT-associatively. If this invocation is itself the
    // right-hand branch of an enclosing set operation, return the bare branch and
    // let that caller's fold loop consume the operator, so the tree is built
    // left-deep rather than right-deep (which matters for EXCEPT / MINUS).
    if (is_setop_rhs) {
        return select_node;
    }

    // Otherwise this SELECT is the first operand of a (possibly empty) set-
    // operation chain: fold any tail onto it and bind a trailing ORDER BY / LIMIT
    // to the whole result.
    return fold_set_operations(select_node);
}

// Attach a trailing ORDER BY and/or LIMIT (in that order) to `target`. Shared by
// the plain-SELECT path and the set-operation path: a trailing ORDER BY / LIMIT
// after `A UNION B` applies to the WHOLE result, so it must attach to the set-op
// node, not to B. No-op when neither keyword is at the current position.
void Parser::attach_trailing_order_limit(ast::ASTNode* target) {
    const auto append = [&](ast::ASTNode* clause) {
        clause->parent = target;
        auto* last_child = target->first_child;
        while (last_child->next_sibling) {
            last_child = last_child->next_sibling;
        }
        last_child->next_sibling = clause;
        target->child_count++;
    };
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        (current_token_->keyword_id == db25::Keyword::ORDER)) {
        advance(); // consume ORDER
        if (current_token_ && current_token_->keyword_id == db25::Keyword::BY) {
            advance(); // consume BY
            if (auto* order_by = parse_order_by_clause()) {
                append(order_by);
            }
        }
    }
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        (current_token_->keyword_id == db25::Keyword::LIMIT)) {
        advance(); // consume LIMIT
        if (auto* limit_clause = parse_limit_clause()) {
            append(limit_clause);
        }
    }
}

// Does the parenthesized group whose CONTENT begins at token index `from`
// constitute a query body - a query expression that fills the group exactly?
//
//   query_expr := primary ( (UNION|INTERSECT|EXCEPT) [ALL|DISTINCT] primary )*
//   primary    := '(' query_expr ')' | (SELECT|VALUES|WITH) <body>
//
// This distinguishes a parenthesized query body - `(SELECT 1)`, `((SELECT 1))`,
// `((SELECT 1) UNION (SELECT 2))` - from a grouped SCALAR expression whose left
// operand merely opens with a subquery (`((SELECT 1) + 2)`), from an IN value
// list (`((SELECT 1),(SELECT 2))`), and from a parenthesized join group
// (`((SELECT 1) t JOIN u)`, `((((t...))))`). Used by the FROM / scalar / IN /
// EXISTS gates.
//
// A SINGLE left-to-right linear pass, iterative (no recursion, no depth cap): a
// deeply nested `((((SELECT 1))))` is classified in O(tokens), so the caller then
// descends the derived-table path ONCE rather than re-running this predicate at
// every nesting level. (The earlier recursive form re-scanned overlapping ranges
// AND false-negatived past a 256 recursion cap, driving the FROM classifier into
// per-level join-group recursion - an O(depth^3) parse-time DoS on legal input.)
bool Parser::paren_group_starts_query(std::size_t from) const {
    if (tokenizer_ == nullptr) {
        return false;
    }
    const auto& tokens = tokenizer_->get_tokens();
    const std::size_t n = tokens.size();
    if (from >= n) {
        return false;
    }
    const auto is_open = [&](std::size_t j) {
        return tokens[j].type == tokenizer::TokenType::Delimiter &&
               tokens[j].value == "(";
    };
    const auto is_close = [&](std::size_t j) {
        return tokens[j].type == tokenizer::TokenType::Delimiter &&
               tokens[j].value == ")";
    };
    const auto is_query_kw = [&](std::size_t j) {
        if (tokens[j].type != tokenizer::TokenType::Keyword) {
            return false;
        }
        const auto k = tokens[j].keyword_id;
        return k == db25::Keyword::SELECT || k == db25::Keyword::VALUES ||
               k == db25::Keyword::WITH;
    };
    const auto is_setop = [&](std::size_t j) {
        if (tokens[j].type != tokenizer::TokenType::Keyword) {
            return false;
        }
        const auto k = tokens[j].keyword_id;
        return k == db25::Keyword::UNION || k == db25::Keyword::INTERSECT ||
               k == db25::Keyword::EXCEPT;
    };

    // Fast path: content opening directly with a query keyword IS a query body -
    // nothing can precede a SELECT / VALUES / WITH inside its parens to make it a
    // mere operand (an unclosed one is still routed to the subquery parser, which
    // reports the missing ')').
    if (is_query_kw(from)) {
        return true;
    }

    // `depth` counts parenthesized-primary wrappers still open. A keyword primary's
    // OWN body parens are balanced locally (body_depth) and don't affect `depth`.
    std::size_t i = from;
    std::size_t depth = 0;
    bool expect_primary = true;
    bool saw_query = false;
    while (i < n) {
        if (expect_primary) {
            if (is_open(i)) {
                ++depth;  // open a nested parenthesized primary
                ++i;
                continue;
            }
            if (is_query_kw(i)) {
                // Keyword-rooted primary: skip its body until, at this level, a
                // set-op keyword or a ')' that belongs to an enclosing wrapper.
                saw_query = true;
                expect_primary = false;
                ++i;
                std::size_t body_depth = 0;
                while (i < n) {
                    if (is_open(i)) {
                        ++body_depth;
                        ++i;
                    } else if (is_close(i)) {
                        if (body_depth == 0) {
                            break;  // ')' of an enclosing wrapper
                        }
                        --body_depth;
                        ++i;
                    } else if (body_depth == 0 && is_setop(i)) {
                        break;  // set-op tail at this level
                    } else {
                        ++i;
                    }
                }
                continue;
            }
            return false;  // expected a primary, got a non-query token (identifier,
                           // literal, operator) - a join group / list / scalar expr
        }
        // After a primary: close wrappers, fold a set-op tail, or hit the boundary.
        if (is_close(i)) {
            if (depth == 0) {
                return true;  // the enclosing group's ')': the whole content parsed
                              // as a query expression
            }
            --depth;
            ++i;
            continue;
        }
        if (is_setop(i)) {
            ++i;
            if (i < n && tokens[i].type == tokenizer::TokenType::Keyword &&
                (tokens[i].keyword_id == db25::Keyword::ALL ||
                 tokens[i].keyword_id == db25::Keyword::DISTINCT)) {
                ++i;
            }
            expect_primary = true;
            continue;
        }
        return false;  // a trailing scalar operator (`(SELECT 1) + 2`) or a table
                       // ref (`(SELECT 1) t JOIN ...`): not a query body
    }
    // Ran to end of input with no disqualifying token: an UNCLOSED but query-shaped
    // group. Route it to the subquery parser (which reports the missing ')'),
    // matching the pre-existing lenient path.
    return saw_query;
}

bool Parser::at_query_block_start() const {
    if (!current_token_) {
        return false;
    }
    if (current_token_->type == tokenizer::TokenType::Keyword) {
        const auto k = current_token_->keyword_id;
        return k == db25::Keyword::SELECT || k == db25::Keyword::VALUES ||
               k == db25::Keyword::WITH;
    }
    // A leading '(' whose group is wholly a query expression is a query block too:
    // `((SELECT 1) UNION (SELECT 2))`. parse_query_body's '(' branch then parses it
    // via parse_parenthesized_query and folds the set-op tail. A group that only
    // opens with a subquery, `((SELECT 1) + 2)`, stays a grouped expression.
    if (current_token_->type == tokenizer::TokenType::Delimiter &&
        current_token_->value == "(" && tokenizer_ != nullptr) {
        return paren_group_starts_query(tokenizer_->position());
    }
    return false;
}

ast::ASTNode* Parser::parse_query_body() {
    if (!current_token_) {
        return nullptr;
    }
    // A query body is a complete, self-contained block: clear the set-op-RHS flag
    // so the body folds its own set-op tail / trailing ORDER BY / LIMIT (restored
    // for the caller). Every branch folds any set-operation tail, so a VALUES or
    // SELECT operand followed by `UNION ...` is never left unconsumed.
    const bool saved_rhs = in_setop_rhs_;
    in_setop_rhs_ = false;
    ast::ASTNode* result = nullptr;
    if (current_token_->type == tokenizer::TokenType::Keyword &&
        current_token_->keyword_id == db25::Keyword::WITH) {
        result = parse_with_statement();
    } else if (current_token_->type == tokenizer::TokenType::Keyword &&
               current_token_->keyword_id == db25::Keyword::VALUES) {
        if (ast::ASTNode* v = parse_values_stmt()) {
            result = fold_set_operations(v);
        }
    } else if (current_token_->type == tokenizer::TokenType::Delimiter &&
               current_token_->value == "(") {
        // A nested parenthesized query block: `((SELECT 1) UNION (SELECT 2))`.
        if (ast::ASTNode* nested = parse_parenthesized_query()) {
            result = fold_set_operations(nested);
        }
    } else if (current_token_->type == tokenizer::TokenType::Keyword &&
               current_token_->keyword_id == db25::Keyword::SELECT) {
        // parse_select_stmt folds its own set-op tail + trailing ORDER BY / LIMIT.
        result = parse_select_stmt();
    }
    in_setop_rhs_ = saved_rhs;
    return result;
}

ast::ASTNode* Parser::parse_parenthesized_query() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;

    parenthesis_depth_++;
    advance(); // consume '('

    // Every parenthesized query dispatches through the one shared query-body
    // parser, so a `( VALUES ... )`, `( VALUES (1) UNION SELECT 2 )`, or nested
    // block is handled identically to the statement / derived-table / CTE sites.
    ast::ASTNode* inner = parse_query_body();
    if (!inner) {
        // Not a query block. parse_select_stmt() would blindly consume the first
        // token as if it were SELECT (e.g. `(1 + 2)` -> `SELECT + 2`), a silent
        // mis-parse; reject instead.
        error("expected a query (SELECT, VALUES, WITH, or a parenthesized query) "
              "after '('");
        return nullptr;
    }

    if (current_token_ && current_token_->value == ")") {
        if (parenthesis_depth_ > 0) parenthesis_depth_--;
        advance(); // consume ')'
    } else {
        error("expected ')' after parenthesized set-operation operand");
        return nullptr;
    }
    return inner;
}

// Fold a set-operation tail onto an already-parsed first operand.
//
// Set operations have TWO precedence levels (SQL standard; matches
// Postgres/Oracle/SQL Server/DuckDB/SQLite): INTERSECT binds TIGHTER than
// UNION / EXCEPT / MINUS. So `A UNION B INTERSECT C` parses as
// `A UNION (B INTERSECT C)`. Within a single level operators fold LEFT-
// associatively (`A UNION B UNION C` -> `(A UNION B) UNION C`).
//
//   union_level    := intersect_level ( (UNION|EXCEPT|MINUS) [ALL] intersect_level )*
//   intersect_level:= <operand> ( INTERSECT [ALL] <operand> )*
//
// An operand is a bare SELECT with no set-op tail of its own (parsed under the
// in_setop_rhs_ guard so this loop owns the folding) OR a parenthesized query
// block `( <query> )` (which may itself be a set operation, and keeps its own
// ORDER BY / LIMIT). A trailing ORDER BY / LIMIT after the whole chain binds to
// the combined result and is attached here.
ast::ASTNode* Parser::fold_set_operations(ast::ASTNode* first_operand) {
    // Parse one operand: a parenthesized query block, or a bare SELECT branch.
    auto parse_setop_operand = [&]() -> ast::ASTNode* {
        if (current_token_ && current_token_->type == tokenizer::TokenType::Delimiter &&
            current_token_->value == "(") {
            return parse_parenthesized_query();
        }
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
            (current_token_->keyword_id == db25::Keyword::SELECT)) {
            in_setop_rhs_ = true;
            auto* branch = parse_select_stmt();
            in_setop_rhs_ = false;
            return branch;
        }
        // A bare VALUES arm: `SELECT 1 UNION VALUES (2)`. VALUES has no set-op
        // tail of its own, so it is parsed as a complete operand. Without this
        // the fold loop saw a non-`(`/SELECT operand, stopped, and silently
        // dropped the operator and the whole VALUES arm.
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
            (current_token_->keyword_id == db25::Keyword::VALUES)) {
            // Mark the operand as a set-op RHS so parse_values_stmt does not eat a
            // trailing ORDER BY / LIMIT that belongs to the whole set operation
            // (mirrors the SELECT arm above). Without this the clause was nested
            // under the VALUES arm instead of the combined node.
            in_setop_rhs_ = true;
            auto* branch = parse_values_stmt();
            in_setop_rhs_ = false;
            return branch;
        }
        return nullptr;
    };

    // Build a 2-child set-operation node of the given kind over left/right.
    auto make_setop_node = [&](std::string_view keyword, ast::NodeType type,
                               ast::ASTNode* left, ast::ASTNode* right,
                               bool all) -> ast::ASTNode* {
        auto* node = arena_.allocate<ast::ASTNode>();
        new (node) ast::ASTNode(type);
        node->node_id = next_node_id_++;
        node->primary_text = copy_to_arena(keyword);
        if (all) {
            node->flags = node->flags | ast::NodeFlags::All;
        }
        left->parent = node;
        right->parent = node;
        node->first_child = left;
        left->next_sibling = right;
        node->child_count = 2;
        return node;
    };

    // Higher-precedence level: fold a left-deep chain of INTERSECT onto `left`.
    auto fold_intersects = [&](ast::ASTNode* left) -> ast::ASTNode* {
        while (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
            std::string_view keyword = current_token_->value;
            // Match on the case-insensitive keyword_id, not the case-preserved
            // spelling: SQL keywords are case-insensitive, so `Intersect` must
            // fold exactly like INTERSECT / intersect (else the arm is dropped).
            if (current_token_->keyword_id != db25::Keyword::INTERSECT) {
                break;  // not INTERSECT: hand back to the union level
            }
            advance(); // consume INTERSECT
            // Consume the optional set-quantifier. ALL keeps duplicates;
            // DISTINCT is the default de-duplicating form and a no-op here.
            // Both must be consumed or parse_setop_operand() sees the keyword,
            // returns nullptr, and the RHS arm is silently dropped -- matching
            // the ALL||DISTINCT the query-body classifier already accepts.
            bool all = false;
            if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                (current_token_->keyword_id == db25::Keyword::ALL ||
                 current_token_->keyword_id == db25::Keyword::DISTINCT)) {
                all = (current_token_->keyword_id == db25::Keyword::ALL);
                advance();
            }
            ast::ASTNode* right = parse_setop_operand();
            if (!right) {
                break;  // malformed RHS: keep the tree folded so far
            }
            left = make_setop_node(keyword, ast::NodeType::IntersectStmt, left, right, all);
        }
        return left;
    };

    // Lower-precedence level: fold UNION / EXCEPT / MINUS left-associatively,
    // each operand first extended by any INTERSECT chain so INTERSECT groups
    // before UNION / EXCEPT.
    ast::ASTNode* left = fold_intersects(first_operand);
    while (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
        std::string_view keyword = current_token_->value;
        ast::NodeType set_op_type;
        // Case-insensitive keyword_id match (see fold_intersects): a mixed-case
        // `Union` / `Except` is the same operator as its UPPER / lower spelling.
        // (MINUS is not a set-op keyword in this tokenizer, so it never reaches
        // this Keyword-typed loop; it was only ever handled here dead.)
        if (current_token_->keyword_id == db25::Keyword::UNION) {
            set_op_type = ast::NodeType::UnionStmt;
        } else if (current_token_->keyword_id == db25::Keyword::EXCEPT) {
            set_op_type = ast::NodeType::ExceptStmt;
        } else {
            break;  // not a union-level operator (INTERSECT already folded): done
        }

        advance(); // consume set operation keyword

        // Consume the optional set-quantifier. ALL keeps duplicates; DISTINCT
        // is the default de-duplicating form and a no-op here. Both must be
        // consumed or parse_setop_operand() sees the keyword, returns nullptr,
        // and the RHS arm (and every following operator) is silently dropped --
        // matching the ALL||DISTINCT the query-body classifier already accepts.
        bool all = false;
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
            (current_token_->keyword_id == db25::Keyword::ALL ||
             current_token_->keyword_id == db25::Keyword::DISTINCT)) {
            all = (current_token_->keyword_id == db25::Keyword::ALL);
            advance();
        }

        ast::ASTNode* right_operand = parse_setop_operand();
        if (!right_operand) {
            // Malformed right-hand side: keep the tree folded so far.
            break;
        }
        // The RHS binds any trailing INTERSECT chain before we fold at this level.
        ast::ASTNode* right = fold_intersects(right_operand);

        left = make_setop_node(keyword, set_op_type, left, right, all);
    }

    // A trailing ORDER BY / LIMIT after the whole chain binds to the combined
    // result. For a lone operand (no operator folded) the clauses were already
    // consumed by the caller, so this is a no-op there.
    attach_trailing_order_limit(left);

    // Consume any trailing semicolon
    if (current_token_ && current_token_->type == tokenizer::TokenType::Delimiter &&
        current_token_->value == ";") {
        advance();
    }

    return left;
}

// ========== SELECT Clause Parsers ==========

ast::ASTNode* Parser::parse_select_list() {
    push_context(ParseContext::SELECT_LIST);
    
    // First check if we immediately hit FROM or another clause keyword
    // This handles "SELECT FROM ..." which should be invalid
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
        const auto& kw = current_token_->value;
        if (current_token_->keyword_id == db25::Keyword::FROM ||
            current_token_->keyword_id == db25::Keyword::WHERE ||
            current_token_->keyword_id == db25::Keyword::GROUP ||
            current_token_->keyword_id == db25::Keyword::HAVING ||
            current_token_->keyword_id == db25::Keyword::ORDER ||
            current_token_->keyword_id == db25::Keyword::LIMIT ||
            kw == "UNION" || kw == "union" ||
            kw == "INTERSECT" || kw == "intersect" ||
            kw == "EXCEPT" || kw == "except") {
            // Empty SELECT list - return nullptr to indicate error
            pop_context();
            return nullptr;
        }
    }
    
    // First, check if we can parse at least one item
    ast::ASTNode* first_item = nullptr;
    
    // Check for * (star)
    if (current_token_ && current_token_->type == tokenizer::TokenType::Operator &&
        current_token_->value == "*") {
        first_item = arena_.allocate<ast::ASTNode>();
        new (first_item) ast::ASTNode(ast::NodeType::Star);
        first_item->node_id = next_node_id_++;
        advance();
    }
    // Parse any other select item (column, expression, function)
    else if (current_token_) {
        first_item = parse_select_item();
    }
    
    // If we couldn't parse any item, return nullptr
    if (!first_item) {
        pop_context();
        return nullptr;
    }
    
    // Now create the SELECT list node since we have at least one item
    auto* list_node = arena_.allocate<ast::ASTNode>();
    new (list_node) ast::ASTNode(ast::NodeType::SelectList);
    list_node->node_id = next_node_id_++;
    
    // Add the first item
    first_item->parent = list_node;
    list_node->first_child = first_item;
    list_node->child_count = 1;
    
    ast::ASTNode* last_item = first_item;
    
    // Parse remaining items
    while (current_token_ && current_token_->type == tokenizer::TokenType::Delimiter &&
           current_token_->value == ",") {
        advance(); // consume comma
        
        ast::ASTNode* item = nullptr;
        
        // Check for * (star)
        if (current_token_ && current_token_->type == tokenizer::TokenType::Operator &&
            current_token_->value == "*") {
            item = arena_.allocate<ast::ASTNode>();
            new (item) ast::ASTNode(ast::NodeType::Star);
            item->node_id = next_node_id_++;
            advance();
        }
        // Parse any other select item (column, expression, function)
        else if (current_token_) {
            item = parse_select_item();
        }
        
        if (item) {
            item->parent = list_node;
            last_item->next_sibling = item;
            last_item = item;
            list_node->child_count++;
        }
    }
    
    pop_context();
    return list_node;
}

ast::ASTNode* Parser::parse_from_clause() {
    push_context(ParseContext::FROM_CLAUSE);
    
    // Parse first table reference
    auto* table_ref = parse_table_reference();
    if (!table_ref) {
        // No table reference means no FROM clause
        pop_context();
        return nullptr;
    }
    
    // Now create FROM clause node since we have at least one table
    auto* from_node = arena_.allocate<ast::ASTNode>();
    new (from_node) ast::ASTNode(ast::NodeType::FromClause);
    from_node->node_id = next_node_id_++;
    
    table_ref->parent = from_node;
    from_node->first_child = table_ref;
    from_node->child_count = 1;
    ast::ASTNode* last_child = table_ref;
    
    // Check for comma-separated tables or JOINs
    while (current_token_) {
        // Handle comma-separated tables (old-style join)
        if (current_token_->type == tokenizer::TokenType::Delimiter &&
            current_token_->value == ",") {
            advance(); // consume comma

            if (auto* next_table = parse_table_reference()) {
                next_table->parent = from_node;
                // last_child is always valid (initialized to table_ref)
                last_child->next_sibling = next_table;
                last_child = next_table;
                from_node->child_count++;
            }
        }
        // Handle JOIN keywords
        else if (current_token_->type == tokenizer::TokenType::Keyword) {
            const auto& kw = current_token_->value;
            
            // Check for various JOIN types
            bool is_join = (kw == "JOIN" || kw == "join" ||
                           kw == "LEFT" || kw == "left" ||
                           kw == "RIGHT" || kw == "right" ||
                           kw == "INNER" || kw == "inner" ||
                           kw == "FULL" || kw == "full" ||
                           kw == "CROSS" || kw == "cross" ||
                           kw == "NATURAL" || kw == "natural");
            
            if (is_join) {
                auto* join_clause = parse_join_clause();
                // parse_join_clause always returns a valid node
                join_clause->parent = from_node;
                // last_child is always valid (initialized to table_ref)
                last_child->next_sibling = join_clause;
                last_child = join_clause;
                from_node->child_count++;
            } else {
                // Not a JOIN keyword, stop parsing FROM clause
                break;
            }
        }
        else {
            // No more tables or joins
            break;
        }
    }
    
    pop_context();
    return from_node;
}

ast::ASTNode* Parser::parse_join_clause() {
    // Create JOIN clause node
    auto* join_node = arena_.allocate<ast::ASTNode>();
    new (join_node) ast::ASTNode(ast::NodeType::JoinClause);
    join_node->node_id = next_node_id_++;
    
    // Store JOIN type (LEFT, RIGHT, INNER, etc.)
    std::string join_type;
    
    // Optional NATURAL prefix: NATURAL [INNER | LEFT [OUTER] | RIGHT [OUTER] |
    // FULL [OUTER]] JOIN. A NATURAL join carries no ON / USING clause; its join
    // columns are every column common to both inputs, resolved downstream.
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        current_token_->keyword_id == db25::Keyword::NATURAL) {
        join_type = "NATURAL";
        advance(); // consume NATURAL
    }

    // Handle JOIN type prefixes (LEFT, RIGHT, INNER, FULL, CROSS), which may
    // follow a NATURAL prefix (NATURAL LEFT JOIN).
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
        const auto& kw = current_token_->value;
        if (kw == "LEFT" || kw == "left" ||
            kw == "RIGHT" || kw == "right" ||
            kw == "INNER" || kw == "inner" ||
            kw == "FULL" || kw == "full" ||
            kw == "CROSS" || kw == "cross") {
            if (!join_type.empty()) {
                join_type += " ";
            }
            join_type += kw;
            advance(); // consume JOIN type

            // Also handle OUTER keyword (LEFT OUTER JOIN)
            if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                current_token_->keyword_id == db25::Keyword::OUTER) {
                join_type += " OUTER";
                advance();
            }
        }
    }
    
    // Consume JOIN keyword
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        current_token_->keyword_id == db25::Keyword::JOIN) {
        if (!join_type.empty()) {
            join_type += " ";
        }
        join_type += "JOIN";
        advance();
    }
    
    // Store join type in the node
    if (!join_type.empty()) {
        join_node->primary_text = copy_to_arena(join_type);
    }
    
    // Parse the joined table
    auto* table = parse_table_reference();
    if (table) {
        table->parent = join_node;
        join_node->first_child = table;
        join_node->child_count++;
        
        // Parse ON condition if present
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
            current_token_->keyword_id == db25::Keyword::ON) {
            advance(); // consume ON
            
            push_context(ParseContext::JOIN_CONDITION);
            // Parse the join condition as an expression
            auto* condition = parse_expression(0);
            pop_context();
            if (condition) {
                condition->parent = join_node;
                table->next_sibling = condition;
                join_node->child_count++;
            }
        }
        // Or a USING ( col [, col]* ) join column list. This is distinct from
        // the DELETE/UPDATE USING table list (parse_using_clause); here USING
        // takes a parenthesized list of shared column names.
        else if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                 current_token_->keyword_id == db25::Keyword::USING) {
            advance(); // consume USING
            if (current_token_ && current_token_->value == "(") {
                advance(); // consume '('
                parenthesis_depth_++;

                auto* using_clause = arena_.allocate<ast::ASTNode>();
                new (using_clause) ast::ASTNode(ast::NodeType::UsingClause);
                using_clause->node_id = next_node_id_++;
                using_clause->parent = join_node;

                ast::ASTNode* first_col = nullptr;
                ast::ASTNode* last_col = nullptr;
                while (current_token_ &&
                       current_token_->type == tokenizer::TokenType::Identifier) {
                    auto* col = arena_.allocate<ast::ASTNode>();
                    new (col) ast::ASTNode(ast::NodeType::ColumnRef);
                    col->node_id = next_node_id_++;
                    col->primary_text = copy_to_arena(current_token_->value);
                    col->parent = using_clause;
                    if (!first_col) {
                        first_col = col;
                        using_clause->first_child = col;
                    } else {
                        last_col->next_sibling = col;
                    }
                    last_col = col;
                    using_clause->child_count++;
                    advance();
                    if (current_token_ && current_token_->value == ",") {
                        advance();
                    } else {
                        break;
                    }
                }

                if (current_token_ && current_token_->value == ")") {
                    if (parenthesis_depth_ > 0) parenthesis_depth_--;
                    advance();
                }

                // Attach as the second child, after the joined table.
                table->next_sibling = using_clause;
                join_node->child_count++;
            }
        }
    }
    
    return join_node;
}

ast::ASTNode* Parser::parse_where_clause() {
    push_context(ParseContext::WHERE_CLAUSE);
    
    // Parse condition expression
    auto* condition = parse_expression(0);
    if (!condition) {
        // No condition means no WHERE clause
        pop_context();
        return nullptr;
    }
    
    // Create WHERE clause node since we have a condition
    auto* where_node = arena_.allocate<ast::ASTNode>();
    new (where_node) ast::ASTNode(ast::NodeType::WhereClause);
    where_node->node_id = next_node_id_++;
    
    condition->parent = where_node;
    where_node->first_child = condition;
    where_node->child_count = 1;
    
    pop_context();
    return where_node;
}

ast::ASTNode* Parser::parse_group_by_clause() {
    push_context(ParseContext::GROUP_BY_CLAUSE);
    
    // Create GROUP BY clause node
    auto* group_node = arena_.allocate<ast::ASTNode>();
    new (group_node) ast::ASTNode(ast::NodeType::GroupByClause);
    group_node->node_id = next_node_id_++;
    
    // Check for GROUPING SETS, CUBE, or ROLLUP
    if (current_token_ && (current_token_->value == "GROUPING" || current_token_->value == "grouping")) {  // TODO: Add GROUPING to keywords
        advance(); // consume GROUPING
        if (current_token_ && (current_token_->value == "SETS" || current_token_->value == "sets")) {  // TODO: Add SETS to keywords
            advance(); // consume SETS
            
            // Create GROUPING SETS node
            auto* grouping_sets = arena_.allocate<ast::ASTNode>();
            new (grouping_sets) ast::ASTNode(ast::NodeType::GroupingElement);
            grouping_sets->node_id = next_node_id_++;
            grouping_sets->primary_text = copy_to_arena("GROUPING_SETS");
            grouping_sets->parent = group_node;
            group_node->first_child = grouping_sets;
            group_node->child_count = 1;
            
            // Parse (set1, set2, ...)
            if (current_token_ && current_token_->value == "(") {
                advance(); // consume (
                parenthesis_depth_++;
                
                ast::ASTNode* first_set = nullptr;
                ast::ASTNode* last_set = nullptr;
                
                do {
                    // Each set can be () or (expr1, expr2, ...)
                    if (current_token_ && current_token_->value == "(") {
                        advance(); // consume inner (
                        parenthesis_depth_++;
                        
                        auto* set_node = arena_.allocate<ast::ASTNode>();
                        new (set_node) ast::ASTNode(ast::NodeType::ColumnList);
                        set_node->node_id = next_node_id_++;
                        set_node->parent = grouping_sets;
                        
                        // Parse expressions in this set
                        ast::ASTNode* first_expr = nullptr;
                        ast::ASTNode* last_expr = nullptr;
                        
                        while (current_token_ && current_token_->value != ")") {
                            auto* expr = parse_expression(0);
                            if (!expr) break;
                            
                            expr->parent = set_node;
                            if (!first_expr) {
                                first_expr = expr;
                                set_node->first_child = first_expr;
                            } else {
                                last_expr->next_sibling = expr;
                            }
                            last_expr = expr;
                            set_node->child_count++;
                            
                            if (current_token_ && current_token_->value == ",") {
                                advance(); // consume comma
                            } else {
                                break;
                            }
                        }
                        
                        if (current_token_ && current_token_->value == ")") {
                            advance(); // consume inner )
                            parenthesis_depth_--;
                        }
                        
                        if (!first_set) {
                            first_set = set_node;
                            grouping_sets->first_child = first_set;
                        } else {
                            last_set->next_sibling = set_node;
                        }
                        last_set = set_node;
                        grouping_sets->child_count++;
                    } else {
                        // Single expression (not in parentheses)
                        auto* expr = parse_expression(0);
                        if (expr) {
                            expr->parent = grouping_sets;
                            if (!first_set) {
                                first_set = expr;
                                grouping_sets->first_child = first_set;
                            } else {
                                last_set->next_sibling = expr;
                            }
                            last_set = expr;
                            grouping_sets->child_count++;
                        }
                    }
                    
                    if (current_token_ && current_token_->value == ",") {
                        advance(); // consume comma
                    } else {
                        break;
                    }
                } while (true);
                
                if (current_token_ && current_token_->value == ")") {
                    advance(); // consume outer )
                    parenthesis_depth_--;
                }
            }
            
            pop_context();
            return group_node;
        }
    } else if (current_token_ && (current_token_->value == "CUBE" || current_token_->value == "cube")) {  // TODO: Add CUBE to keywords
        advance(); // consume CUBE
        
        // Create CUBE node
        auto* cube = arena_.allocate<ast::ASTNode>();
        new (cube) ast::ASTNode(ast::NodeType::GroupingElement);
        cube->node_id = next_node_id_++;
        cube->primary_text = copy_to_arena("CUBE");
        cube->parent = group_node;
        group_node->first_child = cube;
        group_node->child_count = 1;
        
        // Parse (expr1, expr2, ...)
        if (current_token_ && current_token_->value == "(") {
            advance(); // consume (
            parenthesis_depth_++;
            
            ast::ASTNode* first_expr = nullptr;
            ast::ASTNode* last_expr = nullptr;
            
            while (current_token_ && current_token_->value != ")") {
                auto* expr = parse_expression(0);
                if (!expr) break;
                
                expr->parent = cube;
                if (!first_expr) {
                    first_expr = expr;
                    cube->first_child = first_expr;
                } else {
                    last_expr->next_sibling = expr;
                }
                last_expr = expr;
                cube->child_count++;
                
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
        
        pop_context();
        return group_node;
    } else if (current_token_ && (current_token_->value == "ROLLUP" || current_token_->value == "rollup")) {  // TODO: Add ROLLUP to keywords
        advance(); // consume ROLLUP
        
        // Create ROLLUP node
        auto* rollup = arena_.allocate<ast::ASTNode>();
        new (rollup) ast::ASTNode(ast::NodeType::GroupingElement);
        rollup->node_id = next_node_id_++;
        rollup->primary_text = copy_to_arena("ROLLUP");
        rollup->parent = group_node;
        group_node->first_child = rollup;
        group_node->child_count = 1;
        
        // Parse (expr1, expr2, ...)
        if (current_token_ && current_token_->value == "(") {
            advance(); // consume (
            parenthesis_depth_++;
            
            ast::ASTNode* first_expr = nullptr;
            ast::ASTNode* last_expr = nullptr;
            
            while (current_token_ && current_token_->value != ")") {
                auto* expr = parse_expression(0);
                if (!expr) break;
                
                expr->parent = rollup;
                if (!first_expr) {
                    first_expr = expr;
                    rollup->first_child = first_expr;
                } else {
                    last_expr->next_sibling = expr;
                }
                last_expr = expr;
                rollup->child_count++;
                
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
        
        pop_context();
        return group_node;
    }
    
    // Regular GROUP BY items
    // Parse first GROUP BY item
    ast::ASTNode* first_item = nullptr;
    
    // Could be a number (GROUP BY 1, 2), expression, or column
    if (current_token_ && current_token_->type == tokenizer::TokenType::Number) {
        // GROUP BY position
        first_item = arena_.allocate<ast::ASTNode>();
        new (first_item) ast::ASTNode(ast::NodeType::IntegerLiteral);
        first_item->node_id = next_node_id_++;
        first_item->primary_text = copy_to_arena(current_token_->value);
        advance();
    } else {
        // Parse as expression (handles columns, function calls, etc.)
        first_item = parse_expression(0);
    }
    
    // If we couldn't parse any item, return nullptr. group_node is arena-owned
    // (placement-new'd above), so it must NOT be deleted — the arena reclaims it
    // in bulk on reset. Calling global delete on arena memory is undefined
    // behavior; just abandon the node.
    if (!first_item) {
        pop_context();
        return nullptr;
    }
    
    // Add first item
    first_item->parent = group_node;
    group_node->first_child = first_item;
    group_node->child_count = 1;
    ast::ASTNode* last_item = first_item;
    
    // Parse additional comma-separated GROUP BY items
    while (current_token_ && current_token_->type == tokenizer::TokenType::Delimiter &&
           current_token_->value == ",") {
        advance(); // consume comma
        
        ast::ASTNode* group_item = nullptr;
        
        // Could be a number (GROUP BY 1, 2), expression, or column
        if (current_token_ && current_token_->type == tokenizer::TokenType::Number) {
            // GROUP BY position
            group_item = arena_.allocate<ast::ASTNode>();
            new (group_item) ast::ASTNode(ast::NodeType::IntegerLiteral);
            group_item->node_id = next_node_id_++;
            group_item->primary_text = copy_to_arena(current_token_->value);
            advance();
        } else {
            // Parse as expression (handles columns, function calls, etc.)
            group_item = parse_expression(0);
        }
        
        // Add item to GROUP BY clause
        if (group_item) {
            group_item->parent = group_node;
            last_item->next_sibling = group_item;
            last_item = group_item;
            group_node->child_count++;
        } else {
            // If we couldn't parse an item after comma, stop
            break;
        }
    }
    
    pop_context();
    return group_node;
}

ast::ASTNode* Parser::parse_having_clause() {
    push_context(ParseContext::HAVING_CLAUSE);
    
    // Parse condition expression (same as WHERE but can contain aggregates)
    auto* condition = parse_expression(0);
    if (!condition) {
        // No condition means no HAVING clause
        pop_context();
        return nullptr;
    }
    
    // Create HAVING clause node since we have a condition
    auto* having_node = arena_.allocate<ast::ASTNode>();
    new (having_node) ast::ASTNode(ast::NodeType::HavingClause);
    having_node->node_id = next_node_id_++;
    
    condition->parent = having_node;
    having_node->first_child = condition;
    having_node->child_count = 1;
    
    pop_context();
    return having_node;
}

ast::ASTNode* Parser::parse_order_by_clause() {
    push_context(ParseContext::ORDER_BY_CLAUSE);
    
    // Parse first ORDER BY item
    ast::ASTNode* first_item = parse_expression(0);
    
    if (!first_item) {
        // If we can't parse any expression, return nullptr
        pop_context();
        return nullptr;
    }
    
    // Parse ASC/DESC direction for first item (default is ASC)
    // We'll store the direction in the order_item's semantic_flags
    // Using bit 7 for DESC (0 = ASC, 1 = DESC)
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
        if (current_token_->keyword_id == db25::Keyword::DESC) {
            first_item->semantic_flags |= (1 << 7); // Set bit 7 for DESC
            advance();
        } else if (current_token_->keyword_id == db25::Keyword::ASC) {
            // ASC is default, bit 7 remains 0
            advance();
        }
    }
    
    // Parse NULLS FIRST/LAST
    // Using bit 5 for NULLS ordering (0 = default, 1 = explicit)
    // Using bit 4 for FIRST/LAST (0 = LAST, 1 = FIRST)
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        current_token_->keyword_id == db25::Keyword::NULLS) {
        advance(); // consume NULLS
        
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
            if (current_token_->keyword_id == db25::Keyword::FIRST) {
                first_item->semantic_flags |= (1 << 5); // Mark NULLS ordering as explicit
                first_item->semantic_flags |= (1 << 4); // Set FIRST
                advance();
            } else if (current_token_->keyword_id == db25::Keyword::LAST) {
                first_item->semantic_flags |= (1 << 5); // Mark NULLS ordering as explicit
                // bit 4 remains 0 for LAST
                advance();
            }
        }
    }
    
    // Now create ORDER BY clause node since we have at least one item
    auto* order_node = arena_.allocate<ast::ASTNode>();
    new (order_node) ast::ASTNode(ast::NodeType::OrderByClause);
    order_node->node_id = next_node_id_++;
    
    // Add first item
    first_item->parent = order_node;
    order_node->first_child = first_item;
    order_node->child_count = 1;
    ast::ASTNode* last_item = first_item;
    
    // Parse additional comma-separated ORDER BY items
    while (current_token_ && current_token_->type == tokenizer::TokenType::Delimiter &&
           current_token_->value == ",") {
        advance(); // consume comma
        
        // Parse expression
        ast::ASTNode* order_item = parse_expression(0);
        
        if (!order_item) {
            // If we can't parse an expression after comma, stop
            break;
        }
        
        // Parse ASC/DESC direction
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
            if (current_token_->keyword_id == db25::Keyword::DESC) {
                order_item->semantic_flags |= (1 << 7); // Set bit 7 for DESC
                advance();
            } else if (current_token_->keyword_id == db25::Keyword::ASC) {
                // ASC is default, bit 7 remains 0
                advance();
            }
        }
        
        // Parse NULLS FIRST/LAST
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
            current_token_->keyword_id == db25::Keyword::NULLS) {
            advance(); // consume NULLS
            
            if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
                if (current_token_->keyword_id == db25::Keyword::FIRST) {
                    order_item->semantic_flags |= (1 << 5); // Mark NULLS ordering as explicit
                    order_item->semantic_flags |= (1 << 4); // Set FIRST
                    advance();
                } else if (current_token_->keyword_id == db25::Keyword::LAST) {
                    order_item->semantic_flags |= (1 << 5); // Mark NULLS ordering as explicit
                    // bit 4 remains 0 for LAST
                    advance();
                }
            }
        }
        
        // Add item to ORDER BY clause
        order_item->parent = order_node;
        last_item->next_sibling = order_item;
        last_item = order_item;
        order_node->child_count++;
    }
    
    pop_context();
    return order_node;
}

ast::ASTNode* Parser::parse_limit_clause() {
    // Parse a LIMIT/OFFSET operand. The operand is a numeric literal that may
    // carry a leading unary minus (e.g. LIMIT -1); the minus is folded into the
    // literal so the (invalid) value is preserved in the AST for later checks.
    auto parse_operand = [this]() -> ast::ASTNode* {
        bool negative = false;
        if (current_token_ && current_token_->type == tokenizer::TokenType::Operator &&
            current_token_->value == "-") {
            negative = true;
            advance();  // consume -
        }

        if (!current_token_ || current_token_->type != tokenizer::TokenType::Number) {
            return nullptr;
        }

        std::string text;
        if (negative) {
            text = "-";
        }
        text += std::string(current_token_->value);

        auto* num_node = arena_.allocate<ast::ASTNode>();
        new (num_node) ast::ASTNode(internal::number_literal_type(text));
        num_node->node_id = next_node_id_++;
        num_node->primary_text = copy_to_arena(text);
        advance();
        return num_node;
    };

    // Parse limit operand
    auto* num_node = parse_operand();
    if (!num_node) {
        // No numeric operand after LIMIT
        return nullptr;
    }

    // Create LIMIT clause node since we have an operand
    auto* limit_node = arena_.allocate<ast::ASTNode>();
    new (limit_node) ast::ASTNode(ast::NodeType::LimitClause);
    limit_node->node_id = next_node_id_++;

    num_node->parent = limit_node;
    limit_node->first_child = num_node;
    limit_node->child_count = 1;

    // Check for OFFSET
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        current_token_->keyword_id == db25::Keyword::OFFSET) {
        advance();

        auto* offset_node = parse_operand();
        if (offset_node) {
            offset_node->parent = limit_node;
            num_node->next_sibling = offset_node;
            limit_node->child_count++;
        }
    }

    return limit_node;
}

ast::ASTNode* Parser::parse_identifier() {
    // Accept both identifiers and keywords as identifiers in expression context
    if (!current_token_ || 
        (current_token_->type != tokenizer::TokenType::Identifier &&
         current_token_->type != tokenizer::TokenType::Keyword)) {
        return nullptr;
    }
    
    auto* id_node = arena_.allocate<ast::ASTNode>();
    new (id_node) ast::ASTNode(ast::NodeType::Identifier);
    id_node->node_id = next_node_id_++;
    
    // Store identifier text
    id_node->primary_text = copy_to_arena(current_token_->value);
    
    // Store context hint in upper byte of semantic_flags
    id_node->semantic_flags |= (get_context_hint() << 8);
    
    advance();
    return id_node;
}

ast::ASTNode* Parser::parse_table_reference() {
    // A parenthesized join group `( a JOIN b )` recurses here via
    // parse_from_clause() (see the is_join_group branch below), so a `(`-heavy
    // FROM item like `FROM ((((...` would otherwise drive unbounded native
    // recursion and overflow the stack. Unlike parse_select_stmt, this function
    // and parse_from_clause carry no guard, so bound the recursion here (the one
    // re-entrant point of the FROM cycle). The guard must be a plain
    // function-scope local (not an if-init variable, which would be destroyed at
    // the end of the if and decrement the depth before the recursive body runs),
    // so the increment persists across the nested parse_from_clause() call.
    DepthGuard guard(this);
    if (!guard.is_valid()) {
        return nullptr;
    }
    if (!current_token_) {
        return nullptr;
    }

    ast::ASTNode* ref_node = nullptr;

    // A '(' in table-reference position introduces either a derived table
    // ( SELECT ... ) / ( WITH ... ), or a parenthesized join group such as
    // ( a JOIN b ON a.id = b.id ). Disambiguate on the following token.
    if (current_token_->type == tokenizer::TokenType::Delimiter &&
        current_token_->value == "(") {
        // A derived table's body is a query: it starts with a SELECT / VALUES /
        // WITH keyword, OR with a '(' that (past any nested parens) introduces
        // one - `((SELECT 1) UNION (SELECT 2)) t`. Scan past the outer '(' (at
        // position()) starting one token later. A parenthesized join group, by
        // contrast, begins with an identifier or a '(' that wraps a table ref.
        const bool is_derived_table =
            tokenizer_ != nullptr &&
            paren_group_starts_query(tokenizer_->position() + 1);
        // A parenthesized join group begins with something that itself begins a
        // table reference: an identifier (table name) or a nested '('.
        const bool is_join_group =
            !is_derived_table && peek_token_ &&
            (peek_token_->type == tokenizer::TokenType::Identifier ||
             (peek_token_->type == tokenizer::TokenType::Delimiter &&
              peek_token_->value == "("));

        if (is_derived_table) {
            advance();               // consume '('
            parenthesis_depth_++;

            auto* subquery_node = arena_.allocate<ast::ASTNode>();
            new (subquery_node) ast::ASTNode(ast::NodeType::Subquery);
            subquery_node->node_id = next_node_id_++;
            subquery_node->semantic_flags |= static_cast<uint16_t>(ast::NodeFlags::IsSubquery);

            push_context(ParseContext::SUBQUERY);
            // The derived-table body is any query expression: a SELECT, a set
            // operation, a WITH ... query, or a VALUES list - including a VALUES
            // set operation (`(VALUES (1) UNION SELECT 2) t`). Dispatch through
            // the shared query-body parser so the set-op tail is folded (a bare
            // parse_values_stmt left `UNION ...` unconsumed and dropped the item).
            ast::ASTNode* inner = parse_query_body();
            pop_context();
            if (inner) {
                inner->parent = subquery_node;
                subquery_node->first_child = inner;
                subquery_node->child_count = 1;
            }

            // Consume closing ')'
            if (current_token_ && current_token_->value == ")") {
                if (parenthesis_depth_ > 0) parenthesis_depth_--;
                advance();
            }

            ref_node = subquery_node;
        } else if (is_join_group) {
            // Parenthesized join group: ( <table-ref> [ <join> ]* ). Parse the
            // inner join tree by reusing the FROM-item / join chaining logic
            // (parse_from_clause -> parse_table_reference + parse_join_clause),
            // then consume the matching ')'. The resulting FromClause node
            // stands in as a single table reference so the caller can attach it
            // and continue parsing any trailing joins on the group.
            advance();               // consume '('
            parenthesis_depth_++;

            auto* group = parse_from_clause();

            // Consume closing ')'
            if (current_token_ && current_token_->value == ")") {
                if (parenthesis_depth_ > 0) parenthesis_depth_--;
                advance();
            }

            ref_node = group;
        }
        // Otherwise (a bare '(' that is neither): leave it unconsumed and fall
        // through to return nullptr, preserving the prior behaviour.
    }
    // Simple table name, optionally schema/catalog qualified: "schema.table"
    // (or "catalog.schema.table"). Read every dotted part, not just the first,
    // otherwise the qualifier and everything after it (WHERE, etc.) is silently
    // dropped -- a data-loss bug for statements like DELETE.
    else if (current_token_->type == tokenizer::TokenType::Identifier) {
        auto* table_node = arena_.allocate<ast::ASTNode>();
        new (table_node) ast::ASTNode(ast::NodeType::TableRef);
        table_node->node_id = next_node_id_++;

        // Collect the dotted parts of the qualified table name. The tokenizer
        // may classify "." as Operator or Delimiter (see parse_column_ref).
        std::vector<std::string_view> parts;
        parts.push_back(current_token_->value);
        advance();
        while (current_token_ &&
               (current_token_->type == tokenizer::TokenType::Delimiter ||
                current_token_->type == tokenizer::TokenType::Operator) &&
               current_token_->value == "." &&
               peek_token_ &&
               (peek_token_->type == tokenizer::TokenType::Identifier ||
                peek_token_->type == tokenizer::TokenType::Keyword)) {
            advance();  // consume '.'
            parts.push_back(current_token_->value);
            advance();  // consume the identifier after the dot
        }

        // FIELD REPRESENTATION (must stay consistent with the semantic
        // analyzer, whose alias_of() reads schema_name as the ALIAS):
        //   - primary_text : the (unqualified) table name        -> last part
        //   - catalog_name : the schema/catalog qualifier(s)     -> earlier parts
        //   - schema_name  : RESERVED for the table alias, set below; NEVER the
        //                    schema qualifier (that would clobber the alias).
        // catalog_name is chosen because get_qualified_name() already emits it
        // ahead of the table name, and it does not collide with the alias slot.
        table_node->primary_text = copy_to_arena(parts.back());
        if (parts.size() > 1) {
            std::string qualifier;
            for (size_t i = 0; i + 1 < parts.size(); ++i) {
                if (i > 0) qualifier += '.';
                qualifier += parts[i];
            }
            table_node->catalog_name = copy_to_arena(qualifier);
            // has_catalog() keys off catalog_name being non-empty; no flag needed.
        }

        ref_node = table_node;
    }

    if (!ref_node) {
        return nullptr;
    }

    // Optional alias (AS keyword is optional), shared by table refs and
    // derived tables. Stored in schema_name (repurposed for alias).
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        current_token_->keyword_id == db25::Keyword::AS) {
        advance();
    }

    // Check for alias identifier
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier &&
        // Make sure it's not a keyword that could follow
        current_token_->value != "WHERE" && current_token_->value != "where" &&
        current_token_->value != "JOIN" && current_token_->value != "join" &&
        current_token_->value != "LEFT" && current_token_->value != "left" &&
        current_token_->value != "RIGHT" && current_token_->value != "right" &&
        current_token_->value != "INNER" && current_token_->value != "inner" &&
        current_token_->value != "FULL" && current_token_->value != "full" &&
        current_token_->value != "CROSS" && current_token_->value != "cross" &&
        current_token_->value != "ON" && current_token_->value != "on" &&
        current_token_->value != "GROUP" && current_token_->value != "group" &&
        current_token_->value != "ORDER" && current_token_->value != "order") {
        // Store alias in schema_name field (repurposed for alias)
        ref_node->schema_name = copy_to_arena(current_token_->value);
        ref_node->semantic_flags |= static_cast<uint16_t>(ast::NodeFlags::HasAlias);
        advance();
    }

    // Optional column-alias list for a DERIVED TABLE: "(SELECT ...) AS t (a, b)".
    // Standard SQL renames the relation's output columns. Without this the whole
    // "(...)" was left unconsumed: at statement level the trailing tokens were
    // silently tolerated (the aliases dropped), but inside an expression
    // subquery the stray '(' broke ')' matching and failed the parse. Capture it
    // as a ColumnList of Identifier nodes (the same shape a CTE column list
    // uses).
    //
    // Restricted to a subquery ref: parse_table_reference is shared with the
    // INSERT/UPDATE/DELETE target path, where "t (a, b)" is the INSERT target
    // column list (parsed by the DML parser), not an alias list. Base-table
    // column aliases ("FROM t x(a,b)") are rare and left as a follow-up. Guard
    // on the paren being followed by an identifier so a "(1, 2)" is never taken
    // as a column list.
    if (ref_node->node_type == ast::NodeType::Subquery &&
        current_token_ && current_token_->type == tokenizer::TokenType::Delimiter &&
        current_token_->value == "(" && peek_token_ &&
        peek_token_->type == tokenizer::TokenType::Identifier) {
        parenthesis_depth_++;
        advance();  // consume '('

        auto* col_list = arena_.allocate<ast::ASTNode>();
        new (col_list) ast::ASTNode(ast::NodeType::ColumnList);
        col_list->node_id = next_node_id_++;

        ast::ASTNode* last_col = nullptr;
        while (current_token_ &&
               current_token_->type == tokenizer::TokenType::Identifier) {
            auto* id = arena_.allocate<ast::ASTNode>();
            new (id) ast::ASTNode(ast::NodeType::Identifier);
            id->node_id = next_node_id_++;
            id->primary_text = copy_to_arena(current_token_->value);
            id->parent = col_list;
            if (last_col) {
                last_col->next_sibling = id;
            } else {
                col_list->first_child = id;
            }
            last_col = id;
            col_list->child_count++;
            advance();  // consume the identifier
            if (current_token_ && current_token_->value == ",") {
                advance();  // consume ',' and read the next column name
                continue;
            }
            break;
        }

        if (current_token_ && current_token_->value == ")") {
            if (parenthesis_depth_ > 0) parenthesis_depth_--;
            advance();  // consume ')'
        }

        // Attach as the LAST child so consumers that read the derived table's
        // inner query block (via find_child / first_child) are unaffected.
        col_list->parent = ref_node;
        if (ref_node->first_child) {
            ast::ASTNode* c = ref_node->first_child;
            while (c->next_sibling) {
                c = c->next_sibling;
            }
            c->next_sibling = col_list;
        } else {
            ref_node->first_child = col_list;
        }
        ref_node->child_count++;
    }

    return ref_node;
}

// ========== Column Reference Parser ==========

ast::ASTNode* Parser::parse_column_ref() {
    // Parse column references: "id", "users.id", "schema.table.column"
    // Accepts both identifiers and keywords as column names
    
    if (!current_token_ || 
        (current_token_->type != tokenizer::TokenType::Identifier &&
         current_token_->type != tokenizer::TokenType::Keyword)) {
        return nullptr;
    }
    
    // Collect all parts of the qualified name
    std::vector<std::string_view> parts;
    parts.push_back(current_token_->value);
    advance();
    
    // Check for qualified name parts
    // Note: The tokenizer might classify "." as Operator, not Delimiter
    while (current_token_ &&
           (current_token_->type == tokenizer::TokenType::Delimiter ||
            current_token_->type == tokenizer::TokenType::Operator) &&
           current_token_->value == ".") {
        advance(); // consume dot

        // Qualified star: "<qualifier>.*" (e.g. "t.*" or "schema.table.*").
        // Produce a Star node whose schema_name holds the dotted qualifier so
        // the downstream semantic analyzer can expand it, and consume the '*'
        // so the remainder of the statement (FROM, etc.) parses normally.
        if (current_token_ &&
            current_token_->type == tokenizer::TokenType::Operator &&
            current_token_->value == "*") {
            advance(); // consume '*'
            auto* star = arena_.allocate<ast::ASTNode>();
            new (star) ast::ASTNode(ast::NodeType::Star);
            star->node_id = next_node_id_++;

            std::string qualifier;
            for (size_t i = 0; i < parts.size(); i++) {
                if (i > 0) {
                    qualifier += '.';
                }
                qualifier += parts[i];
            }
            star->schema_name = copy_to_arena(qualifier);
            star->primary_text = copy_to_arena("*");
            return star;
        }

        if (current_token_ &&
            (current_token_->type == tokenizer::TokenType::Identifier ||
             current_token_->type == tokenizer::TokenType::Keyword)) {
            // Allow keywords as column names in qualified references (e.g., h.level)
            parts.push_back(current_token_->value);
            advance();
        } else {
            break; // Invalid qualified name
        }
    }
    
    // Create appropriate node based on qualification
    auto* col_ref = arena_.allocate<ast::ASTNode>();
    new (col_ref) ast::ASTNode(ast::NodeType::ColumnRef);
    col_ref->node_id = next_node_id_++;
    
    // Build full qualified name
    std::string qualified_name;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) {
            qualified_name += '.';
        }
        qualified_name += parts[i];
    }
    
    col_ref->primary_text = copy_to_arena(qualified_name);
    return col_ref;
}

// ========== Function Call Parser ==========

ast::ASTNode* Parser::parse_function_call() {
    // Parse function calls: "COUNT(*)", "MAX(id)", "SUM(price * quantity)"
    // Accept both identifiers and keywords as function names
    
    if (!current_token_ || 
        (current_token_->type != tokenizer::TokenType::Identifier &&
         current_token_->type != tokenizer::TokenType::Keyword)) {
        return nullptr;
    }
    
    // Store function name
    std::string_view func_name = current_token_->value;
    advance();
    
    // Must be followed by '('
    if (!current_token_ || current_token_->type != tokenizer::TokenType::Delimiter ||
        current_token_->value != "(") {
        // Not a function call, backtrack would be needed
        // For now, return nullptr
        return nullptr;
    }
    
    // Create FunctionCall node
    auto* func_call = arena_.allocate<ast::ASTNode>();
    new (func_call) ast::ASTNode(ast::NodeType::FunctionCall);
    func_call->node_id = next_node_id_++;
    
    // Store function name
    func_call->primary_text = copy_to_arena(func_name);
    
    parenthesis_depth_++;
    advance(); // consume '('
    
    // Check for DISTINCT keyword (for aggregate functions)
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        (current_token_->keyword_id == db25::Keyword::DISTINCT)) {
        func_call->set_flag(ast::NodeFlags::Distinct);  // Use built-in method that sets the flag properly
        func_call->semantic_flags |= static_cast<uint16_t>(ast::NodeFlags::Distinct);  // Also set semantic_flags for compatibility
        advance(); // consume DISTINCT
    }
    
    // Parse arguments
    push_context(ParseContext::FUNCTION_ARG);
    ast::ASTNode* last_arg = nullptr;
    
    // Check for empty argument list
    if (current_token_ && current_token_->type == tokenizer::TokenType::Delimiter &&
        current_token_->value == ")") {
        // Empty argument list
        if (parenthesis_depth_ > 0) parenthesis_depth_--;
            advance(); // consume ')'
    } else {
        // Parse comma-separated arguments
        while (current_token_ && !(current_token_->type == tokenizer::TokenType::Delimiter &&
                                  current_token_->value == ")")) {
            ast::ASTNode* arg = nullptr;
            
            // Special case for COUNT(*)
            if (current_token_->type == tokenizer::TokenType::Operator &&
                current_token_->value == "*") {
                arg = arena_.allocate<ast::ASTNode>();
                new (arg) ast::ASTNode(ast::NodeType::Star);
                arg->node_id = next_node_id_++;
                advance();
            } else {
                // Parse as expression
                arg = parse_expression(0);
            }
            
            if (arg) {
                arg->parent = func_call;
                if (!func_call->first_child) {
                    func_call->first_child = arg;
                } else if (last_arg) {
                    last_arg->next_sibling = arg;
                }
                last_arg = arg;
                func_call->child_count++;
            }
            
            // Check for comma
            if (current_token_ && current_token_->type == tokenizer::TokenType::Delimiter &&
                current_token_->value == ",") {
                advance(); // consume comma
            } else {
                // End of arguments (closing paren) or error
                break;
            }
        }
        
        if (current_token_ && current_token_->value == ")") {
            if (parenthesis_depth_ > 0) parenthesis_depth_--;
            advance(); // consume ')'
        }
    }
    pop_context();

    // Optional aggregate FILTER (WHERE predicate) clause, e.g.
    //   COUNT(*) FILTER (WHERE amount > 0)
    // Sits between the argument list and any OVER clause. The predicate is
    // wrapped in a WhereClause node attached as a child of the call, so the
    // binder can tell it apart from the aggregate's arguments.
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        current_token_->keyword_id == db25::Keyword::FILTER) {
        advance();  // consume FILTER
        if (current_token_ && current_token_->value == "(") {
            parenthesis_depth_++;
            advance();  // consume '('
            if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                current_token_->keyword_id == db25::Keyword::WHERE) {
                advance();  // consume WHERE
                ast::ASTNode* pred = parse_expression(0);
                if (pred) {
                    auto* where_node = arena_.allocate<ast::ASTNode>();
                    new (where_node) ast::ASTNode(ast::NodeType::WhereClause);
                    where_node->node_id = next_node_id_++;
                    where_node->primary_text = copy_to_arena("FILTER");
                    pred->parent = where_node;
                    where_node->first_child = pred;
                    where_node->child_count = 1;
                    // Attach the FILTER clause as the call's last child.
                    where_node->parent = func_call;
                    if (func_call->first_child) {
                        ast::ASTNode* last = func_call->first_child;
                        while (last->next_sibling) last = last->next_sibling;
                        last->next_sibling = where_node;
                    } else {
                        func_call->first_child = where_node;
                    }
                    func_call->child_count++;
                }
            }
            if (current_token_ && current_token_->value == ")") {
                if (parenthesis_depth_ > 0) parenthesis_depth_--;
                advance();  // consume ')'
            }
        }
    }

    // Check for OVER clause (window function)
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        (current_token_->keyword_id == db25::Keyword::OVER)) {
        advance(); // consume OVER
        
        // Parse window specification
        auto* window_spec = parse_window_spec();
        if (window_spec) {
            // Transform FunctionCall into WindowExpr
            // Note: We can't change node_type after construction, so we mark it as a window function
            // using semantic_flags instead
            func_call->semantic_flags |= (1 << 8);  // Bit 8 indicates window function
            
            // Add window specification as last child
            if (func_call->first_child) {
                ast::ASTNode* last = func_call->first_child;
                while (last->next_sibling) {
                    last = last->next_sibling;
                }
                last->next_sibling = window_spec;
            } else {
                func_call->first_child = window_spec;
            }
            window_spec->parent = func_call;
            func_call->child_count++;
        }
    }
    
    return func_call;
}

// ========== Window Specification Parser ==========

ast::ASTNode* Parser::parse_window_spec() {
    // Parse OVER (window_specification)
    // window_spec: ( [PARTITION BY expr, ...] [ORDER BY expr [ASC|DESC], ...] [frame_clause] )
    
    if (!current_token_ || current_token_->value != "(") {
        return nullptr;  // OVER must be followed by (
    }
    
    auto* window_spec = arena_.allocate<ast::ASTNode>();
    new (window_spec) ast::ASTNode(ast::NodeType::WindowSpec);
    window_spec->node_id = next_node_id_++;
    
    parenthesis_depth_++;
    advance(); // consume (
    
    ast::ASTNode* last_child = nullptr;
    
    // Check for empty window specification ()
    if (current_token_ && current_token_->value == ")") {
        if (parenthesis_depth_ > 0) parenthesis_depth_--;
        advance(); // consume )
        return window_spec;  // Empty window spec is valid
    }
    
    // Parse PARTITION BY clause
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        (current_token_->keyword_id == db25::Keyword::PARTITION)) {
        advance(); // consume PARTITION
        
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
            current_token_->keyword_id == db25::Keyword::BY) {
            advance(); // consume BY
            
            // Create PARTITION BY node
            auto* partition_node = arena_.allocate<ast::ASTNode>();
            new (partition_node) ast::ASTNode(ast::NodeType::PartitionByClause);
            partition_node->node_id = next_node_id_++;
            partition_node->parent = window_spec;
            
            // Parse partition expressions
            ast::ASTNode* last_partition_expr = nullptr;
            while (true) {
                auto* expr = parse_expression(0);
                if (!expr) break;
                
                expr->parent = partition_node;
                if (!partition_node->first_child) {
                    partition_node->first_child = expr;
                } else {
                    last_partition_expr->next_sibling = expr;
                }
                last_partition_expr = expr;
                partition_node->child_count++;
                
                // Check for comma
                if (current_token_ && current_token_->value == ",") {
                    advance(); // consume comma
                } else {
                    break;  // No more partition expressions
                }
            }
            
            // Add partition node to window spec
            if (!window_spec->first_child) {
                window_spec->first_child = partition_node;
            } else {
                last_child->next_sibling = partition_node;
            }
            last_child = partition_node;
            window_spec->child_count++;
        }
    }
    
    // Parse ORDER BY clause
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        (current_token_->keyword_id == db25::Keyword::ORDER)) {
        advance(); // consume ORDER
        
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
            current_token_->keyword_id == db25::Keyword::BY) {
            advance(); // consume BY
            
            // Use existing ORDER BY parser
            auto* order_node = parse_order_by_clause();
            if (order_node) {
                order_node->parent = window_spec;
                
                // Add order node to window spec
                if (!window_spec->first_child) {
                    window_spec->first_child = order_node;
                } else {
                    last_child->next_sibling = order_node;
                }
                last_child = order_node;
                window_spec->child_count++;
            }
        }
    }
    
    // Parse frame clause: ROWS/RANGE/GROUPS { BETWEEN <bound> AND <bound> |
    // <bound> }. A frame unit may be followed by BETWEEN (two bounds) or by a
    // single bound (shorthand for BETWEEN <bound> AND CURRENT ROW).
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        (current_token_->keyword_id == db25::Keyword::ROWS ||
         current_token_->keyword_id == db25::Keyword::RANGE ||
         current_token_->keyword_id == db25::Keyword::GROUPS)) {

        auto* frame_node = arena_.allocate<ast::ASTNode>();
        new (frame_node) ast::ASTNode(ast::NodeType::FrameClause);
        frame_node->node_id = next_node_id_++;
        frame_node->parent = window_spec;

        // Store frame type (ROWS or RANGE)
        frame_node->primary_text = copy_to_arena(current_token_->value);
        advance(); // consume ROWS/RANGE

        // Parse a single frame bound (UNBOUNDED PRECEDING/FOLLOWING, CURRENT
        // ROW, <n> PRECEDING/FOLLOWING, or an INTERVAL bound). Shared by the
        // single-bound frame form below; the BETWEEN form keeps its own inline
        // parsing unchanged.
        auto parse_frame_bound = [&]() -> ast::ASTNode* {
            auto* bound = arena_.allocate<ast::ASTNode>();
            new (bound) ast::ASTNode(ast::NodeType::FrameBound);
            bound->node_id = next_node_id_++;
            bound->parent = frame_node;

            if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                current_token_->keyword_id == db25::Keyword::UNBOUNDED) {
                bound->primary_text = copy_to_arena("UNBOUNDED");
                advance(); // consume UNBOUNDED
                if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                    current_token_->keyword_id == db25::Keyword::PRECEDING) {
                    bound->primary_text = copy_to_arena("UNBOUNDED PRECEDING");
                    bound->semantic_flags |= ast::FrameBoundPreceding;
                    advance(); // consume PRECEDING
                } else if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                           current_token_->keyword_id == db25::Keyword::FOLLOWING) {
                    bound->primary_text = copy_to_arena("UNBOUNDED FOLLOWING");
                    bound->semantic_flags |= ast::FrameBoundFollowing;
                    advance(); // consume FOLLOWING
                }
            } else if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                       current_token_->keyword_id == db25::Keyword::CURRENT) {
                advance(); // consume CURRENT
                if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                    current_token_->keyword_id == db25::Keyword::ROW) {
                    bound->primary_text = copy_to_arena("CURRENT ROW");
                    advance(); // consume ROW
                }
            } else if (current_token_) {
                if (current_token_->type == tokenizer::TokenType::Number) {
                    bound->primary_text = copy_to_arena(current_token_->value);
                    advance(); // consume number
                } else if (current_token_->value == "INTERVAL" || current_token_->value == "interval") {
                    advance(); // consume INTERVAL
                    if (current_token_ && current_token_->type == tokenizer::TokenType::String) {
                        std::string interval_expr = "INTERVAL ";
                        interval_expr += std::string(current_token_->value);
                        bound->primary_text = copy_to_arena(interval_expr);
                        advance();
                    }
                    if (current_token_ && (current_token_->type == tokenizer::TokenType::Keyword ||
                                           current_token_->type == tokenizer::TokenType::Identifier)) {
                        std::string interval_text = bound->primary_text.empty() ? "INTERVAL" : std::string(bound->primary_text);
                        interval_text += " ";
                        interval_text += std::string(current_token_->value);
                        bound->primary_text = copy_to_arena(interval_text);
                        advance();
                    }
                }
                if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
                    if (current_token_->keyword_id == db25::Keyword::PRECEDING) {
                        bound->primary_text =
                            copy_to_arena(std::string(bound->primary_text) + " PRECEDING");
                        bound->semantic_flags |= ast::FrameBoundPreceding;
                        advance();
                    } else if (current_token_->keyword_id == db25::Keyword::FOLLOWING) {
                        bound->primary_text =
                            copy_to_arena(std::string(bound->primary_text) + " FOLLOWING");
                        bound->semantic_flags |= ast::FrameBoundFollowing;
                        advance();
                    }
                }
            }
            return bound;
        };

        // Parse BETWEEN clause
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
            current_token_->keyword_id == db25::Keyword::BETWEEN) {
            advance(); // consume BETWEEN
            
            // Parse frame start (UNBOUNDED PRECEDING, CURRENT ROW, N PRECEDING)
            auto* frame_start = arena_.allocate<ast::ASTNode>();
            new (frame_start) ast::ASTNode(ast::NodeType::FrameBound);
            frame_start->node_id = next_node_id_++;
            frame_start->parent = frame_node;
            
            if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                current_token_->keyword_id == db25::Keyword::UNBOUNDED) {
                frame_start->primary_text = copy_to_arena("UNBOUNDED");
                advance(); // consume UNBOUNDED

                if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                    current_token_->keyword_id == db25::Keyword::PRECEDING) {
                    frame_start->primary_text = copy_to_arena("UNBOUNDED PRECEDING");
                    frame_start->semantic_flags |= ast::FrameBoundPreceding;
                    advance(); // consume PRECEDING
                }
            } else if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                       (current_token_->keyword_id == db25::Keyword::CURRENT)) {
                advance(); // consume CURRENT
                if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                    current_token_->keyword_id == db25::Keyword::ROW) {
                    frame_start->primary_text = copy_to_arena("CURRENT ROW");
                    advance(); // consume ROW
                }
            } else if (current_token_) {
                // Could be N PRECEDING/FOLLOWING or INTERVAL expression
                if (current_token_->type == tokenizer::TokenType::Number) {
                    // Simple numeric bound
                    frame_start->primary_text = copy_to_arena(current_token_->value);
                    advance(); // consume number
                } else if (current_token_->value == "INTERVAL" || current_token_->value == "interval") {
                    // INTERVAL expression
                    advance(); // consume INTERVAL
                    
                    // Parse interval value (e.g., '1')
                    if (current_token_ && current_token_->type == tokenizer::TokenType::String) {
                        std::string interval_expr = "INTERVAL ";
                        interval_expr += std::string(current_token_->value);
                        frame_start->primary_text = copy_to_arena(interval_expr);
                        advance();
                    }
                    
                    // Parse interval unit (e.g., DAY, MONTH, YEAR)
                    // These might be identifiers, not keywords
                    if (current_token_ && (current_token_->type == tokenizer::TokenType::Keyword || 
                                          current_token_->type == tokenizer::TokenType::Identifier)) {
                        std::string interval_text = frame_start->primary_text.empty() ? "INTERVAL" : std::string(frame_start->primary_text);
                        interval_text += " ";
                        interval_text += std::string(current_token_->value);
                        frame_start->primary_text = copy_to_arena(interval_text);
                        advance();
                    }
                }
                
                // Parse PRECEDING/FOLLOWING
                if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
                    if (current_token_->keyword_id == db25::Keyword::PRECEDING) {
                        frame_start->primary_text =
                            copy_to_arena(std::string(frame_start->primary_text) + " PRECEDING");
                        frame_start->semantic_flags |= ast::FrameBoundPreceding;
                        advance();
                    } else if (current_token_->keyword_id == db25::Keyword::FOLLOWING) {
                        frame_start->primary_text =
                            copy_to_arena(std::string(frame_start->primary_text) + " FOLLOWING");
                        frame_start->semantic_flags |= ast::FrameBoundFollowing;
                        advance();
                    }
                }
            }
            
            frame_node->first_child = frame_start;
            frame_node->child_count = 1;
            
            // Parse AND
            if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                current_token_->keyword_id == db25::Keyword::AND) {
                advance(); // consume AND
                
                // Parse frame end
                auto* frame_end = arena_.allocate<ast::ASTNode>();
                new (frame_end) ast::ASTNode(ast::NodeType::FrameBound);
                frame_end->node_id = next_node_id_++;
                frame_end->parent = frame_node;
                
                if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                    current_token_->keyword_id == db25::Keyword::UNBOUNDED) {
                    frame_end->primary_text = copy_to_arena("UNBOUNDED");
                    advance(); // consume UNBOUNDED

                    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                        current_token_->keyword_id == db25::Keyword::FOLLOWING) {
                        frame_end->primary_text = copy_to_arena("UNBOUNDED FOLLOWING");
                        frame_end->semantic_flags |= ast::FrameBoundFollowing;
                        advance(); // consume FOLLOWING
                    }
                } else if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                           (current_token_->keyword_id == db25::Keyword::CURRENT)) {
                    advance(); // consume CURRENT
                    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                        current_token_->keyword_id == db25::Keyword::ROW) {
                        frame_end->primary_text = copy_to_arena("CURRENT ROW");
                        advance(); // consume ROW
                    }
                } else if (current_token_) {
                    // Could be N PRECEDING/FOLLOWING or INTERVAL expression
                    if (current_token_->type == tokenizer::TokenType::Number) {
                        // Simple numeric bound
                        frame_end->primary_text = copy_to_arena(current_token_->value);
                        advance(); // consume number
                    } else if (current_token_->value == "INTERVAL" || current_token_->value == "interval") {
                        // INTERVAL expression
                        advance(); // consume INTERVAL
                        
                        // Parse interval value (e.g., '1')
                        if (current_token_ && current_token_->type == tokenizer::TokenType::String) {
                            std::string interval_expr = "INTERVAL ";
                            interval_expr += std::string(current_token_->value);
                            frame_end->primary_text = copy_to_arena(interval_expr);
                            advance();
                        }
                        
                        // Parse interval unit (e.g., DAY, MONTH, YEAR)
                        // These might be identifiers, not keywords
                        if (current_token_ && (current_token_->type == tokenizer::TokenType::Keyword || 
                                              current_token_->type == tokenizer::TokenType::Identifier)) {
                            std::string interval_text = frame_end->primary_text.empty() ? "INTERVAL" : std::string(frame_end->primary_text);
                            interval_text += " ";
                            interval_text += std::string(current_token_->value);
                            frame_end->primary_text = copy_to_arena(interval_text);
                            advance();
                        }
                    }
                    
                    // Parse PRECEDING/FOLLOWING
                    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
                        if (current_token_->keyword_id == db25::Keyword::PRECEDING) {
                            frame_end->primary_text =
                                copy_to_arena(std::string(frame_end->primary_text) + " PRECEDING");
                            frame_end->semantic_flags |= ast::FrameBoundPreceding;
                            advance();
                        } else if (current_token_->keyword_id == db25::Keyword::FOLLOWING) {
                            frame_end->primary_text =
                                copy_to_arena(std::string(frame_end->primary_text) + " FOLLOWING");
                            frame_end->semantic_flags |= ast::FrameBoundFollowing;
                            advance();
                        }
                    }
                }
                
                frame_start->next_sibling = frame_end;
                frame_node->child_count++;
            }
        } else {
            // Single-bound frame: ROWS/RANGE/GROUPS <bound> without BETWEEN,
            // e.g. ROWS UNBOUNDED PRECEDING, ROWS 5 PRECEDING, RANGE CURRENT
            // ROW. Parse exactly one bound so the frame-bound tokens are
            // consumed and the closing ')' is reached (otherwise parenthesis
            // tracking is left elevated and the parse fails as "Unclosed
            // parenthesis").
            auto* frame_bound = parse_frame_bound();
            frame_node->first_child = frame_bound;
            frame_node->child_count = 1;
        }

        // Add frame node to window spec
        if (!window_spec->first_child) {
            window_spec->first_child = frame_node;
        } else {
            last_child->next_sibling = frame_node;
        }
        last_child = frame_node;
        window_spec->child_count++;
    }
    
    // Consume closing parenthesis
    if (current_token_ && current_token_->value == ")") {
        if (parenthesis_depth_ > 0) parenthesis_depth_--;
        advance(); // consume )
    }
    
    return window_spec;
}

// ========== SELECT Item Parser ==========

ast::ASTNode* Parser::parse_select_item() {
    // Parse a single item in SELECT list
    // Can be: *, table.*, column, expression, function call, with optional alias
    
    // First check if we've hit a clause keyword that ends the SELECT list
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
        const auto& kw = current_token_->value;
        if (current_token_->keyword_id == db25::Keyword::FROM ||
            current_token_->keyword_id == db25::Keyword::WHERE ||
            current_token_->keyword_id == db25::Keyword::GROUP ||
            current_token_->keyword_id == db25::Keyword::HAVING ||
            current_token_->keyword_id == db25::Keyword::ORDER ||
            current_token_->keyword_id == db25::Keyword::LIMIT ||
            kw == "UNION" || kw == "union" ||
            kw == "INTERSECT" || kw == "intersect" ||
            kw == "EXCEPT" || kw == "except") {
            // These keywords end the SELECT list
            return nullptr;
        }
    }
    
    ast::ASTNode* item = nullptr;
    
    // Check for * (star)
    if (current_token_ && current_token_->type == tokenizer::TokenType::Operator &&
        current_token_->value == "*") {
        auto* star = arena_.allocate<ast::ASTNode>();
        new (star) ast::ASTNode(ast::NodeType::Star);
        star->node_id = next_node_id_++;
        advance();
        item = star;
    } else {
        // Otherwise, parse as expression (which handles all other cases)
        // This includes qualified names like "u.name" and qualified wildcards like "table.*"
        item = parse_expression(0);
    }
    
    if (!item) return nullptr;
    
    // Check for alias (AS keyword or implicit)
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        current_token_->keyword_id == db25::Keyword::AS) {
        // Consume AS keyword
        advance();
        
        // Get the alias name - allow both identifiers and keywords as aliases
        if (current_token_ && 
            (current_token_->type == tokenizer::TokenType::Identifier ||
             current_token_->type == tokenizer::TokenType::Keyword)) {
            // Store alias in the item's schema_name field (repurposed for alias)
            item->schema_name = copy_to_arena(current_token_->value);
            item->semantic_flags |= static_cast<uint16_t>(ast::NodeFlags::HasAlias);
            advance();
        }
    }
    // Check for implicit alias (identifier after expression without AS)
    else if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier &&
             // Make sure it's not a keyword that could follow
             current_token_->value != "FROM" && current_token_->value != "from" &&
             current_token_->value != "WHERE" && current_token_->value != "where") {
        // Store alias
        item->schema_name = copy_to_arena(current_token_->value);
        item->semantic_flags |= static_cast<uint16_t>(ast::NodeFlags::HasAlias);
        advance();
    }
    
    return item;
}

// ========== Expression Parsers (implementations in parser_expressions.cpp) ==========

// Note: All expression parsing functions are now implemented in parser_expressions.cpp
// to reduce the size of this file and improve maintainability.

// parse_primary_expression() - Implemented in parser_expressions.cpp
// parse_expression() - Implemented in parser_expressions.cpp
// get_precedence() - Implemented in parser_expressions.cpp
// parse_case_expression() - Implemented in parser_expressions.cpp
// parse_cast_expression() - Implemented in parser_expressions.cpp
// parse_extract_expression() - Implemented in parser_expressions.cpp
// parse_primary() - Implemented in parser_expressions.cpp

// ========== Error Handling ==========

void Parser::error(const std::string& message) {
    // Exceptions are disabled (-fno-exceptions). Rather than std::abort()ing
    // the host process on malformed input (a denial-of-service hazard that
    // defeated the std::expected-based error API), record the first error and
    // enter a recoverable failed state. Callers already return nullptr after
    // calling error(), so this unwinds cleanly up to parse()/parse_script(),
    // which surface the recorded message as a ParseError.
    if (!has_error_) {
        has_error_ = true;
        error_message_ = message;
    }
}

void Parser::synchronize() {
    // Error recovery - skip to next statement boundary
    // This helps the parser recover from errors and continue parsing
    
    // Skip tokens until we find a statement boundary
    while (current_token_ && current_token_->type != tokenizer::TokenType::EndOfFile) {
        // Semicolon marks end of statement
        if (current_token_->type == tokenizer::TokenType::Delimiter && 
            current_token_->value == ";") {
            advance();
            return;
        }
        
        // Keywords that typically start new statements
        if (current_token_->type == tokenizer::TokenType::Keyword) {
            auto keyword_id = current_token_->keyword_id;
            if (keyword_id == db25::Keyword::SELECT ||
                keyword_id == db25::Keyword::INSERT ||
                keyword_id == db25::Keyword::UPDATE ||
                keyword_id == db25::Keyword::DELETE ||
                keyword_id == db25::Keyword::CREATE ||
                keyword_id == db25::Keyword::DROP ||
                keyword_id == db25::Keyword::ALTER ||
                keyword_id == db25::Keyword::BEGIN ||
                keyword_id == db25::Keyword::COMMIT ||
                keyword_id == db25::Keyword::ROLLBACK ||
                keyword_id == db25::Keyword::WITH) {
                // Found a statement boundary - don't consume it
                return;
            }
        }
        
        advance();
    }
}

// ========== Validation Methods ==========

bool Parser::validate_ast(ast::ASTNode* root) {
    if (!root) return false;
    
    // Dispatch based on statement type
    switch (root->node_type) {
        case ast::NodeType::SelectStmt:
            return validate_select_stmt(root);
        case ast::NodeType::UnionStmt:
        case ast::NodeType::IntersectStmt:
        case ast::NodeType::ExceptStmt:
            // For set operations, validate both sides
            if (root->first_child) {
                if (!validate_ast(root->first_child)) return false;
                if (root->first_child->next_sibling) {
                    return validate_ast(root->first_child->next_sibling);
                }
            }
            return true;
        case ast::NodeType::InsertStmt:
        case ast::NodeType::UpdateStmt:
        case ast::NodeType::DeleteStmt:
            // Basic validation for DML statements
            return true;  // TODO: Add specific validation
        default:
            return true;  // Unknown statement types pass for now
    }
}

bool Parser::validate_select_stmt(ast::ASTNode* select_stmt) {
    if (!select_stmt || select_stmt->node_type != ast::NodeType::SelectStmt) {
        return false;
    }
    
    // Check clause dependencies
    if (!validate_clause_dependencies(select_stmt)) {
        return false;
    }
    
    // Validate JOIN clauses if present
    const ast::ASTNode* child = select_stmt->first_child;
    while (child) {
        if (child->node_type == ast::NodeType::FromClause) {
            // Check JOINs within FROM clause
            ast::ASTNode* from_child = child->first_child;
            while (from_child) {
                if (from_child->node_type == ast::NodeType::JoinClause) {
                    if (!validate_join_clause(from_child)) {
                        return false;
                    }
                }
                from_child = from_child->next_sibling;
            }
        }
        child = child->next_sibling;
    }
    
    return true;
}

bool Parser::validate_clause_dependencies(ast::ASTNode* select_stmt) {
    bool has_from = false;
    bool has_where = false;
    bool has_group_by = false;
    bool has_having = false;
    bool has_order_by = false;
    
    // Scan all clauses
    ast::ASTNode* child = select_stmt->first_child;
    while (child) {
        switch (child->node_type) {
            case ast::NodeType::FromClause:
                has_from = true;
                break;
            case ast::NodeType::WhereClause:
                has_where = true;
                break;
            case ast::NodeType::GroupByClause:
                has_group_by = true;
                break;
            case ast::NodeType::HavingClause:
                has_having = true;
                break;
            case ast::NodeType::OrderByClause:
                has_order_by = true;
                break;
            default:
                break;
        }
        child = child->next_sibling;
    }
    
    // Validate dependencies
    // WHERE, GROUP BY, HAVING, ORDER BY all require FROM
    if ((has_where || has_group_by || has_having || has_order_by) && !has_from) {
        return false;  // These clauses require FROM
    }
    
    // HAVING without GROUP BY is semantically questionable but syntactically valid
    // Some databases allow it (treats whole result as one group)
    // So we don't enforce this dependency at parse time
    // if (has_having && !has_group_by) {
    //     return false;
    // }
    
    return true;
}

bool Parser::validate_join_clause(ast::ASTNode* join_clause) {
    if (!join_clause || join_clause->node_type != ast::NodeType::JoinClause) {
        return false;
    }
    
    // JOIN must have at least a table reference as first child
    if (!join_clause->first_child) {
        return false;  // JOIN without table is invalid
    }
    
    // First child should be a table reference or subquery
    const ast::ASTNode* table_ref = join_clause->first_child;
    if (table_ref->node_type != ast::NodeType::TableRef &&
        table_ref->node_type != ast::NodeType::Subquery) {
        return false;  // JOIN must specify a table or subquery
    }
    
    // If there's an ON clause, it should be the second child
    // (ON clause is optional for CROSS JOIN)
    if (join_clause->primary_text == "CROSS JOIN" || 
        join_clause->primary_text == "cross join") {
        return true;  // CROSS JOIN doesn't need ON clause
    }
    
    // Other JOINs should have ON clause (second child)
    if (!table_ref->next_sibling) {
        // Missing ON clause for non-CROSS JOIN
        // Allow it for now as some databases support NATURAL JOIN
        return true;
    }
    
    return true;
}


} // namespace db25::parser
