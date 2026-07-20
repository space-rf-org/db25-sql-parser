/*
 * Negative-path parser tests.
 *
 * Feeds malformed / incomplete SQL and asserts that Parser::parse() returns an
 * error Result (has_value() == false) without crashing. Running under the
 * ASan/UBSan CI job additionally guarantees these inputs trigger no memory or
 * undefined-behavior errors on the failure paths.
 *
 * NOTE: the DB25 parser is deliberately LENIENT about certain malformed inputs
 * (e.g. trailing commas in a SELECT list, a stray word treated as a table
 * alias such as `SELECT * FORM t`, or an empty GROUP BY list). Those are NOT
 * asserted here because current, intentional behavior is to accept them and
 * still return an AST. This suite pins only the inputs that genuinely fail.
 */

#include <gtest/gtest.h>

#include <string>

#include "db25/parser/parser.hpp"

using namespace db25::parser;

namespace {

// Parse `sql` and assert it fails cleanly (error Result, no crash).
void expect_parse_error(const std::string& sql) {
    Parser parser;
    auto result = parser.parse(sql);
    EXPECT_FALSE(result.has_value()) << "expected parse error for: [" << sql << "]";
}

}  // namespace

// Empty / whitespace-only input is not a statement.
TEST(ParserNegativeTest, EmptyInput) {
    expect_parse_error("");
}

TEST(ParserNegativeTest, WhitespaceOnlyInput) {
    expect_parse_error("   \t\n  ");
}

// A lone statement terminator has no statement.
TEST(ParserNegativeTest, LoneSemicolon) {
    expect_parse_error(";");
}

// Missing select list: FROM immediately after SELECT.
TEST(ParserNegativeTest, SelectMissingSelectList) {
    expect_parse_error("SELECT FROM t");
}

// Dangling WHERE with no predicate.
TEST(ParserNegativeTest, DanglingWhere) {
    expect_parse_error("SELECT * FROM t WHERE");
}

// WHERE that starts with a binary operator (nothing on the left).
TEST(ParserNegativeTest, WhereStartsWithAnd) {
    expect_parse_error("WHERE AND x");
}

// Unbalanced parentheses in a predicate.
TEST(ParserNegativeTest, UnbalancedParensInPredicate) {
    expect_parse_error("SELECT * FROM t WHERE (a AND b");
}

// Unbalanced parentheses in a projection expression.
TEST(ParserNegativeTest, UnbalancedParensInProjection) {
    expect_parse_error("SELECT (1 + 2 FROM t");
}

// INSERT with no target table / values.
TEST(ParserNegativeTest, IncompleteInsert) {
    expect_parse_error("INSERT INTO");
}

// UPDATE with no target table before SET.
TEST(ParserNegativeTest, UpdateMissingTable) {
    expect_parse_error("UPDATE SET x = 1");
}

// DELETE without FROM.
TEST(ParserNegativeTest, DeleteMissingFrom) {
    expect_parse_error("DELETE t");
}

// Pure garbage that begins with a non-statement identifier.
TEST(ParserNegativeTest, GarbageInput) {
    expect_parse_error("GARBAGE tokens here");
}
