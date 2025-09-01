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
        
        // Parse the CTE query (could be SELECT or WITH for nested CTEs)
        ast::ASTNode* cte_query = nullptr;
#ifdef DEBUG_CTE
        // std::cerr << "DEBUG: About to parse CTE query. Current token: ";
        if (current_token_) {
            std::cerr << current_token_->value << " type: " << static_cast<int>(current_token_->type);
        }
        std::cerr << std::endl;
#endif
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
            if (current_token_->keyword_id == db25::Keyword::SELECT) {
#ifdef DEBUG_CTE
                // std::cerr << "DEBUG: Parsing SELECT in CTE" << std::endl;
#endif
                cte_query = parse_select_stmt();
                // parse_select_stmt might return a UNION/INTERSECT/EXCEPT node
                // That's fine for CTEs, especially recursive ones
            } else if (current_token_->keyword_id == db25::Keyword::WITH) {
                cte_query = parse_with_statement();
            }
        }
        
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
    
    // Parse ORDER BY clause
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        (current_token_->keyword_id == db25::Keyword::ORDER)) {
        advance(); // consume ORDER
        if (current_token_ && current_token_->keyword_id == db25::Keyword::BY) {
            advance(); // consume BY
            
            auto* order_by = parse_order_by_clause();
            if (order_by) {
                order_by->parent = select_node;
                // Add to the end of child list
                auto* last_child = select_node->first_child;
                while (last_child->next_sibling) {
                    last_child = last_child->next_sibling;
                }
                last_child->next_sibling = order_by;
                select_node->child_count++;
            }
        }
    }
    
    // Parse LIMIT clause
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        (current_token_->keyword_id == db25::Keyword::LIMIT)) {
        advance(); // consume LIMIT
        
        auto* limit_clause = parse_limit_clause();
        if (limit_clause) {
            limit_clause->parent = select_node;
            // Add to the end of child list
            auto* last_child = select_node->first_child;
            while (last_child->next_sibling) {
                last_child = last_child->next_sibling;
            }
            last_child->next_sibling = limit_clause;
            select_node->child_count++;
        }
    }
    
    // Check for set operations (UNION, INTERSECT, EXCEPT)
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
        std::string_view keyword = current_token_->value;
        if (keyword == "UNION" || keyword == "union" ||
            keyword == "INTERSECT" || keyword == "intersect" ||
            keyword == "EXCEPT" || keyword == "except" ||
            keyword == "MINUS" || keyword == "minus") {
            
            // Create set operation node
            ast::NodeType set_op_type;
            if (keyword == "UNION" || keyword == "union") {
                set_op_type = ast::NodeType::UnionStmt;
            } else if (keyword == "INTERSECT" || keyword == "intersect") {
                set_op_type = ast::NodeType::IntersectStmt;
            } else {
                set_op_type = ast::NodeType::ExceptStmt;
            }
            
            auto* set_op_node = arena_.allocate<ast::ASTNode>();
            new (set_op_node) ast::ASTNode(set_op_type);
            set_op_node->node_id = next_node_id_++;
            set_op_node->primary_text = copy_to_arena(keyword);
            
            advance(); // consume set operation keyword
            
            // Check for ALL
            if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                (current_token_->keyword_id == db25::Keyword::ALL)) {
                set_op_node->flags = set_op_node->flags | ast::NodeFlags::All;
                advance();
            }
            
            // Parse right-hand SELECT
            ast::ASTNode* right_select = nullptr;
            if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                (current_token_->keyword_id == db25::Keyword::SELECT)) {
                right_select = parse_select_stmt();
            }
            
            if (right_select) {
                // Set up the tree: set_op_node has left (current select) and right as children
                select_node->parent = set_op_node;
                right_select->parent = set_op_node;
                set_op_node->first_child = select_node;
                select_node->next_sibling = right_select;
                set_op_node->child_count = 2;
                
                return set_op_node;
            }
        }
    }
    
    // Consume any trailing semicolon
    if (current_token_ && current_token_->type == tokenizer::TokenType::Delimiter &&
        current_token_->value == ";") {
        advance();
    }
    
    return select_node;
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
                           kw == "CROSS" || kw == "cross");
            
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
    
    // Handle JOIN type prefixes (LEFT, RIGHT, INNER, FULL, CROSS)
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword) {
        const auto& kw = current_token_->value;
        if (kw == "LEFT" || kw == "left" ||
            kw == "RIGHT" || kw == "right" ||
            kw == "INNER" || kw == "inner" ||
            kw == "FULL" || kw == "full" ||
            kw == "CROSS" || kw == "cross") {
            join_type = kw;
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
        // Could also have USING clause
        else if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                 current_token_->keyword_id == db25::Keyword::USING) {
            // Parse USING clause for JOIN
            advance(); // consume USING
            auto* using_clause = parse_using_clause();
            if (using_clause) {
                using_clause->parent = join_node;
                if (join_node->first_child) {
                    // Add as second child after the joined table
                    auto* first = join_node->first_child;
                    if (first->next_sibling) {
                        // Replace ON condition with USING
                        using_clause->next_sibling = first->next_sibling->next_sibling;
                        first->next_sibling = using_clause;
                    } else {
                        first->next_sibling = using_clause;
                    }
                } else {
                    join_node->first_child = using_clause;
                }
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
    
    // If we couldn't parse any item, return nullptr
    if (!first_item) {
        delete group_node;
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
    // Parse limit number
    if (!current_token_ || current_token_->type != tokenizer::TokenType::Number) {
        // No number after LIMIT
        return nullptr;
    }
    
    // Create LIMIT clause node since we have a number
    auto* limit_node = arena_.allocate<ast::ASTNode>();
    new (limit_node) ast::ASTNode(ast::NodeType::LimitClause);
    limit_node->node_id = next_node_id_++;
    
    // Create and add the limit number node
    auto* num_node = arena_.allocate<ast::ASTNode>();
    new (num_node) ast::ASTNode(ast::NodeType::IntegerLiteral);
    num_node->node_id = next_node_id_++;
    
    // Store the limit value
    num_node->primary_text = copy_to_arena(current_token_->value);
    
    num_node->parent = limit_node;
    limit_node->first_child = num_node;
    limit_node->child_count = 1;
    
    advance();
    
    // Check for OFFSET
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        current_token_->keyword_id == db25::Keyword::OFFSET) {
        advance();
        
        if (current_token_ && current_token_->type == tokenizer::TokenType::Number) {
            auto* offset_node = arena_.allocate<ast::ASTNode>();
            new (offset_node) ast::ASTNode(ast::NodeType::IntegerLiteral);
            offset_node->node_id = next_node_id_++;
            
            // Store the offset value
            offset_node->primary_text = copy_to_arena(current_token_->value);
            
            offset_node->parent = limit_node;
            num_node->next_sibling = offset_node;
            limit_node->child_count++;
            
            advance();
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
    if (!current_token_) {
        return nullptr;
    }
    
    // Parse simple table name
    if (current_token_->type == tokenizer::TokenType::Identifier) {
        auto* table_node = arena_.allocate<ast::ASTNode>();
        new (table_node) ast::ASTNode(ast::NodeType::TableRef);
        table_node->node_id = next_node_id_++;
        
        // Store table name
        table_node->primary_text = copy_to_arena(current_token_->value);
        
        advance();
        
        // Check for alias (AS keyword is optional)
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
            table_node->schema_name = copy_to_arena(current_token_->value);
            table_node->semantic_flags |= static_cast<uint16_t>(ast::NodeFlags::HasAlias);
            advance();
        }
        
        return table_node;
    }
    
    return nullptr;
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
    
    // Parse frame clause (ROWS/RANGE BETWEEN ... AND ...)
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        (current_token_->keyword_id == db25::Keyword::ROWS ||
         current_token_->keyword_id == db25::Keyword::RANGE)) {
        
        auto* frame_node = arena_.allocate<ast::ASTNode>();
        new (frame_node) ast::ASTNode(ast::NodeType::FrameClause);
        frame_node->node_id = next_node_id_++;
        frame_node->parent = window_spec;
        
        // Store frame type (ROWS or RANGE)
        frame_node->primary_text = copy_to_arena(current_token_->value);
        advance(); // consume ROWS/RANGE
        
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
                    frame_start->schema_name = copy_to_arena("PRECEDING");
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
                        frame_start->schema_name = copy_to_arena("PRECEDING");
                        advance();
                    } else if (current_token_->keyword_id == db25::Keyword::FOLLOWING) {
                        frame_start->schema_name = copy_to_arena("FOLLOWING");
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
                        frame_end->schema_name = copy_to_arena("FOLLOWING");
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
                            frame_end->schema_name = copy_to_arena("PRECEDING");
                            advance();
                        } else if (current_token_->keyword_id == db25::Keyword::FOLLOWING) {
                            frame_end->schema_name = copy_to_arena("FOLLOWING");
                            advance();
                        }
                    }
                }
                
                frame_start->next_sibling = frame_end;
                frame_node->child_count++;
            }
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

[[noreturn]] void Parser::error(const std::string& message) {
    // Since we have exceptions disabled, use abort for fatal errors
    // In production, this should set an error state instead
    (void)message;  // Suppress unused warning
    std::abort();   // Fatal error - parser is in invalid state
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
