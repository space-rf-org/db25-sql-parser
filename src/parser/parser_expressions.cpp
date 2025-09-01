/*
 * Copyright (c) 2024 DB25 Parser Project
 * 
 * Expression Parser Module
 * Implements Pratt parser for SQL expressions with precedence climbing
 */

#include "parser_internal.hpp"
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include "db25/parser/tokenizer_adapter.hpp"

namespace db25::parser {

// ========== Primary Expression Parser ==========

ast::ASTNode* Parser::parse_primary_expression() {
    DepthGuard guard(this);  // Protect against deep recursion
    if (!guard.is_valid()) return nullptr;
    
    // Parse primary expressions: literals, identifiers, column refs, function calls
    // This is called by parse_expression for the base case
    
    if (!current_token_) {
        return nullptr;
    }
    
    // Handle CASE expressions
    if (current_token_->type == tokenizer::TokenType::Keyword &&
        (current_token_->keyword_id == db25::Keyword::KW_CASE)) {
        return parse_case_expression();
    }
    
    // Handle CAST expressions
    if (current_token_->type == tokenizer::TokenType::Keyword &&
        (current_token_->keyword_id == db25::Keyword::CAST)) {
        return parse_cast_expression();
    }
    
    // Handle EXTRACT expressions
    if (current_token_->type == tokenizer::TokenType::Keyword &&
        current_token_->keyword_id == db25::Keyword::EXTRACT) {
        return parse_extract_expression();
    }
    
    // Handle unary operators (NOT, EXISTS, -, +)
    if (current_token_->type == tokenizer::TokenType::Keyword) {
        if (current_token_->keyword_id == db25::Keyword::NOT) {
            // Check for NOT EXISTS
            advance(); // consume NOT
            
            if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                current_token_->keyword_id == db25::Keyword::EXISTS) {
                // NOT EXISTS
                auto* exists_node = arena_.allocate<ast::ASTNode>();
                new (exists_node) ast::ASTNode(ast::NodeType::UnaryExpr);
                exists_node->node_id = next_node_id_++;
                exists_node->primary_text = "EXISTS";
                exists_node->semantic_flags |= 0x40; // Set NOT flag
                
                advance(); // consume EXISTS
                
                // Parse subquery
                auto* operand = parse_primary_expression();
                if (operand) {
                    operand->parent = exists_node;
                    exists_node->first_child = operand;
                    exists_node->child_count = 1;
                }
                return exists_node;
            } else {
                // Regular NOT
                auto* not_node = arena_.allocate<ast::ASTNode>();
                new (not_node) ast::ASTNode(ast::NodeType::UnaryExpr);
                not_node->node_id = next_node_id_++;
                not_node->primary_text = "NOT";
                
                // Parse operand with higher precedence than NOT (3)
                auto* operand = parse_expression(3);
                if (operand) {
                    operand->parent = not_node;
                    not_node->first_child = operand;
                    not_node->child_count = 1;
                }
                return not_node;
            }
        } else if (current_token_->keyword_id == db25::Keyword::EXISTS) {
            // EXISTS without NOT
            auto* exists_node = arena_.allocate<ast::ASTNode>();
            new (exists_node) ast::ASTNode(ast::NodeType::UnaryExpr);
            exists_node->node_id = next_node_id_++;
            exists_node->primary_text = "EXISTS";
            
            advance(); // consume EXISTS
            
            // Parse subquery
            auto* operand = parse_primary_expression();
            if (operand) {
                operand->parent = exists_node;
                exists_node->first_child = operand;
                exists_node->child_count = 1;
            }
            return exists_node;
        }
    }
    
    // Handle unary - or +
    if (current_token_->type == tokenizer::TokenType::Operator &&
        (current_token_->value == "-" || current_token_->value == "+")) {
        // Save operator
        std::string_view op = current_token_->value;
        
        // Look ahead - if next is a number, we might combine them
        if (peek_token_ && peek_token_->type == tokenizer::TokenType::Number && op == "-") {
            // For negative numbers, just parse as number with minus
            advance(); // consume -
            auto* num = arena_.allocate<ast::ASTNode>();
            new (num) ast::ASTNode(ast::NodeType::IntegerLiteral);
            num->node_id = next_node_id_++;
            
            // Combine - with number
            std::string combined = "-";
            combined += std::string(current_token_->value);
            num->primary_text = copy_to_arena(combined);
            advance();
            return num;
        }
        
        // Otherwise, create unary expression
        auto* unary_node = arena_.allocate<ast::ASTNode>();
        new (unary_node) ast::ASTNode(ast::NodeType::UnaryExpr);
        unary_node->node_id = next_node_id_++;
        
        // Store operator
        unary_node->primary_text = copy_to_arena(op);
        
        advance(); // consume operator
        
        // Parse operand
        auto* operand = parse_primary_expression();
        if (operand) {
            operand->parent = unary_node;
            unary_node->first_child = operand;
            unary_node->child_count = 1;
        }
        return unary_node;
    }
    
