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
// exponent marker; otherwise it is an integer. A hex (0x..) or binary (0b..)
// literal is always an integer - its digits can include 'e'/'E' (a hex digit,
// as in 0xBEEF), which must not be mistaken for a float's exponent marker.
inline ast::NodeType number_literal_type(std::string_view text) {
    std::string_view digits = text;
    if (!digits.empty() && (digits.front() == '-' || digits.front() == '+')) {
        digits.remove_prefix(1);
    }
    if (digits.size() >= 2 && digits[0] == '0' &&
        (digits[1] == 'x' || digits[1] == 'X' ||
         digits[1] == 'b' || digits[1] == 'B')) {
        return ast::NodeType::IntegerLiteral;
    }
    for (const char c : text) {
        if (c == '.' || c == 'e' || c == 'E') {
            return ast::NodeType::FloatLiteral;
        }
    }
    return ast::NodeType::IntegerLiteral;
}

} // namespace db25::parser::internal