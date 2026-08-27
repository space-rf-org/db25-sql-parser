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

// A binary operator whose right operand is a clause-introducing keyword must be
// a syntax error - NOT silently accepted with the keyword absorbed as a column
// and the real clause deleted. `SELECT a + FROM t` used to yield BinaryExpr('+',
// a, ColumnRef 'FROM') with no FROM clause.
TEST(ParserNegativeTest, BinaryOperatorWithClauseKeywordOperand) {
    expect_parse_error("SELECT a + FROM t");
    expect_parse_error("SELECT a * WHERE x");
    expect_parse_error("SELECT a || ORDER BY b");
    expect_parse_error("SELECT a + GROUP BY b");
    expect_parse_error("SELECT a - LIMIT 1");
    // A trailing binary operator with no operand at all is likewise rejected.
    expect_parse_error("SELECT a + FROM t WHERE b");
}

// The unsupported PostgreSQL JSON access operators `->` / `->>` are rejected,
// not silently truncated (which dropped the trailing FROM/WHERE).
TEST(ParserNegativeTest, JsonArrowOperatorsRejected) {
    expect_parse_error("SELECT data->'key' FROM t");
    expect_parse_error("SELECT data->>'key' FROM t WHERE id = 5");
}

// Guards: legal binary minus / arithmetic and a legal negative literal still
// parse - the reserved-keyword-operand and missing-RHS guards must not reject
// well-formed expressions.
TEST(ParserNegativeTest, LegalArithmeticStillParses) {
    Parser parser;
    EXPECT_TRUE(parser.parse("SELECT a - b FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT a - 1, a + b * c FROM t WHERE a > b").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT -a, a - -1 FROM t").has_value());
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

// `DISTINCT ON (...)` (PostgreSQL) is unsupported and must be rejected, not
// silently mis-parsed: previously `ON (a)` was read as a function call `ON(a)`
// that took the first output slot and swallowed the intended first column `a`
// as its alias, dropping a projected column.
TEST(ParserNegativeTest, DistinctOnUnsupported) {
    expect_parse_error("SELECT DISTINCT ON (a) a, b FROM t");
}

// Plain DISTINCT (no ON) is unaffected and still parses.
TEST(ParserNegativeTest, PlainDistinctStillParses) {
    Parser parser;
    EXPECT_TRUE(parser.parse("SELECT DISTINCT a, b FROM t").has_value());
}

// WITHIN GROUP (ORDER BY ...) - an ordered-set aggregate - is not yet supported
// and MUST be rejected cleanly, not silently mis-parsed. Before the fix the
// parser returned "success" with an AST of only `SELECT percentile_cont(0.5)`,
// silently discarding WITHIN GROUP and every following clause (FROM/WHERE).
TEST(ParserNegativeTest, WithinGroupRejected) {
    expect_parse_error("SELECT percentile_cont(0.5) WITHIN GROUP (ORDER BY x) FROM t");
    expect_parse_error("SELECT percentile_disc(0.5) WITHIN GROUP (ORDER BY x) FROM t WHERE y > 0");
    expect_parse_error("SELECT mode() WITHIN GROUP (ORDER BY x) FROM t");
}

// Guard: the fix sits on the aggregate-postfix path, so ordinary calls and the
// FILTER / OVER postfixes must still parse (no over-rejection).
TEST(ParserNegativeTest, WithinGroupFixDoesNotBreakNormalCalls) {
    Parser parser;
    EXPECT_TRUE(parser.parse("SELECT count(*) FROM t WHERE y > 0").has_value());
    EXPECT_TRUE(parser.parse("SELECT count(*) FILTER (WHERE y > 0) FROM t").has_value());
    EXPECT_TRUE(parser.parse("SELECT sum(x) OVER (PARTITION BY g) FROM t").has_value());
}