    // Handle numbers
    if (current_token_->type == tokenizer::TokenType::Number) {
        auto* num = arena_.allocate<ast::ASTNode>();
        new (num) ast::ASTNode(ast::NodeType::IntegerLiteral);
        num->node_id = next_node_id_++;
        
        num->primary_text = copy_to_arena(current_token_->value);
        advance();
        return num;
    }
    
    // Handle strings
    if (current_token_->type == tokenizer::TokenType::String) {
        auto* str_node = arena_.allocate<ast::ASTNode>();
        new (str_node) ast::ASTNode(ast::NodeType::StringLiteral);
        str_node->node_id = next_node_id_++;
        
        str_node->primary_text = copy_to_arena(current_token_->value);
        advance();
        return str_node;
    }
    
    // Handle parentheses (could be subquery or grouped expression)
    if (current_token_->type == tokenizer::TokenType::Delimiter &&
        current_token_->value == "(") {
        parenthesis_depth_++;  // Track opening parenthesis
        advance(); // consume '('
        
        // Check if it's a SELECT subquery
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
            (current_token_->keyword_id == db25::Keyword::SELECT)) {
            // It's a subquery
            auto* subquery_node = arena_.allocate<ast::ASTNode>();
            new (subquery_node) ast::ASTNode(ast::NodeType::Subquery);
            subquery_node->node_id = next_node_id_++;
            
            push_context(ParseContext::SUBQUERY);
            // Parse the SELECT statement
            auto* select_stmt = parse_select_stmt();
            pop_context();
            if (select_stmt) {
                select_stmt->parent = subquery_node;
                subquery_node->first_child = select_stmt;
                subquery_node->child_count = 1;
            }
            
            // Consume closing ')'
            if (current_token_ && current_token_->value == ")") {
                if (parenthesis_depth_ > 0) parenthesis_depth_--;
                advance();
            }
            
            return subquery_node;
        } else {
            // It's a grouped expression
            auto* expr = parse_expression(0);
            
            // Consume closing ')'
            if (current_token_ && current_token_->value == ")") {
                if (parenthesis_depth_ > 0) parenthesis_depth_--;
                advance();
            }
            
            return expr;
        }
    }
    
    // Handle special keyword literals (TRUE, FALSE, NULL)
    if (current_token_->type == tokenizer::TokenType::Keyword) {
        const auto& kw = current_token_->value;
        if (kw == "TRUE" || kw == "true" || kw == "FALSE" || kw == "false") {
            auto* bool_node = arena_.allocate<ast::ASTNode>();
            new (bool_node) ast::ASTNode(ast::NodeType::BooleanLiteral);
            bool_node->node_id = next_node_id_++;
            bool_node->primary_text = copy_to_arena(current_token_->value);
            advance();
            return bool_node;
        } else if (kw == "NULL" || kw == "null") {
            auto* null_node = arena_.allocate<ast::ASTNode>();
            new (null_node) ast::ASTNode(ast::NodeType::NullLiteral);
            null_node->node_id = next_node_id_++;
            null_node->primary_text = copy_to_arena("NULL");
            advance();
            return null_node;
        }
    }
    
    // Handle identifiers (could be column, function, or simple identifier)
    // Also handle keywords that can be used as identifiers (e.g., date, time, level)
    if (current_token_->type == tokenizer::TokenType::Identifier ||
        current_token_->type == tokenizer::TokenType::Keyword) {
        // Look ahead using peek_token_
        if (peek_token_) {
            // Check for function call
            if (peek_token_->type == tokenizer::TokenType::Delimiter &&
                peek_token_->value == "(") {
                return parse_function_call();
            }
        }
        
        // Check for qualified name - ONLY call parse_column_ref if we see a dot
        if (peek_token_ && 
            (peek_token_->type == tokenizer::TokenType::Delimiter ||
             peek_token_->type == tokenizer::TokenType::Operator) &&
            peek_token_->value == ".") {
            return parse_column_ref();
        }
        
        // Create appropriate node based on context
        uint16_t context_hint = get_context_hint();
        bool is_column_context = (context_hint == static_cast<uint16_t>(ParseContext::SELECT_LIST) ||
                                  context_hint == static_cast<uint16_t>(ParseContext::WHERE_CLAUSE) ||
                                  context_hint == static_cast<uint16_t>(ParseContext::GROUP_BY_CLAUSE) ||
                                  context_hint == static_cast<uint16_t>(ParseContext::HAVING_CLAUSE) ||
                                  context_hint == static_cast<uint16_t>(ParseContext::ORDER_BY_CLAUSE) ||
                                  context_hint == static_cast<uint16_t>(ParseContext::JOIN_CONDITION));
        
        // Create appropriate node type based on context
        auto* node = arena_.allocate<ast::ASTNode>();
        if (is_column_context) {
            new (node) ast::ASTNode(ast::NodeType::ColumnRef);
        } else {
            new (node) ast::ASTNode(ast::NodeType::Identifier);
        }
        node->node_id = next_node_id_++;
        node->primary_text = copy_to_arena(current_token_->value);
        
        // Store context hint in upper byte of semantic_flags
        node->semantic_flags |= (context_hint << 8);
        
        advance();
        return node;
    }
    
    // Handle parenthesized expressions
    if (current_token_->type == tokenizer::TokenType::Delimiter &&
        current_token_->value == "(") {
        parenthesis_depth_++;
        advance(); // consume '('
        auto* expr = parse_expression(0);
        if (current_token_ && current_token_->value == ")") {
            if (parenthesis_depth_ > 0) parenthesis_depth_--;
            advance(); // consume ')'
        }
        return expr;
    }
    
    return nullptr;
}

