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

// An incomplete BETWEEN / LIKE / ILIKE / IS predicate must be a syntax error, not
// silently accepted with the consumed operator dropped and the following clause
// stitched onto a corrupted statement. `WHERE x BETWEEN 1 GROUP BY y` used to
// yield WhereClause[ColumnRef 'x'] (a bare truthiness test) plus a phantom GROUP BY.
TEST(ParserNegativeTest, IncompleteBetweenRejected) {
    expect_parse_error("SELECT * FROM t WHERE x BETWEEN 1 GROUP BY y");
    expect_parse_error("SELECT * FROM t WHERE x BETWEEN 1");            // ends after low bound
    expect_parse_error("SELECT * FROM t WHERE x BETWEEN 1 AND");        // ends after AND
    expect_parse_error("SELECT * FROM t WHERE x NOT BETWEEN 1 ORDER BY y");
}

TEST(ParserNegativeTest, IncompleteLikeRejected) {
    expect_parse_error("SELECT * FROM t WHERE a LIKE GROUP BY y");
    expect_parse_error("SELECT * FROM t WHERE a ILIKE ORDER BY y");
    expect_parse_error("SELECT * FROM t WHERE a LIKE");
}

TEST(ParserNegativeTest, IncompleteIsRejected) {
    expect_parse_error("SELECT * FROM t WHERE x IS GROUP BY y");
    expect_parse_error("SELECT * FROM t WHERE x IS");
    expect_parse_error("SELECT * FROM t WHERE x IS NOT LIMIT 1");
}

// A `::type` cast whose type name is a reserved clause keyword (or is missing)
// must be a syntax error, not silently accepted with the clause keyword swallowed
// as a bogus type and the clause deleted. `SELECT a:: FROM t` used to yield
// CastExpr[a, Identifier 'FROM'] with no FromClause.
TEST(ParserNegativeTest, CastToClauseKeywordRejected) {
    expect_parse_error("SELECT a:: FROM t");
    expect_parse_error("SELECT a:: WHERE b");
    expect_parse_error("SELECT a:: HAVING c FROM t");
    expect_parse_error("SELECT a::");                 // missing type at end of input
    expect_parse_error("SELECT a::int:: FROM t");     // chained cast, second is empty
}

// A COLLATE whose collation name is a reserved clause keyword (or is missing) is
// likewise rejected, not silently accepted with the clause keyword swallowed.
TEST(ParserNegativeTest, CollateToClauseKeywordRejected) {
    expect_parse_error("SELECT a COLLATE FROM t");
    expect_parse_error("SELECT a COLLATE WHERE b");
    expect_parse_error("SELECT a COLLATE");           // missing collation name
}

// A set operator (UNION/INTERSECT/EXCEPT) or other clause keyword after `::`,
// COLLATE, or a binary operator must be a syntax error, not swallowed as a bogus
// type/collation/operand that deletes the following query branch. `SELECT a::
// UNION SELECT b` used to parse to a single SELECT with UNION absorbed as the
// cast type.
TEST(ParserNegativeTest, SetOpKeywordNotAbsorbedAsOperand) {
    expect_parse_error("SELECT a:: UNION SELECT b FROM t");
    expect_parse_error("SELECT a + UNION SELECT b");
    expect_parse_error("SELECT a COLLATE INTERSECT SELECT b");
    expect_parse_error("SELECT a:: EXCEPT SELECT b");
    expect_parse_error("SELECT a:: WINDOW w AS ()");
}

// Guard: a VALID set operation still parses (the guard only fires when the set
// operator is in operand / type / collation position, never as the real
// branch separator).
TEST(ParserNegativeTest, ValidSetOperationsStillParse) {
    Parser parser;
    EXPECT_TRUE(parser.parse("SELECT a::int FROM t UNION SELECT b FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT a FROM t INTERSECT SELECT b FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT a FROM t EXCEPT SELECT b FROM t").has_value());
}

// An unclosed `::type(...)` cast type-parameter list must be a syntax error, not
// silently accepted by swallowing the rest of the statement (clause keywords
// included) as "type parameters". `SELECT x::VARCHAR(10 FROM t` used to parse to
// a CastExpr with FROM/WHERE deleted.
TEST(ParserNegativeTest, UnclosedCastTypeParamsRejected) {
    expect_parse_error("SELECT x::VARCHAR(10 FROM t WHERE a = 1");
    expect_parse_error("SELECT x::int(1 FROM t");
    expect_parse_error("SELECT a FROM t WHERE b = x::int(1 AND c > 5 GROUP BY d");
    expect_parse_error("SELECT x::DECIMAL(10, 2 FROM t");
}

// An unclosed `::type[...]` array-type suffix (the array sibling of the `(...)`
// type-parameter list above) must be a syntax error, not silently dropped. The
// `::` shorthand had no closing delimiter to catch it, so `SELECT x::int[3 FROM
// t` consumed the `[3` and continued as a plain `::int` cast, deleting the
// tokens; a malformed cast inside a function argument (`f(x::int[9 , y)`) was
// swallowed by the argument loop resyncing on the comma. The full CAST form is
// covered too: `CAST(x AS int[3)` used to drop `[3` and close on the `)`.
TEST(ParserNegativeTest, UnclosedCastArraySuffixRejected) {
    expect_parse_error("SELECT x::int[3 FROM t WHERE a = 1");
    expect_parse_error("SELECT x::int[ FROM t");
    expect_parse_error("SELECT x::int[3");                  // at EOF
    expect_parse_error("SELECT f(x::int[9 , y) FROM t");    // malformed cast in a fn arg
    expect_parse_error("SELECT f(x::int[9) FROM t");
    expect_parse_error("SELECT CAST(x AS int[3) FROM t");   // full CAST form
    expect_parse_error("SELECT CAST(x AS int[3 FROM t");
}

// Guards: well-formed array-type casts (sized, empty, multi-dimensional) in both
// the `::` shorthand and the full CAST form still parse.
TEST(ParserNegativeTest, ValidArrayCastsStillParse) {
    Parser parser;
    EXPECT_TRUE(parser.parse("SELECT x::int[3] FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT x::int[] FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT x::int[][] FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT f(x::int[9], y) FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT CAST(x AS int[3]) FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT CAST(x AS int[]) FROM t").has_value());
}

// Guards: real `::type` casts and COLLATE names (which ARE keywords for the
// types, but not clause keywords) still parse - including parameterized types.
TEST(ParserNegativeTest, RealCastAndCollateStillParse) {
    Parser parser;
    EXPECT_TRUE(parser.parse("SELECT a::int FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT a::TIMESTAMP, b::DATE FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT a::int::text FROM t WHERE b > 0").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT a::VARCHAR(100) FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT a::DECIMAL(10, 2) FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT name COLLATE \"C\" FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT a::int COLLATE \"C\" FROM t").has_value());
}

// Guards: well-formed BETWEEN / LIKE / IS predicates still parse.
TEST(ParserNegativeTest, CompletePredicatesStillParse) {
    Parser parser;
    EXPECT_TRUE(parser.parse("SELECT * FROM t WHERE x BETWEEN 1 AND 10").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT * FROM t WHERE x NOT BETWEEN 1 AND 10 GROUP BY y").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT * FROM t WHERE a LIKE '%x%' ORDER BY a").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT * FROM t WHERE x IS NULL").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT * FROM t WHERE x IS NOT NULL AND y IS TRUE").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT * FROM t WHERE x IS DISTINCT FROM y").has_value());
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
