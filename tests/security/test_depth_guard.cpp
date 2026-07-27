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

// Generate deeply nested parenthesised join groups in FROM:
// SELECT 1 FROM (((( ... t ... )))). Each '(' whose next token begins a table
// reference is a join group and recurses parse_from_clause -> parse_table_reference.
std::string generate_nested_from_groups(int depth) {
    std::stringstream sql;
    sql << "SELECT 1 FROM ";
    for (int i = 0; i < depth; ++i) {
        sql << "(";
    }
    sql << "t";
    for (int i = 0; i < depth; ++i) {
        sql << ")";
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

// Generate deeply nested CREATE TRIGGER with a single-statement body:
// CREATE TRIGGER ... CREATE TRIGGER ... (x depth) SELECT 1. Each level recurses
// parse_create_trigger -> parse_statement -> parse_create_stmt -> parse_create_trigger.
std::string generate_nested_triggers_single(int depth) {
    std::stringstream sql;
    for (int i = 0; i < depth; ++i) {
        sql << "CREATE TRIGGER x AFTER INSERT ON t ";
    }
    sql << "SELECT 1";
    return sql.str();
}

// Same recursion, but through the BEGIN ... END trigger-body form.
std::string generate_nested_triggers_block(int depth) {
    std::stringstream sql;
    for (int i = 0; i < depth; ++i) {
        sql << "CREATE TRIGGER x AFTER INSERT ON t BEGIN ";
    }
    sql << "SELECT 1";
    for (int i = 0; i < depth; ++i) {
        sql << " END";
    }
    return sql.str();
}

}  // namespace

// A shallow chain of nested triggers stays under the limit and must parse. This
// also confirms the recursion is real (each CREATE TRIGGER body can itself be a
// statement), i.e. the deep cases below exercise a genuine recursion vector.
TEST(DepthGuard, ShallowNestedTriggersParse) {
    Parser parser;
    ASSERT_TRUE(parser.parse(generate_nested_triggers_single(3)).has_value());
    ASSERT_TRUE(parser.parse(generate_nested_triggers_block(3)).has_value());
}

// Deeply nested CREATE TRIGGER (single-statement body) previously drove unbounded
// native recursion and overflowed the stack: every DDL entry point wrote its
// DepthGuard as an if-init-statement, so the guard object was destroyed at the
// end of the `if` and depth never accumulated. It must now be rejected gracefully.
TEST(DepthGuard, DeeplyNestedTriggersSingleRejected) {
    Parser parser;
    const size_t limit = parser.config().max_depth;
    auto result = parser.parse(generate_nested_triggers_single(static_cast<int>(limit) * 3));
    ASSERT_FALSE(result.has_value())
        << "Deeply nested CREATE TRIGGER must be rejected by the DepthGuard";
    EXPECT_EQ(result.error().message, kDepthError);
}

// Deeply nested CREATE TRIGGER through the BEGIN ... END body form. Besides the
// same defeated-guard stack overflow, this path had a second hazard: the body
// loop re-called parse_statement() on a token it could not consume without ever
// advancing, so once the depth guard returned nullptr the crash degraded into an
// infinite loop. It must terminate and reject gracefully.
TEST(DepthGuard, DeeplyNestedTriggersBlockRejected) {
    Parser parser;
    const size_t limit = parser.config().max_depth;
    auto result = parser.parse(generate_nested_triggers_block(static_cast<int>(limit) * 3));
    ASSERT_FALSE(result.has_value())
        << "Deeply nested BEGIN..END triggers must be rejected by the DepthGuard";
    EXPECT_EQ(result.error().message, kDepthError);
}

// A stray, unparseable token inside a BEGIN ... END trigger body must not hang.
// The body loop calls parse_statement(), which returns nullptr on '@'; without a
// forward-progress guard the loop spins on the same token forever. Completion of
// this test (it does not time out) is the assertion; the parser must also stay
// usable afterwards.
TEST(DepthGuard, GarbageInTriggerBodyTerminates) {
    Parser parser;
    (void)parser.parse("CREATE TRIGGER x AFTER INSERT ON t BEGIN @ END");
    // Reusable after the (previously hanging) input.
    auto ok = parser.parse("SELECT * FROM users WHERE id = 1");
    ASSERT_TRUE(ok.has_value())
        << "Parser must remain usable after a malformed trigger body";
}

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

// A modestly nested parenthesised FROM join group stays under the limit and parses.
TEST(DepthGuard, ShallowNestedFromGroupParses) {
    Parser parser;
    auto result = parser.parse(generate_nested_from_groups(5));
    ASSERT_TRUE(result.has_value())
        << "Shallow FROM join group should parse, got error: " << result.error().message;
}

// Deeply nested parenthesised FROM join groups (`FROM ((((...`) previously drove
// unbounded native recursion (parse_table_reference <-> parse_from_clause) and
// overflowed the stack. They must now be rejected gracefully by the DepthGuard.
TEST(DepthGuard, DeeplyNestedFromGroupsRejected) {
    Parser parser;
    const size_t limit = parser.config().max_depth;
    const std::string sql = generate_nested_from_groups(static_cast<int>(limit) * 3);

    auto result = parser.parse(sql);
    ASSERT_FALSE(result.has_value())
        << "Deeply nested FROM join groups must be rejected by the DepthGuard";
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