// ========== Precedence Table ==========

int Parser::get_precedence() const {
    if (!current_token_) return 0;
    
    // Handle operators
    if (current_token_->type == tokenizer::TokenType::Operator) {
        const auto& op = current_token_->value;
        // Don't treat comma or dot as binary operators in expressions
        if (op == "," || op == ".") return 0;
        
        // Bitwise operators (lowest precedence after logical)
        if (op == "|") return 2;   // Bitwise OR (lower than concat)
        if (op == "&") return 2;   // Bitwise AND
        if (op == "^") return 2;   // Bitwise XOR
        if (op == "<<" || op == ">>") return 2;  // Bit shifts
        
        // String concatenation
        if (op == "||") return 3;  // String concatenation (lower than comparison)
        
        // Comparison operators
        if (op == "=") return 4;  // PREC_COMP
        if (op == "<" || op == ">") return 4;  // PREC_COMP
        if (op == "<=" || op == ">=") return 4;  // PREC_COMP
        if (op == "<>" || op == "!=") return 4;  // PREC_COMP
        
        // Arithmetic operators
        if (op == "+" || op == "-") return 5;  // PREC_TERM
        if (op == "*" || op == "/" || op == "%") return 6;  // PREC_FACTOR
        
        // Check for invalid operators (from other languages)
        if (op == "==" || op == "===" || op == "!==") {
            if (strict_mode_) {
                return -1;  // Invalid operator - will cause parse to fail
            }
            // In non-strict mode, treat as non-operator
            return 0;
        }
        
        // Unknown operator token - treat as non-operator
        return 0;
    }
    
    // Handle keywords that act as operators (only in WHERE/HAVING context)
    if (current_token_->type == tokenizer::TokenType::Keyword) {
        // SQL clause keywords should not be treated as operators
        if (current_token_->keyword_id == db25::Keyword::FROM ||
            current_token_->keyword_id == db25::Keyword::WHERE ||
            current_token_->keyword_id == db25::Keyword::ORDER ||
            current_token_->keyword_id == db25::Keyword::GROUP ||
            current_token_->keyword_id == db25::Keyword::HAVING ||
            current_token_->keyword_id == db25::Keyword::LIMIT ||
            current_token_->keyword_id == db25::Keyword::OFFSET) {
            return 0;  // Stop parsing expression
        }
        // Binary operators
        if (current_token_->keyword_id == db25::Keyword::OR) return 1;  // PREC_OR
        if (current_token_->keyword_id == db25::Keyword::AND) return 2;  // PREC_AND
        // SQL-specific operators (higher than AND but lower than comparison)
        if (current_token_->keyword_id == db25::Keyword::BETWEEN) return 3;  // PREC_BETWEEN
        if (current_token_->keyword_id == db25::Keyword::IN) return 3;  // PREC_IN
        if (current_token_->keyword_id == db25::Keyword::LIKE) return 3;  // PREC_LIKE
        if (current_token_->keyword_id == db25::Keyword::IS) return 3;  // PREC_IS (for IS NULL)
        // NOT can be binary when followed by LIKE/IN/BETWEEN
        if (current_token_->keyword_id == db25::Keyword::NOT) {
            // Look ahead to see if it's NOT LIKE/IN/BETWEEN
            if (peek_token_ && peek_token_->type == tokenizer::TokenType::Keyword) {
                if (peek_token_->keyword_id == db25::Keyword::LIKE ||
                    peek_token_->keyword_id == db25::Keyword::IN ||
                    peek_token_->keyword_id == db25::Keyword::BETWEEN) {
                    return 3;  // Same precedence as LIKE/IN/BETWEEN
                }
            }
            return 0;  // Otherwise NOT is not a binary operator here
        }
    }
    
    return 0;  // No precedence
}

