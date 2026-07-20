/*
 * DepthGuard security test.
 *
 * Verifies that the parser's recursion DepthGuard protects against stack
 * overflow from maliciously (or accidentally) deep nesting. These are real,
 * gating assertions: deeply nested inputs MUST be rejected with a graceful
 * error Result rather than crashing or overflowing the stack, custom depth
 * limits MUST be honoured, and the parser MUST remain usable after a depth
 * failure.
 */

#include <string>
#include <sstream>

#include <gtest/gtest.h>

#include "db25/parser/parser.hpp"

using namespace db25;
using namespace db25::parser;

namespace {

// The error message emitted by the guard when the limit is exceeded.
constexpr const char* kDepthError = "Maximum recursion depth exceeded";

// Generate deeply nested subqueries: SELECT * FROM (SELECT * FROM (... SELECT 1 ...))
std::string generate_nested_subqueries(int depth) {
    std::stringstream sql;
    for (int i = 0; i < depth; ++i) {
        sql << "SELECT * FROM (";
    }
    sql << "SELECT 1";
    for (int i = 0; i < depth; ++i) {
        sql << ") AS t" << i;
    }
    return sql.str();
}

// Generate deeply nested parenthesised expressions: SELECT (1 + (1 + ( ... 1 ... )))
std::string generate_nested_expr(int depth) {
    std::stringstream sql;
    sql << "SELECT ";
    for (int i = 0; i < depth; ++i) {
        sql << "(1 + ";
    }
    sql << "1";
    for (int i = 0; i < depth; ++i) {
        sql << ")";
    }
    sql << " AS result";
    return sql.str();
}

}  // namespace

// A modestly nested query stays well under the default limit and must parse.
TEST(DepthGuard, ShallowNestedQueryParses) {
    Parser parser;
    const std::string sql = generate_nested_subqueries(10);
    auto result = parser.parse(sql);
    ASSERT_TRUE(result.has_value())
        << "Shallow query should parse, got error: " << result.error().message;
}

// Deeply nested subqueries that exceed the default depth limit must be
// rejected gracefully (error Result, no crash, no stack overflow).
TEST(DepthGuard, DeeplyNestedSubqueriesRejected) {
    Parser parser;
    const size_t limit = parser.config().max_depth;
    const std::string sql = generate_nested_subqueries(static_cast<int>(limit) * 3);

    auto result = parser.parse(sql);
    ASSERT_FALSE(result.has_value())
        << "Deeply nested subqueries must be rejected by the DepthGuard";
    EXPECT_EQ(result.error().message, kDepthError);
}

// Deeply nested parenthesised expressions must likewise be rejected gracefully.
TEST(DepthGuard, DeeplyNestedExpressionsRejected) {
    Parser parser;
    const size_t limit = parser.config().max_depth;
    const std::string sql = generate_nested_expr(static_cast<int>(limit) * 3);

    auto result = parser.parse(sql);
    ASSERT_FALSE(result.has_value())
        << "Deeply nested expressions must be rejected by the DepthGuard";
    EXPECT_EQ(result.error().message, kDepthError);
}

// A custom (lower) depth limit must be honoured: inputs within the limit parse,
// inputs beyond it are rejected.
TEST(DepthGuard, CustomDepthLimitHonored) {
    Parser parser;
    ParserConfig config = parser.config();
    config.max_depth = 50;
    parser.set_config(config);

    // Comfortably within the custom limit.
    {
        auto ok = parser.parse(generate_nested_subqueries(10));
        ASSERT_TRUE(ok.has_value())
            << "Query within custom limit should parse, got: " << ok.error().message;
    }

    // Well beyond the custom limit.
    {
        auto bad = parser.parse(generate_nested_subqueries(200));
        ASSERT_FALSE(bad.has_value())
            << "Query beyond custom limit must be rejected";
        EXPECT_EQ(bad.error().message, kDepthError);
    }
}

// After a depth failure the parser must reset cleanly and remain reusable.
TEST(DepthGuard, ParserReusableAfterDepthFailure) {
    Parser parser;
    const size_t limit = parser.config().max_depth;

    auto deep = parser.parse(generate_nested_subqueries(static_cast<int>(limit) * 3));
    ASSERT_FALSE(deep.has_value());
    ASSERT_EQ(deep.error().message, kDepthError);

    // The parser must recover and parse an ordinary query.
    auto ok = parser.parse("SELECT * FROM users WHERE id = 1");
    ASSERT_TRUE(ok.has_value())
        << "Parser must be reusable after a depth failure, got: "
        << ok.error().message;
}
