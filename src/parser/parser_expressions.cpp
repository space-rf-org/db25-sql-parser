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
    
    // Handle bind parameters: positional '?' and numbered '$1', '$2', ...
    // The tokenizer emits '?' as a single Operator token, and '$1' as an
    // Operator '$' immediately followed by a Number token. Both are turned
    // into a single Parameter node whose primary_text holds the placeholder.
    if (current_token_->type == tokenizer::TokenType::Operator &&
        current_token_->value == "?") {
        auto* param = arena_.allocate<ast::ASTNode>();
        new (param) ast::ASTNode(ast::NodeType::Parameter);
        param->node_id = next_node_id_++;
        param->primary_text = copy_to_arena("?");
        advance(); // consume '?'
        return param;
    }
    if (current_token_->type == tokenizer::TokenType::Operator &&
        current_token_->value == "$" &&
        peek_token_ && peek_token_->type == tokenizer::TokenType::Number) {
        advance(); // consume '$'
        std::string placeholder = "$";
        placeholder += std::string(current_token_->value);
        auto* param = arena_.allocate<ast::ASTNode>();
        new (param) ast::ASTNode(ast::NodeType::Parameter);
        param->node_id = next_node_id_++;
        param->primary_text = copy_to_arena(placeholder);
        advance(); // consume the number
        return param;
    }

    // Handle unary - or +
    if (current_token_->type == tokenizer::TokenType::Operator &&
        (current_token_->value == "-" || current_token_->value == "+")) {
        // Save operator
        std::string_view op = current_token_->value;
        
        // Look ahead - if next is a number, we might combine them
        if (peek_token_ && peek_token_->type == tokenizer::TokenType::Number && op == "-") {
            // Negative numeric literal. The `::type` cast and COLLATE postfix
            // bind TIGHTER than the unary minus (Postgres precedence: `::`, `[]`,
            // unary `+`/`-`, ...), so build the UNSIGNED literal first and run the
            // postfix passes on it. If a postfix binds, the minus applies to the
            // whole postfixed value -> `-(5::text)`, matching the general
            // unary-operand path (`-a::int` -> `-(a::int)`). Only when NO postfix
            // follows do we fold the sign into the literal (the fast path for a
            // plain `-5`). number_literal_type ignores a leading sign, so the node
            // type is the same either way.
            advance();  // consume '-'; current_token_ is the number
            const std::string digits = std::string(current_token_->value);

            auto* num = arena_.allocate<ast::ASTNode>();
            new (num) ast::ASTNode(internal::number_literal_type(digits));
            num->node_id = next_node_id_++;
            num->primary_text = copy_to_arena(digits);  // unsigned for now
            advance();  // consume the number

            std::size_t neg_fold = 0;
            ast::ASTNode* operand = parse_collate_postfix(num, neg_fold);
            if (operand) operand = parse_cast_postfix(operand, neg_fold);
            if (operand == num) {
                // No postfix bound: fold the sign into the literal (fast path).
                num->primary_text = copy_to_arena("-" + digits);
                return num;
            }
            // A postfix bound to the number; wrap the whole thing in unary minus.
            auto* unary_node = arena_.allocate<ast::ASTNode>();
            new (unary_node) ast::ASTNode(ast::NodeType::UnaryExpr);
            unary_node->node_id = next_node_id_++;
            unary_node->primary_text = copy_to_arena(op);  // "-"
            if (operand) {
                operand->parent = unary_node;
                unary_node->first_child = operand;
                unary_node->child_count = 1;
            }
            return unary_node;
        }

        // Otherwise, create unary expression
        auto* unary_node = arena_.allocate<ast::ASTNode>();
        new (unary_node) ast::ASTNode(ast::NodeType::UnaryExpr);
        unary_node->node_id = next_node_id_++;
        
        // Store operator
        unary_node->primary_text = copy_to_arena(op);
        
        advance(); // consume operator

        // Parse operand. The `::type` postfix cast and COLLATE bind TIGHTER than
        // unary +/- (Postgres precedence: `::`, `[]`, unary `+`/`-`, `^`, ...),
        // so run the postfix passes on the operand right here. Otherwise the
        // operand is a bare primary and the postfix re-binds to the WHOLE unary
        // node up in parse_expression, mis-parsing `-a::int` as `(-a)::int`
        // instead of `-(a::int)` (and `-'5'::int` as unary minus over a string
        // literal - a spurious type error on a legal query).
        auto* operand = parse_primary_expression();
        std::size_t unary_fold = 0;
        if (operand) {
            operand = parse_collate_postfix(operand, unary_fold);
        }
        if (operand) {
            operand = parse_cast_postfix(operand, unary_fold);
        }
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
        new (num) ast::ASTNode(internal::number_literal_type(current_token_->value));
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
        
        // A parenthesized subquery in expression / IN / EXISTS context may be any
        // query block that begins with a keyword: SELECT, VALUES, or WITH (a bare
        // `(` here stays a grouped expression / row constructor, unchanged). A
        // leading VALUES was previously mis-parsed as a `VALUES(...)` function
        // call, and `(VALUES (1) UNION SELECT 2)` left the `UNION ...` unconsumed.
        if (at_query_block_start()) {
            // It's a subquery
            auto* subquery_node = arena_.allocate<ast::ASTNode>();
            new (subquery_node) ast::ASTNode(ast::NodeType::Subquery);
            subquery_node->node_id = next_node_id_++;

            push_context(ParseContext::SUBQUERY);
            // Parse the query body (SELECT / VALUES / WITH), folding any set-op tail.
            auto* select_stmt = parse_query_body();
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
            // Grouped expression, or a row/tuple constructor "( expr, expr, ... )"
            auto* first = parse_expression(0);

            // Row constructor: a comma after the first expression means this is
            // a parenthesised expression list "(a, b, ...)", not a simple
            // grouping. Same RowConstructor node as the explicit ROW(...) form.
            if (current_token_ && current_token_->value == ",") {
                auto* row_node = arena_.allocate<ast::ASTNode>();
                new (row_node) ast::ASTNode(ast::NodeType::RowConstructor);
                row_node->node_id = next_node_id_++;
                row_node->primary_text = copy_to_arena("ROW");
                if (first) {
                    first->parent = row_node;
                    row_node->first_child = first;
                    row_node->child_count = 1;
                }
                ast::ASTNode* last_elem = first;
                while (current_token_ && current_token_->value == ",") {
                    advance(); // consume ','
                    auto* elem = parse_expression(0);
                    if (!elem) break;
                    elem->parent = row_node;
                    if (last_elem) {
                        last_elem->next_sibling = elem;
                    } else {
                        row_node->first_child = elem;
                    }
                    last_elem = elem;
                    row_node->child_count++;
                }
                if (current_token_ && current_token_->value == ")") {
                    if (parenthesis_depth_ > 0) parenthesis_depth_--;
                    advance();
                }
                return row_node;
            }

            // Consume closing ')'
            if (current_token_ && current_token_->value == ")") {
                if (parenthesis_depth_ > 0) parenthesis_depth_--;
                advance();
            }

            return first;
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
        } else if ((kw == "INTERVAL" || kw == "interval") &&
                   peek_token_ && peek_token_->type == tokenizer::TokenType::String) {
            // INTERVAL '<literal>' — parsed as an IntervalLiteral whose value is
            // exposed as a child StringLiteral (the interval string is not further
            // decomposed here). Only triggers when a string follows, so INTERVAL
            // used as a plain identifier falls through unchanged.
            advance(); // consume INTERVAL
            auto* interval_node = arena_.allocate<ast::ASTNode>();
            new (interval_node) ast::ASTNode(ast::NodeType::IntervalLiteral);
            interval_node->node_id = next_node_id_++;
            interval_node->data_type = ast::DataType::Interval;
            interval_node->primary_text = copy_to_arena(current_token_->value);

            auto* str_node = arena_.allocate<ast::ASTNode>();
            new (str_node) ast::ASTNode(ast::NodeType::StringLiteral);
            str_node->node_id = next_node_id_++;
            str_node->primary_text = copy_to_arena(current_token_->value);
            str_node->parent = interval_node;
            interval_node->first_child = str_node;
            interval_node->child_count = 1;

            advance(); // consume the interval string
            return interval_node;
        } else if ((kw == "DATE" || kw == "date" || kw == "TIME" || kw == "time" ||
                    kw == "TIMESTAMP" || kw == "timestamp") &&
                   peek_token_ && peek_token_->type == tokenizer::TokenType::String) {
            // DATE / TIME / TIMESTAMP '<literal>' typed literal -> DateTimeLiteral,
            // mirroring the INTERVAL branch. Only when a string follows, so the
            // bare keyword as a type name (CAST(x AS DATE)) or a column named
            // `date` falls through unchanged. The value is carried in primary_text
            // and as a child StringLiteral.
            const bool is_time = (kw == "TIME" || kw == "time");
            const bool is_ts = (kw == "TIMESTAMP" || kw == "timestamp");
            advance(); // consume the type keyword
            auto* dt_node = arena_.allocate<ast::ASTNode>();
            new (dt_node) ast::ASTNode(ast::NodeType::DateTimeLiteral);
            dt_node->node_id = next_node_id_++;
            dt_node->data_type = is_ts ? ast::DataType::Timestamp
                                 : is_time ? ast::DataType::Time
                                           : ast::DataType::Date;
            dt_node->primary_text = copy_to_arena(current_token_->value);

            auto* str_node = arena_.allocate<ast::ASTNode>();
            new (str_node) ast::ASTNode(ast::NodeType::StringLiteral);
            str_node->node_id = next_node_id_++;
            str_node->primary_text = copy_to_arena(current_token_->value);
            str_node->parent = dt_node;
            dt_node->first_child = str_node;
            dt_node->child_count = 1;

            advance(); // consume the literal string
            return dt_node;
        }
    }

    // A clause-introducing keyword (FROM / WHERE / GROUP / HAVING / ORDER /
    // LIMIT / OFFSET) terminates an expression - it is NEVER a value operand.
    // get_precedence() already stops the operator loop on these in operator
    // position, but in OPERAND position (the RHS of `a + FROM t`, or a bare
    // `SELECT WHERE ...`) the identifier path below would swallow the keyword as
    // a ColumnRef, silently deleting the real clause and accepting a
    // structurally wrong statement. Reject here so the operand parse returns
    // null and the caller reports a syntax error.
    if (current_token_->type == tokenizer::TokenType::Keyword) {
        switch (current_token_->keyword_id) {
            case db25::Keyword::FROM:
            case db25::Keyword::WHERE:
            case db25::Keyword::GROUP:
            case db25::Keyword::HAVING:
            case db25::Keyword::ORDER:
            case db25::Keyword::LIMIT:
            case db25::Keyword::OFFSET:
                return nullptr;
            default:
                break;
        }
    }

    // Handle identifiers (could be column, function, or simple identifier)
    // Also handle keywords that can be used as identifiers (e.g., date, time, level)
    if (current_token_->type == tokenizer::TokenType::Identifier ||
        current_token_->type == tokenizer::TokenType::Keyword) {
        // ARRAY[ elem, ... ] constructor. Handled before the plain-identifier
        // path so the bracketed element list is consumed as part of the
        // expression instead of being left dangling (which desynchronised
        // parenthesis tracking inside e.g. ANY(ARRAY[...])).
        if ((current_token_->value == "ARRAY" || current_token_->value == "array") &&
            peek_token_ && peek_token_->value == "[") {
            advance(); // consume ARRAY
            advance(); // consume '['
            auto* array_node = arena_.allocate<ast::ASTNode>();
            new (array_node) ast::ASTNode(ast::NodeType::ArrayConstructor);
            array_node->node_id = next_node_id_++;
            array_node->primary_text = copy_to_arena("ARRAY");

            ast::ASTNode* last_elem = nullptr;
            while (current_token_ && current_token_->value != "]") {
                auto* elem = parse_expression(0);
                if (!elem) break;
                elem->parent = array_node;
                if (!array_node->first_child) {
                    array_node->first_child = elem;
                } else {
                    last_elem->next_sibling = elem;
                }
                last_elem = elem;
                array_node->child_count++;
                if (current_token_ && current_token_->value == ",") {
                    advance();
                } else {
                    break;
                }
            }
            if (current_token_ && current_token_->value == "]") {
                advance(); // consume ']'
            }
            return array_node;
        }

        // ROW(a, b, ...) explicit row constructor. Handled before the function-
        // call path so ROW is not parsed as a function named "ROW". Unlike the
        // bare (a, b) form below, ROW(x) with a single element is still a row.
        if (current_token_->type == tokenizer::TokenType::Keyword &&
            current_token_->keyword_id == db25::Keyword::ROW &&
            peek_token_ && peek_token_->value == "(") {
            advance();  // consume ROW
            parenthesis_depth_++;
            advance();  // consume '('
            auto* row_node = arena_.allocate<ast::ASTNode>();
            new (row_node) ast::ASTNode(ast::NodeType::RowConstructor);
            row_node->node_id = next_node_id_++;
            row_node->primary_text = copy_to_arena("ROW");
            ast::ASTNode* last = nullptr;
            while (current_token_ && current_token_->value != ")") {
                auto* el = parse_expression(0);
                if (!el) break;
                el->parent = row_node;
                if (!row_node->first_child) row_node->first_child = el;
                else last->next_sibling = el;
                last = el;
                row_node->child_count++;
                if (current_token_ && current_token_->value == ",") advance();
                else break;
            }
            if (current_token_ && current_token_->value == ")") {
                if (parenthesis_depth_ > 0) parenthesis_depth_--;
                advance();  // consume ')'
            }
            return row_node;
        }

        // Niladic datetime functions - CURRENT_DATE / CURRENT_TIME /
        // CURRENT_TIMESTAMP - are written WITHOUT parentheses but are functions,
        // not columns. Emit a no-arg FunctionCall so the analyzer types them
        // (CURRENT_DATE -> Date, CURRENT_TIMESTAMP -> Timestamp) instead of
        // reporting an unresolved column reference.
        //
        // A DELIMITED identifier (`"current_date"`) is an ordinary column named
        // current_date, never the function - the double quotes are precisely the
        // SQL way to force that reading. The tokenizer flags it; skip the niladic
        // promotion for it and fall through to plain column-reference handling.
        if (!current_token_->delimited) {
            std::string u;
            u.reserve(current_token_->value.size());
            for (char c : current_token_->value) {
                u.push_back((c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c);
            }
            if (u == "CURRENT_DATE" || u == "CURRENT_TIME" || u == "CURRENT_TIMESTAMP") {
                // Only the PAREN-LESS spelling is niladic. CURRENT_TIME(p) and
                // CURRENT_TIMESTAMP(p) take a precision argument, so if a '('
                // follows, fall through to the function-call path below rather
                // than emitting a no-arg node - otherwise the argument list AND
                // every following clause (FROM/WHERE/...) are silently dropped
                // while parse() still reports success.
                const bool has_arg_list =
                    peek_token_ &&
                    peek_token_->type == tokenizer::TokenType::Delimiter &&
                    peek_token_->value == "(";
                if (!has_arg_list) {
                    auto* fn = arena_.allocate<ast::ASTNode>();
                    new (fn) ast::ASTNode(ast::NodeType::FunctionCall);
                    fn->node_id = next_node_id_++;
                    fn->primary_text = copy_to_arena(current_token_->value);
                    advance();  // consume the niladic-function name
                    return fn;
                }
            }
        }

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
        
        // Comparison operators
        if (op == "=") return 4;  // PREC_COMP
        if (op == "<" || op == ">") return 4;  // PREC_COMP
        if (op == "<=" || op == ">=") return 4;  // PREC_COMP
        if (op == "<>" || op == "!=") return 4;  // PREC_COMP

        // String concatenation binds TIGHTER than comparison (a || b = c parses
        // as (a || b) = c), and looser than the bitwise/arithmetic operators.
        if (op == "||") return 5;  // PREC_CONCAT

        // Bitwise / shift operators all bind tighter than comparison (so
        // `flags & 4 = 4` is `(flags & 4) = 4`) and are ranked among themselves
        // as | < ^ < & < shifts, per common SQL (MySQL) ordering.
        if (op == "|") return 6;   // Bitwise OR
        if (op == "^") return 7;   // Bitwise XOR
        if (op == "&") return 8;   // Bitwise AND
        if (op == "<<" || op == ">>") return 9;  // Bit shifts

        // Arithmetic operators (tightest binary operators).
        if (op == "+" || op == "-") return 10;  // PREC_TERM
        if (op == "*" || op == "/" || op == "%") return 11;  // PREC_FACTOR
        
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
        // BETWEEN / IN / LIKE / ILIKE bind TIGHTER than the comparison operators
        // (< > = <= >= <>), matching PostgreSQL (Table 4.2). So a comparison that
        // PRECEDES one of them must not absorb its left operand: `x = y BETWEEN 1
        // AND 10` is `x = (y BETWEEN 1 AND 10)`, `a = b LIKE c` is `a = (b LIKE c)`.
        // At the old precedence 3 (below comparison 4) the comparison reduced
        // first, producing the inverted `(x = y) BETWEEN ...`. (The operand hacks
        // - bounds / pattern parsed at 5 - only fixed the reverse order, where
        // LIKE/BETWEEN/IN precede the comparison.) IS stays BELOW comparison (3),
        // which matches Postgres (`a = b IS NULL` -> `(a = b) IS NULL`).
        if (current_token_->keyword_id == db25::Keyword::BETWEEN) return 5;  // PREC_RANGE
        if (current_token_->keyword_id == db25::Keyword::IN) return 5;  // PREC_RANGE
        if (current_token_->keyword_id == db25::Keyword::LIKE) return 5;  // PREC_RANGE
        if (current_token_->keyword_id == db25::Keyword::ILIKE) return 5;  // PREC_RANGE
        if (current_token_->keyword_id == db25::Keyword::IS) return 3;  // PREC_IS (for IS NULL)
        // NOT can be binary when followed by LIKE/ILIKE/IN/BETWEEN
        if (current_token_->keyword_id == db25::Keyword::NOT) {
            // Look ahead to see if it's NOT LIKE/ILIKE/IN/BETWEEN
            if (peek_token_ && peek_token_->type == tokenizer::TokenType::Keyword) {
                if (peek_token_->keyword_id == db25::Keyword::LIKE ||
                    peek_token_->keyword_id == db25::Keyword::ILIKE ||
                    peek_token_->keyword_id == db25::Keyword::IN ||
                    peek_token_->keyword_id == db25::Keyword::BETWEEN) {
                    return 5;  // Same precedence as LIKE/IN/BETWEEN (PREC_RANGE)
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

    // COLLATE and the `::type` cast shorthand are SAME-precedence postfixes -
    // both bind tighter than any binary operator - applied left to right. Loop
    // the two passes until neither consumes a token: a single collate-then-cast
    // pass dropped a COLLATE that TRAILS a cast (`a::int COLLATE "C"`), because
    // the collate pass ran before the `::` was consumed and nothing re-checked
    // for COLLATE afterwards - leaving `COLLATE "C" <rest of statement>`
    // unconsumed and silently discarded (FROM/WHERE and all). Looping makes
    // `a::int COLLATE "C"` -> `(a::int) COLLATE "C"`, matching the CAST(...)
    // COLLATE and plain-COLLATE shapes, and preserves the trailing clauses.
    // One shared iterative-fold budget for this expression's entire left-deep
    // spine: the COLLATE / ::cast postfixes below AND the binary-operator fold
    // further down all charge against it, so no combination (a pure chain, an
    // alternating COLLATE/::cast chain, or postfixes topped by operators) can
    // build an AST deeper than max_depth by resetting a per-loop counter.
    std::size_t fold_count = 0;
    while (true) {
        ast::ASTNode* before = left;
        left = parse_collate_postfix(left, fold_count);
        if (!left) {
            return nullptr;
        }
        left = parse_cast_postfix(left, fold_count);
        if (!left) {
            return nullptr;
        }
        if (left == before) {
            break;  // neither postfix consumed anything
        }
    }

    // A value array-subscript `expr[...]` (e.g. `a[1]`, `t.a[1]`) is not part of
    // this grammar - only the ARRAY[...] constructor and the `::type[]` array-type
    // cast suffix use brackets, and both are fully consumed above (in
    // parse_primary_expression / parse_cast_postfix). A '[' still sitting here
    // therefore follows a COMPLETE value expression and is a subscript. It has no
    // rule, so it would desync the operator loop and let statement-level leftover
    // tolerance silently DROP the '[' and everything after it (further select
    // items, FROM, the rest of a WHERE predicate). Reject it explicitly instead,
    // matching the norm for other unsupported constructs (WITHIN GROUP, a COLLATE
    // trailing a cast, OVER <named-window>).
    if (current_token_ && current_token_->type == tokenizer::TokenType::Delimiter &&
        current_token_->value == "[") {
        error("array subscript on a value expression is not supported");
        return nullptr;
    }

    // Loop to handle operators with precedence
    // A left-associative chain (`a AND a AND ...`, `1+1+...`, `a||b||...`) is
    // folded ITERATIVELY here, one level of left-deep AST per iteration. Unlike a
    // nested subexpression - which re-enters the DepthGuarded parse_expression and
    // so is bounded by config_.max_depth - this fold never touches the recursion
    // guard, so without a cap it builds an AST whose depth == the operator count,
    // unbounded, which then overflows every downstream recursive walker
    // (analyze / bind / optimize) on otherwise-legal input. Bound the cumulative
    // depth (this call's recursion depth + operators folded here) at max_depth and
    // surface the standard depth-exceeded error, exactly as fold_set_operations
    // does for a flat set-op chain (commit 196d58d). fold_count is the SAME
    // shared budget the postfix folds above already charged to, so a spine of
    // postfixes topped by operators is bounded by their combined depth.
    while (true) {
        // The PostgreSQL JSON access operators `->` / `->>` are tokenized as `-`
        // followed by `>` / `>>` (the tokenizer emits no combined `->` token).
        // DB25 does not support them. Detect the pair up front and reject with a
        // clear message: otherwise `-` parses as binary minus and the following
        // `>` fails as an operand, which - before the missing-RHS guard below -
        // silently dropped the operator and every following clause (FROM/WHERE).
        if (current_token_ && current_token_->type == tokenizer::TokenType::Operator &&
            current_token_->value == "-" && peek_token_ &&
            peek_token_->type == tokenizer::TokenType::Operator &&
            (peek_token_->value == ">" || peek_token_->value == ">>")) {
            error("the JSON access operator '->" +
                  std::string(peek_token_->value == ">>" ? ">" : "") +
                  "' is not supported");
            return nullptr;
        }

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

        if (current_depth_ + fold_count >= config_.max_depth) {
            depth_exceeded_ = true;
            break;  // chain too deep: stop folding; parse() surfaces the error
        }
        ++fold_count;

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
                    next_keyword_id != db25::Keyword::ILIKE &&
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
                // BETWEEN bounds are value expressions: they must bind TIGHTER
                // than comparison so `x BETWEEN a = c AND b` does not fold the
                // low bound into `(a = c)`. Comparison is precedence 4
                // (PREC_COMP), so parse bounds at PREC_COMP + 1. This still
                // stops at the `AND` separator (precedence 2) and leaves any
                // trailing `AND c` to the outer loop, so
                // `x BETWEEN a AND b AND c` stays `(x BETWEEN a AND b) AND c`.
                constexpr int kBetweenBoundPrec = 5; // PREC_COMP + 1
                auto* lower = parse_expression(kBetweenBoundPrec);
                if (!lower) return left;

                // Expect AND
                if (!current_token_ || current_token_->type != tokenizer::TokenType::Keyword ||
                    current_token_->keyword_id != db25::Keyword::AND) {
                    return left; // Error: missing AND
                }
                advance(); // consume AND

                auto* upper = parse_expression(kBetweenBoundPrec);
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

                // IN must be followed by a parenthesised list or subquery. Anything
                // else (e.g. `a IN 5`) is malformed; error rather than silently
                // dropping the IN and degrading the predicate to its bare left
                // operand (a silent wrong result - the filter would vanish).
                if (!current_token_ || current_token_->value != "(") {
                    error("expected '(' with a value list or subquery after IN");
                    return nullptr;
                }

                // Look for opening paren
                if (current_token_ && current_token_->value == "(") {
                    // A query body after `(` means `IN (subquery)`; anything else
                    // is an `IN (expr, expr, ...)` value list. The body starts with
                    // a SELECT / VALUES / WITH keyword OR a nested `(` that
                    // introduces one (`IN ((SELECT 1) UNION (SELECT 2))`), scanned
                    // from one token past the current `(`. A leading VALUES was
                    // previously treated as a list and mis-parsed as a `VALUES(..)`
                    // function call; a leading `(` dropped the set-op tail.
                    if (tokenizer_ != nullptr &&
                        paren_group_starts_query(tokenizer_->position() + 1)) {
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
            
            // LIKE / ILIKE pattern or NOT LIKE / NOT ILIKE pattern
            if (op_keyword_id == db25::Keyword::LIKE ||
                op_keyword_id == db25::Keyword::ILIKE) {
                // LIKE is precedence 3 (below comparison), but its PATTERN must
                // bind TIGHTER than comparison so `x LIKE '%a%' = TRUE` parses as
                // `(x LIKE '%a%') = TRUE`, not `x LIKE ('%a%' = TRUE)`. Parsing at
                // `precedence + 1` (= 4, PREC_COMP) wrongly folded the trailing
                // `= TRUE` into the pattern. Parse at PREC_COMP + 1, exactly as
                // BETWEEN parses its bounds, so comparison operators are left for
                // the outer loop while concat/arithmetic still bind into the
                // pattern.
                constexpr int kComparisonOperandPrec = 5;  // PREC_COMP + 1
                auto* pattern = parse_expression(kComparisonOperandPrec);
                if (!pattern) return left;

                auto* like_node = arena_.allocate<ast::ASTNode>();
                new (like_node) ast::ASTNode(ast::NodeType::LikeExpr);
                like_node->node_id = next_node_id_++;

                const bool is_ilike = (op_keyword_id == db25::Keyword::ILIKE);
                // Store the canonical operator text; the binder reads ILIKE (for
                // the case-insensitive flag) and NOT off this text.
                if (has_not) {
                    like_node->primary_text = copy_to_arena(is_ilike ? "NOT ILIKE" : "NOT LIKE");
                    like_node->semantic_flags |= (1 << 6); // Use bit 6 for NOT flag
                } else {
                    like_node->primary_text = copy_to_arena(is_ilike ? "ILIKE" : "LIKE");
                }
                
                left->parent = like_node;
                like_node->first_child = left;
                like_node->child_count = 1;
                
                pattern->parent = like_node;
                left->next_sibling = pattern;
                like_node->child_count = 2;

                // Optional ESCAPE '<char>' clause. Attach the escape-character
                // expression as a third child; the binder lowers it into the Like
                // node's escape slot and the evaluator treats the following
                // pattern character literally.
                if (current_token_ &&
                    current_token_->type == tokenizer::TokenType::Keyword &&
                    current_token_->keyword_id == db25::Keyword::ESCAPE) {
                    advance();  // consume ESCAPE
                    // Same reasoning as the pattern: parse the ESCAPE operand at
                    // PREC_COMP + 1 so a trailing comparison is left for the outer
                    // loop rather than folded into the escape expression.
                    if (auto* escape = parse_expression(5 /* PREC_COMP + 1 */)) {
                        escape->parent = like_node;
                        pattern->next_sibling = escape;
                        like_node->child_count = 3;
                    }
                }

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
                
                // IS [NOT] TRUE / FALSE / UNKNOWN -- a three-valued boolean test
                // that collapses the operand's 3VL truth value to a plain 2VL
                // boolean (never NULL). TRUE/FALSE tokenize as keywords; UNKNOWN
                // is not a reserved keyword, so it arrives as a bare identifier
                // (matched case-insensitively without a heap allocation).
                auto is_unknown_ident = [](std::string_view v) {
                    static constexpr char kU[] = "UNKNOWN";
                    if (v.size() != 7) return false;
                    for (std::size_t i = 0; i < 7; ++i) {
                        char c = v[i];
                        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
                        if (c != kU[i]) return false;
                    }
                    return true;
                };
                if (current_token_ &&
                    (current_token_->keyword_id == db25::Keyword::KW_TRUE ||
                     current_token_->keyword_id == db25::Keyword::KW_FALSE ||
                     is_unknown_ident(current_token_->value))) {
                    const char* target =
                        current_token_->keyword_id == db25::Keyword::KW_TRUE  ? "TRUE"
                        : current_token_->keyword_id == db25::Keyword::KW_FALSE ? "FALSE"
                        : "UNKNOWN";
                    advance(); // consume TRUE / FALSE / UNKNOWN

                    auto* bool_test_node = arena_.allocate<ast::ASTNode>();
                    new (bool_test_node) ast::ASTNode(ast::NodeType::BooleanTestExpr);
                    bool_test_node->node_id = next_node_id_++;

                    // Store the full test, e.g. "IS TRUE" / "IS NOT UNKNOWN". The
                    // binder reads the target off this text and the NOT off the
                    // presence of "NOT" (matching the IsNull convention).
                    bool_test_node->primary_text =
                        copy_to_arena(std::string("IS ") + (is_not ? "NOT " : "") + target);

                    left->parent = bool_test_node;
                    bool_test_node->first_child = left;
                    bool_test_node->child_count = 1;

                    left = bool_test_node;
                    continue;
                }

                // IS [NOT] DISTINCT FROM <expr> -- a null-safe comparison that is
                // ALWAYS a plain boolean, never NULL (two NULLs are "not distinct";
                // a NULL and a non-NULL are "distinct"). Represented as a BinaryExpr
                // whose operator text the binder maps to
                // BinaryOp::Is[Not]DistinctFrom. Previously the DISTINCT FROM tail
                // was silently dropped, leaving the predicate as its bare left
                // operand (a silent wrong result).
                if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
                    current_token_->keyword_id == db25::Keyword::DISTINCT) {
                    advance(); // consume DISTINCT
                    if (!current_token_ || current_token_->type != tokenizer::TokenType::Keyword ||
                        current_token_->keyword_id != db25::Keyword::FROM) {
                        error("expected FROM after IS [NOT] DISTINCT");
                        return nullptr;
                    }
                    advance(); // consume FROM
                    ast::ASTNode* rhs = parse_expression(precedence + 1);
                    if (!rhs) {
                        error("expected expression after IS [NOT] DISTINCT FROM");
                        return nullptr;
                    }
                    auto* dist_node = arena_.allocate<ast::ASTNode>();
                    new (dist_node) ast::ASTNode(ast::NodeType::BinaryExpr);
                    dist_node->node_id = next_node_id_++;
                    dist_node->primary_text =
                        copy_to_arena(is_not ? "IS NOT DISTINCT FROM" : "IS DISTINCT FROM");
                    left->parent = dist_node;
                    dist_node->first_child = left;
                    left->next_sibling = rhs;
                    rhs->parent = dist_node;
                    dist_node->child_count = 2;
                    left = dist_node;
                    continue;
                }

                // Expect NULL
                if (!current_token_ || current_token_->type != tokenizer::TokenType::Keyword ||
                    (current_token_->value != "NULL" && current_token_->value != "null")) {
                    return left; // Error: expected NULL / TRUE / FALSE / UNKNOWN
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
            // The operator was already consumed, so a missing/invalid right
            // operand is a syntax error: `a + FROM t` (a clause keyword in
            // operand position), `a + )`, a trailing `a +`, or an unsupported
            // operator token like the `->` JSON accessor (tokenized `-` then
            // `>`, whose `>` is no operand). Returning `left` here SILENTLY
            // dropped the operator and every following clause (FROM/WHERE/...),
            // accepting a structurally wrong AST. Reject instead - matching the
            // value-subscript / WITHIN GROUP / DISTINCT-FROM rejections.
            error("expected an expression after binary operator '" +
                  std::string(op_str_view) + "'");
            return nullptr;
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
    
    // A searched CASE (`CASE WHEN ...`) has no operand between CASE and the first
    // WHEN; a simple CASE (`CASE x WHEN ...`) does. Detect the WHEN by keyword id
    // - not by comparing the token text to "WHEN"/"when", which missed a
    // mixed-case `When` (SQL keywords are case-insensitive) and mis-parsed it as a
    // simple-CASE operand, losing every branch. Guarding on the keyword id also
    // avoids dereferencing a null current token.
    const bool at_when = current_token_ != nullptr &&
                         current_token_->type == tokenizer::TokenType::Keyword &&
                         current_token_->keyword_id == db25::Keyword::WHEN;
    if (!at_when) {
        // Parse the search expression (the simple-CASE operand).
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
    
    // Expect END. A CASE without its closing END is malformed; error rather than
    // silently accepting a truncated expression (parse() surfaces the recorded
    // error once the null unwinds to the top).
    if (current_token_ && current_token_->type == tokenizer::TokenType::Keyword &&
        (current_token_->keyword_id == db25::Keyword::END)) {
        advance(); // consume END
    } else {
        error("expected END to close CASE expression");
        pop_context();
        return nullptr;
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

        // Array type suffix `[]` (optionally sized / multi-dimensional), e.g.
        // CAST(x AS int[]) / CAST(x AS int[][]). Mirror the `::type` postfix cast
        // (parse_cast_postfix): count the `[]` pairs, then append the whole suffix
        // ONCE (linear, not quadratic) and set the array-type flag. Without this,
        // CAST(x AS int[]) was rejected while the equivalent x::int[] was accepted,
        // and embedding it in a larger statement silently truncated the rest.
        std::size_t array_dims = 0;
        while (current_token_ && current_token_->value == "[") {
            advance();  // consume [
            if (current_token_ &&
                current_token_->type == tokenizer::TokenType::Number) {
                advance();  // optional array size, ignored (as in DDL / ::cast)
            }
            if (current_token_ && current_token_->value == "]") {
                advance();  // consume ]
                ++array_dims;
            } else {
                break;  // malformed: leave it for the error path below
            }
        }
        if (array_dims > 0) {
            type_node->flags = static_cast<ast::NodeFlags>(
                static_cast<uint8_t>(type_node->flags) | 0x80);  // array-type flag
            std::string type_text{type_node->primary_text};
            type_text.reserve(type_text.size() + array_dims * 2);
            for (std::size_t i = 0; i < array_dims; ++i) {
                type_text += "[]";
            }
            type_node->primary_text = copy_to_arena(type_text);
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

// ========== COLLATE postfix ==========

ast::ASTNode* Parser::parse_collate_postfix(ast::ASTNode* operand,
                                            std::size_t& fold_depth) {
    // `<value> COLLATE <collation>` annotates a value with a collation. COLLATE
    // binds tighter than any binary operator, so it is applied as a postfix to
    // the primary immediately after it is parsed. The collation name is stored
    // on the CollateClause's schema_name; the annotated value is its one child.
    // Like the binary-operator and ::cast folds, this COLLATE chain is iterative
    // and so escapes the recursion DepthGuard; charge each fold to the SHARED
    // fold_depth budget so a pathological `a COLLATE "C" ...` chain - or an
    // alternating `a COLLATE "C"::int COLLATE "C"::int ...` chain spread across
    // this helper and parse_cast_postfix - is rejected rather than building an
    // unbounded tree that overflows downstream recursive walkers.
    while (operand != nullptr && current_token_ &&
           current_token_->type == tokenizer::TokenType::Keyword &&
           current_token_->keyword_id == db25::Keyword::COLLATE) {
        if (current_depth_ + fold_depth >= config_.max_depth) {
            depth_exceeded_ = true;
            break;  // chain too deep: parse() surfaces the depth-exceeded error
        }
        ++fold_depth;
        advance();  // consume COLLATE
        auto* node = arena_.allocate<ast::ASTNode>();
        new (node) ast::ASTNode(ast::NodeType::CollateClause);
        node->node_id = next_node_id_++;
        node->primary_text = copy_to_arena("COLLATE");

        // The collation name is an identifier (bare or delimited) or a string.
        if (current_token_ &&
            (current_token_->type == tokenizer::TokenType::Identifier ||
             current_token_->type == tokenizer::TokenType::Keyword ||
             current_token_->type == tokenizer::TokenType::String)) {
            node->schema_name = copy_to_arena(current_token_->value);
            advance();
        }

        operand->parent = node;
        node->first_child = operand;
        node->child_count = 1;
        operand = node;
    }
    return operand;
}

ast::ASTNode* Parser::parse_cast_postfix(ast::ASTNode* operand,
                                         std::size_t& fold_depth) {
    // `<value>::<type>` is the PostgreSQL shorthand for CAST(<value> AS <type>).
    // The tokenizer emits `::` as a single two-char token (typed Delimiter, since
    // `:` is a delimiter), so match on the value. This builds the SAME CastExpr
    // shape parse_cast_expression() produces - CastExpr with children
    // [value, Identifier(typename)], the type's parameters (e.g. ::VARCHAR(100))
    // stored on the type node's schema_name - so the analyzer and binder need no
    // change. Chained casts (`a::int::text`) fold left via the loop.
    // Like the binary-operator fold, this cast chain is iterative and so escapes
    // the recursion DepthGuard; charge each fold to the SHARED fold_depth budget
    // (shared with parse_collate_postfix and the operator loop) so a pathological
    // `x::int::int...` chain - or an alternating collate/cast chain - is rejected
    // rather than building an unbounded tree that overflows downstream walkers.
    while (operand != nullptr && current_token_ && current_token_->value == "::") {
        if (current_depth_ + fold_depth >= config_.max_depth) {
            depth_exceeded_ = true;
            break;  // chain too deep: parse() surfaces the depth-exceeded error
        }
        ++fold_depth;
        advance();  // consume ::
        if (!current_token_ ||
            (current_token_->type != tokenizer::TokenType::Identifier &&
             current_token_->type != tokenizer::TokenType::Keyword)) {
            return operand;  // `::` without a type name: leave the value as-is
        }
        auto* cast_node = arena_.allocate<ast::ASTNode>();
        new (cast_node) ast::ASTNode(ast::NodeType::CastExpr);
        cast_node->node_id = next_node_id_++;
        cast_node->primary_text = copy_to_arena("CAST");

        auto* type_node = arena_.allocate<ast::ASTNode>();
        new (type_node) ast::ASTNode(ast::NodeType::Identifier);
        type_node->node_id = next_node_id_++;
        type_node->primary_text = copy_to_arena(current_token_->value);
        advance();

        // Optional type parameters, e.g. `x::VARCHAR(100)`.
        if (current_token_ && current_token_->value == "(") {
            advance();  // consume (
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
                advance();  // consume final )
            }
            if (!type_params.empty()) {
                type_node->schema_name = copy_to_arena(type_params);
            }
        }

        // Array type suffix `[]` (optionally sized / multi-dimensional, e.g.
        // `text[]`, `int[3]`, `int[][]`). The DDL type parser handles this, but
        // the `::type` postfix cast did not, so `x::text[]` only parsed where a
        // later postfix pass happened to absorb the `[`; nested in parens, a
        // function argument, or a CASE branch it was left dangling and the parse
        // failed. Consume the suffix here so an array-type cast parses uniformly.
        // Count the `[]` pairs first, then append the suffix ONCE. Rebuilding
        // `primary_text` inside the loop re-copied the whole (growing) type
        // string on every dimension - O(N^2) parse time and O(N^2) never-freed
        // arena bytes for an N-dimension cast, so a large legal `x::int[][]...[]`
        // exhausted memory. A single build is linear.
        std::size_t array_dims = 0;
        while (current_token_ && current_token_->value == "[") {
            advance();  // consume [
            if (current_token_ &&
                current_token_->type == tokenizer::TokenType::Number) {
                advance();  // optional array size, ignored (as in DDL)
            }
            if (current_token_ && current_token_->value == "]") {
                advance();  // consume ]
                ++array_dims;
            } else {
                break;  // malformed: leave it for the caller / error path
            }
        }
        if (array_dims > 0) {
            type_node->flags = static_cast<ast::NodeFlags>(
                static_cast<uint8_t>(type_node->flags) | 0x80);  // array-type flag
            std::string type_text{type_node->primary_text};
            type_text.reserve(type_text.size() + array_dims * 2);
            for (std::size_t i = 0; i < array_dims; ++i) {
                type_text += "[]";
            }
            type_node->primary_text = copy_to_arena(type_text);
        }

        operand->parent = cast_node;
        cast_node->first_child = operand;
        type_node->parent = cast_node;
        operand->next_sibling = type_node;
        cast_node->child_count = 2;
        operand = cast_node;
    }
    return operand;
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
    
    // Parse the temporal expression as a full expression, terminated by the
    // closing ')'. It may be a column, a qualified column, a niladic datetime
    // function (CURRENT_DATE), a function call, an arithmetic expression, or a
    // TYPED LITERAL - DATE '2020-01-01' / INTERVAL '3 days'. The previous
    // column-only operand parser took the leading type keyword of a typed literal
    // as a bare column and left the literal dangling, so a legal
    // `EXTRACT(YEAR FROM DATE '2020-01-01')` failed to parse.
    ast::ASTNode* temporal_expr = nullptr;
    if (current_token_ && current_token_->value != ")") {
        temporal_expr = parse_expression(0);
        if (!temporal_expr) {
            return nullptr;
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