// ========== Pratt Parser for Expressions ==========

ast::ASTNode* Parser::parse_expression(int min_precedence) {
    DepthGuard guard(this);  // Protect against deep recursion
    if (!guard.is_valid()) return nullptr;
    
    // Pratt parser with proper precedence handling
    
    // Prefetch upcoming tokens for expression parsing
    // Expression parsing often examines multiple tokens ahead
    if (tokenizer_) {
        const auto& tokens = tokenizer_->get_tokens();
        size_t pos = tokenizer_->position();
        
        // Prefetch next 4 tokens for operator precedence checking
        if (pos + 1 < tokens.size()) __builtin_prefetch(&tokens[pos + 1], 0, 3);
        if (pos + 2 < tokens.size()) __builtin_prefetch(&tokens[pos + 2], 0, 2);
        if (pos + 3 < tokens.size()) __builtin_prefetch(&tokens[pos + 3], 0, 1);
        if (pos + 4 < tokens.size()) __builtin_prefetch(&tokens[pos + 4], 0, 1);
    }
    
    // Parse left side (primary expression)
    ast::ASTNode* left = parse_primary_expression();
    
    if (!left) {
        return nullptr;
    }
    
    // Loop to handle operators with precedence
    while (true) {
        int precedence = get_precedence();
        
        // Check for invalid operators in strict mode
        if (strict_mode_ && precedence == -1) {
            // Invalid operator detected
            return nullptr;  // Fail the parse
        }
        
        if (precedence < min_precedence) {
            break;  // Current operator has lower precedence
        }
        
        // Special case: precedence 0 means stop parsing (not an expression operator)
        if (precedence == 0) {
            break;
        }
        
        // Save operator info
        std::string_view op_value = current_token_->value;
        auto token_type = current_token_->type;
        auto op_keyword_id = current_token_->keyword_id;
        
        // Handle special SQL operators
        bool has_not = false;
        if (token_type == tokenizer::TokenType::Keyword) {
            // Check for NOT prefix for LIKE, IN, BETWEEN
            if (op_keyword_id == db25::Keyword::NOT) {
                // NOT in binary position - must be followed by LIKE, IN, or BETWEEN
                advance(); // consume NOT
                
                if (!current_token_ || current_token_->type != tokenizer::TokenType::Keyword) {
                    // NOT followed by non-keyword - this is an error
                    return left;
                }
                
                auto next_keyword_id = current_token_->keyword_id;
                if (next_keyword_id != db25::Keyword::LIKE &&
                    next_keyword_id != db25::Keyword::IN &&
                    next_keyword_id != db25::Keyword::BETWEEN) {
                    // NOT followed by unsupported keyword - error
                    return left;
                }
                
                // Valid NOT LIKE/IN/BETWEEN
                has_not = true;
                op_value = current_token_->value;
                op_keyword_id = next_keyword_id;  // Update op_keyword_id for proper handling
            }
        }
        
        // Copy operator to arena
        std::string_view op_str_view = copy_to_arena(op_value);
        
        advance(); // consume operator (or the actual operator after NOT)
        
        // Handle special SQL operators
        if (token_type == tokenizer::TokenType::Keyword) {
            // BETWEEN x AND y (or NOT BETWEEN x AND y)
            if (op_keyword_id == db25::Keyword::BETWEEN) {
                auto* lower = parse_expression(precedence + 1);
                if (!lower) return left;
                
                // Expect AND
                if (!current_token_ || current_token_->type != tokenizer::TokenType::Keyword ||
                    current_token_->keyword_id != db25::Keyword::AND) {
                    return left; // Error: missing AND
                }
                advance(); // consume AND
                
                auto* upper = parse_expression(precedence + 1);
                if (!upper) return left;
                
                // Create BETWEEN node
                auto* between_node = arena_.allocate<ast::ASTNode>();
                new (between_node) ast::ASTNode(ast::NodeType::BetweenExpr);
                between_node->node_id = next_node_id_++;
                
                // Store "BETWEEN" or "NOT BETWEEN"
                if (has_not) {
                    between_node->primary_text = copy_to_arena("NOT BETWEEN");
                    between_node->semantic_flags |= (1 << 6); // Use bit 6 for NOT flag
                } else {
                    between_node->primary_text = op_str_view;
                }
                
                // Children: left, lower, upper
                left->parent = between_node;
                between_node->first_child = left;
                between_node->child_count = 1;
                
                lower->parent = between_node;
                left->next_sibling = lower;
                between_node->child_count = 2;
                
                upper->parent = between_node;
                lower->next_sibling = upper;
                between_node->child_count = 3;
                
                left = between_node;
                continue;
            }
            
            // IN (list) or IN (subquery) or NOT IN ...
            if (op_keyword_id == db25::Keyword::IN) {
                // Check if next is a subquery or a list
                ast::ASTNode* in_operand = nullptr;
                
                // Look for opening paren
                if (current_token_ && current_token_->value == "(") {
                    // Peek ahead to see if it's a SELECT
                    if (peek_token_ && peek_token_->type == tokenizer::TokenType::Keyword &&
                        peek_token_->keyword_id == db25::Keyword::SELECT) {
                        // It's a subquery - parse it as a primary expression
                        in_operand = parse_primary_expression();
                    } else {
                        // It's a list - parse it here
                        parenthesis_depth_++;
                        advance(); // consume (
                        
                        auto* in_node = arena_.allocate<ast::ASTNode>();
                        new (in_node) ast::ASTNode(ast::NodeType::InExpr);
                        in_node->node_id = next_node_id_++;
                        
                        // Store "IN" or "NOT IN"
                        if (has_not) {
                            in_node->primary_text = copy_to_arena("NOT IN");
                            in_node->semantic_flags |= (1 << 6); // Use bit 6 for NOT flag
                        } else {
                            in_node->primary_text = op_str_view;
                        }
                        
                        // Left operand
                        left->parent = in_node;
                        in_node->first_child = left;
                        in_node->child_count = 1;
                        
                        // Parse list items
                        ast::ASTNode* last_item = left;
                        while (current_token_ && current_token_->value != ")") {
                            auto* item = parse_expression(0);
                            if (item) {
                                item->parent = in_node;
                                last_item->next_sibling = item;
                                last_item = item;
                                in_node->child_count++;
                            }
                            
                            if (current_token_ && current_token_->value == ",") {
                                advance(); // consume comma
                            } else if (current_token_ && current_token_->value != ")") {
                                break; // Error in list
                            }
                        }
                        
                        if (current_token_ && current_token_->value == ")") {
                            if (parenthesis_depth_ > 0) parenthesis_depth_--;
                            advance(); // consume )
                        }
                        
                        left = in_node;
                        continue;
                    }
                }
                
                // If we have a subquery, create IN node with subquery
                if (in_operand && in_operand->node_type == ast::NodeType::Subquery) {
                    auto* in_node = arena_.allocate<ast::ASTNode>();
                    new (in_node) ast::ASTNode(ast::NodeType::InExpr);
                    in_node->node_id = next_node_id_++;
                    
                    // Store "IN" or "NOT IN"
                    if (has_not) {
                        in_node->primary_text = copy_to_arena("NOT IN");
                        in_node->semantic_flags |= (1 << 6); // Use bit 6 for NOT flag
                    } else {
                        in_node->primary_text = op_str_view;
                    }
                    
                    // Left operand
                    left->parent = in_node;
                    in_node->first_child = left;
                    in_node->child_count = 1;
                    
                    // Subquery as second child
                    in_operand->parent = in_node;
                    left->next_sibling = in_operand;
                    in_node->child_count = 2;
                    
                    left = in_node;
                    continue;
                }
                
                // If neither list nor subquery was parsed properly, just continue
                continue;
            }
            
            // LIKE pattern or NOT LIKE pattern
            if (op_keyword_id == db25::Keyword::LIKE) {
                auto* pattern = parse_expression(precedence + 1);
                if (!pattern) return left;
                
                auto* like_node = arena_.allocate<ast::ASTNode>();
                new (like_node) ast::ASTNode(ast::NodeType::LikeExpr);
                like_node->node_id = next_node_id_++;
                
                // Store "LIKE" or "NOT LIKE"
                if (has_not) {
                    like_node->primary_text = copy_to_arena("NOT LIKE");
                    like_node->semantic_flags |= (1 << 6); // Use bit 6 for NOT flag
                } else {
                    like_node->primary_text = op_str_view;
                }
                
                left->parent = like_node;
                like_node->first_child = left;
                like_node->child_count = 1;
                
                pattern->parent = like_node;
                left->next_sibling = pattern;
                like_node->child_count = 2;
                
                left = like_node;
                continue;
            }
            
            // IS NULL / IS NOT NULL
            if (op_keyword_id == db25::Keyword::IS) {
                bool is_not = false;
                
                // Check for NOT
                if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                    current_token_->keyword_id == db25::Keyword::NOT) {
                    is_not = true;
                    advance(); // consume NOT
                }
                
                // Expect NULL
                if (!current_token_ || current_token_->type != tokenizer::TokenType::Keyword ||
                    (current_token_->value != "NULL" && current_token_->value != "null")) {
                    return left; // Error: expected NULL
                }
                advance(); // consume NULL
                
                auto* is_null_node = arena_.allocate<ast::ASTNode>();
                new (is_null_node) ast::ASTNode(ast::NodeType::IsNullExpr);
                is_null_node->node_id = next_node_id_++;
                
                // Store IS NULL or IS NOT NULL
                is_null_node->primary_text = copy_to_arena(is_not ? "IS NOT NULL" : "IS NULL");
                
                left->parent = is_null_node;
                is_null_node->first_child = left;
                is_null_node->child_count = 1;
                
                left = is_null_node;
                continue;
            }
        }
        
        // Check for ANY/ALL/SOME after comparison operators
        bool has_any_all = false;
        std::string modifier;
        
        if ((op_value == "=" || op_value == "<" || op_value == ">" || 
             op_value == "<=" || op_value == ">=" || op_value == "<>" || op_value == "!=") &&
            current_token_ && (current_token_->type == tokenizer::TokenType::Keyword ||
                              current_token_->type == tokenizer::TokenType::Identifier)) {
            
            const auto& kw = current_token_->value;
            if (kw == "ANY" || kw == "any" || kw == "SOME" || kw == "some") {
                has_any_all = true;
                modifier = " ANY";
                advance(); // consume ANY/SOME
            } else if (kw == "ALL" || kw == "all") {
                has_any_all = true;
                modifier = " ALL";
                advance(); // consume ALL
            }
        }
        
        // Standard binary operator
        ast::ASTNode* right = parse_expression(precedence + 1);
        
        if (!right) {
            // If we can't parse right side, it might be an error
            // For now, just return what we have
            return left;
        }
        
        // Create binary expression node
        auto* binary_node = arena_.allocate<ast::ASTNode>();
        new (binary_node) ast::ASTNode(ast::NodeType::BinaryExpr);
        binary_node->node_id = next_node_id_++;
        
        // Store operator with ANY/ALL modifier if present
        if (has_any_all) {
            std::string op_with_modifier = std::string(op_str_view) + modifier;
            binary_node->primary_text = copy_to_arena(op_with_modifier);
        } else {
            binary_node->primary_text = op_str_view; // Store operator
        }
        
        // Set up the binary expression tree
        left->parent = binary_node;
        binary_node->first_child = left;
        binary_node->child_count = 1;
        
        right->parent = binary_node;
        left->next_sibling = right;
        binary_node->child_count = 2;
        
        // The binary node becomes the new left for the next iteration
        left = binary_node;
    }
    
    return left;
}

