/*
 * Copyright (c) 2024 DB25 Parser Project
 * 
 * Internal header for parser module refactoring
 * Provides shared utilities and friend class access
 */

#pragma once

#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include "db25/parser/tokenizer_adapter.hpp"

namespace db25::parser::internal {

// Select the correct literal node type for a numeric token.
// A Number token is a float if its text carries a decimal point or an
// exponent marker; otherwise it is an integer.
inline ast::NodeType number_literal_type(std::string_view text) {
    for (const char c : text) {
        if (c == '.' || c == 'e' || c == 'E') {
            return ast::NodeType::FloatLiteral;
        }
    }
    return ast::NodeType::IntegerLiteral;
}

} // namespace db25::parser::internal