ast::ASTNode* Parser::parse_case_expression() {
    push_context(ParseContext::CASE_EXPRESSION);
    
    // Parse CASE expressions:
    // CASE [expr] WHEN condition THEN result [WHEN...] [ELSE result] END
    
    advance(); // consume CASE
    
    auto* case_node = arena_.allocate<ast::ASTNode>();
    new (case_node) ast::ASTNode(ast::NodeType::CaseExpr);
    case_node->node_id = next_node_id_++;
    
    ast::ASTNode* last_child = nullptr;
    ast::ASTNode* search_expr = nullptr;
    
    // Check if this is a searched CASE (has expression after CASE)
    if ((current_token_ && current_token_->type != tokenizer::TokenType::Keyword) ||
        (current_token_->value != "WHEN" && current_token_->value != "when")) {
        // Parse the search expression
        search_expr = parse_expression(0);
        if (search_expr) {
            search_expr->parent = case_node;
            case_node->first_child = search_expr;
            case_node->child_count++;
            last_child = search_expr;
        }
    }
    
    // Parse WHEN clauses
    while (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
           (current_token_->keyword_id == db25::Keyword::WHEN)) {
        advance(); // consume WHEN
        
        // Create a WHEN node (using BinaryExpr with "WHEN" as operator)
        auto* when_node = arena_.allocate<ast::ASTNode>();
        new (when_node) ast::ASTNode(ast::NodeType::BinaryExpr);
        when_node->node_id = next_node_id_++;
        
        // Store "WHEN" as operator
        when_node->primary_text = copy_to_arena("WHEN");
        
        // Parse condition
        auto* condition = parse_expression(0);
        
        // Expect THEN
        if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
            (current_token_->keyword_id == db25::Keyword::THEN)) {
            advance(); // consume THEN
            
            // Parse result expression
            auto* result = parse_expression(0);
            
            // Set up WHEN node with condition and result as children
            if (condition) {
                condition->parent = when_node;
                when_node->first_child = condition;
                when_node->child_count++;
            }
            if (result) {
                result->parent = when_node;
                if (condition) {
                    condition->next_sibling = result;
                } else {
                    when_node->first_child = result;
                }
                when_node->child_count++;
            }
        }
        
        // Add WHEN node to CASE
        when_node->parent = case_node;
        if (!case_node->first_child) {
            case_node->first_child = when_node;
        } else if (last_child) {
            last_child->next_sibling = when_node;
        }
        last_child = when_node;
        case_node->child_count++;
    }
    
    // Parse optional ELSE clause
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        (current_token_->keyword_id == db25::Keyword::ELSE)) {
        advance(); // consume ELSE
        
        auto* else_result = parse_expression(0);
        if (else_result) {
            else_result->parent = case_node;
            if (last_child) {
                last_child->next_sibling = else_result;
            } else {
                case_node->first_child = else_result;
            }
            case_node->child_count++;
        }
    }
    
    // Expect END
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        (current_token_->keyword_id == db25::Keyword::END)) {
        advance(); // consume END
    }
    
    pop_context();
    return case_node;
}

// ========== CAST Expression Parser ==========

ast::ASTNode* Parser::parse_cast_expression() {
    // Parse CAST(expression AS type)
    
    advance(); // consume CAST
    
    // Expect opening parenthesis
    if (!current_token_ || current_token_->value != "(") {
        return nullptr;
    }
    advance(); // consume (
    
    auto* cast_node = arena_.allocate<ast::ASTNode>();
    new (cast_node) ast::ASTNode(ast::NodeType::CastExpr);
    cast_node->node_id = next_node_id_++;
    cast_node->primary_text = copy_to_arena("CAST");
    
    // Parse the expression to cast
    auto* expr = parse_expression(0);
    if (!expr) {
        return nullptr;
    }
    
    // Expect AS keyword
    if (!current_token_ || current_token_->type != tokenizer::TokenType::Keyword ||
        (current_token_->value != "AS" && current_token_->value != "as")) {
        return nullptr;
    }
    advance(); // consume AS
    
    // Parse the target data type
    ast::ASTNode* type_node = nullptr;
    if (current_token_ && (current_token_->type == tokenizer::TokenType::Identifier ||
                           current_token_->type == tokenizer::TokenType::Keyword)) {
        type_node = arena_.allocate<ast::ASTNode>();
        new (type_node) ast::ASTNode(ast::NodeType::Identifier);
        type_node->node_id = next_node_id_++;
        type_node->primary_text = copy_to_arena(current_token_->value);
        advance();
        
        // Check for type parameters like VARCHAR(100)
        if (current_token_ && current_token_->value == "(") {
            advance(); // consume (
            
            // Consume type parameters
            int paren_depth = 1;
            std::string type_params;
            while (current_token_ && paren_depth > 0) {
                if (current_token_->value == "(") {
                    paren_depth++;
                } else if (current_token_->value == ")") {
                    paren_depth--;
                    if (paren_depth == 0) break;
                }
                type_params += std::string(current_token_->value);
                advance();
            }
            
            if (current_token_ && current_token_->value == ")") {
                advance(); // consume final )
            }
            
            // Store type parameters
            if (!type_params.empty()) {
                type_node->schema_name = copy_to_arena(type_params);
            }
        }
    }
    
    // Expect closing parenthesis
    if (!current_token_ || current_token_->value != ")") {
        return nullptr;
    }
    advance(); // consume )
    
    // Set up children: expression and type
    expr->parent = cast_node;
    cast_node->first_child = expr;
    cast_node->child_count = 1;
    
    if (type_node) {
        type_node->parent = cast_node;
        expr->next_sibling = type_node;
        cast_node->child_count = 2;
    }
    
    return cast_node;
}

// ========== EXTRACT Expression Parser ==========

ast::ASTNode* Parser::parse_extract_expression() {
    // Parse EXTRACT(temporal_part FROM temporal_expression)
    // temporal_part: YEAR, MONTH, DAY, HOUR, MINUTE, SECOND, etc.
    
    advance(); // consume EXTRACT
    
    // Expect opening parenthesis
    if (!current_token_ || current_token_->value != "(") {
        return nullptr;
    }
    advance(); // consume (
    
    auto* extract_node = arena_.allocate<ast::ASTNode>();
    new (extract_node) ast::ASTNode(ast::NodeType::FunctionCall);
    extract_node->node_id = next_node_id_++;
    extract_node->primary_text = copy_to_arena("EXTRACT");
    
    // Parse temporal part (YEAR, MONTH, DAY, etc.)
    // These could be keywords or identifiers
    ast::ASTNode* temporal_part = nullptr;
    if (current_token_ && (current_token_->type == tokenizer::TokenType::Identifier ||
                           current_token_->type == tokenizer::TokenType::Keyword)) {
        temporal_part = arena_.allocate<ast::ASTNode>();
        new (temporal_part) ast::ASTNode(ast::NodeType::Identifier);
        temporal_part->node_id = next_node_id_++;
        temporal_part->primary_text = copy_to_arena(current_token_->value);
        advance();
    } else {
        return nullptr; // Expected temporal part
    }
    
    // Expect FROM keyword
    if (!current_token_ || current_token_->type != tokenizer::TokenType::Keyword ||
        (current_token_->value != "FROM" && current_token_->value != "from")) {
        return nullptr;
    }
    advance(); // consume FROM
    
    // Parse the temporal expression
    // Since FROM was already consumed, we can parse normally
    ast::ASTNode* temporal_expr = nullptr;
    
    // Parse expression until closing parenthesis
    // Most commonly this is a column reference or simple expression
    if (current_token_ && current_token_->value != ")") {
        // Check if it's a simple identifier/column
        if (current_token_->type == tokenizer::TokenType::Identifier) {
            // Check for qualified column (table.column)
            if (peek_token_ && peek_token_->value == ".") {
                temporal_expr = parse_column_ref();
            } else {
                // Unqualified column reference
                auto* col_ref = arena_.allocate<ast::ASTNode>();
                new (col_ref) ast::ASTNode(ast::NodeType::ColumnRef);
                col_ref->node_id = next_node_id_++;
                col_ref->primary_text = copy_to_arena(current_token_->value);
                advance();
                temporal_expr = col_ref;
            }
        } else if (current_token_->type == tokenizer::TokenType::Keyword) {
            // Some keywords can be column names
            auto* col_ref = arena_.allocate<ast::ASTNode>();
            new (col_ref) ast::ASTNode(ast::NodeType::ColumnRef);
            col_ref->node_id = next_node_id_++;
            col_ref->primary_text = copy_to_arena(current_token_->value);
            advance();
            temporal_expr = col_ref;
        } else if (current_token_->value == "(") {
            // Could be a nested expression or function
            temporal_expr = parse_primary_expression();
        } else {
            // Try to parse as a general expression
            // But we need to be careful about stopping at the closing paren
            temporal_expr = parse_primary_expression();
        }
    }
    
    // Expect closing parenthesis
    if (!current_token_ || current_token_->value != ")") {
        return nullptr;
    }
    advance(); // consume )
    
    // Set up children: temporal_part and temporal_expression
    if (temporal_part) {
        temporal_part->parent = extract_node;
        extract_node->first_child = temporal_part;
        extract_node->child_count = 1;
    }
    
    if (temporal_expr) {
        temporal_expr->parent = extract_node;
        if (temporal_part) {
            temporal_part->next_sibling = temporal_expr;
        } else {
            extract_node->first_child = temporal_expr;
        }
        extract_node->child_count++;
    }
    
    return extract_node;
}

ast::ASTNode* Parser::parse_primary() {
    // Parse primary expressions - this is called when we need a basic expression
    // It delegates to parse_primary_expression which handles the actual parsing
    return parse_primary_expression();
}

} // namespace db25::parser