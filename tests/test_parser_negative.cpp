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

// A malformed sub-expression inside a grouping element (ROLLUP/CUBE/GROUPING
// SETS) or a VALUES row must FAIL the parse, not be silently dropped. Those
// paths use break-on-null recovery loops that returned the enclosing node while
// error() had already recorded a hard error - and parse() did not consult
// has_error_ when the root was non-null, so `INSERT INTO t VALUES (1 + )` parsed
// to an INSERT of an EMPTY row and `GROUP BY ROLLUP(a + )` to a childless ROLLUP.
// The same fragments are (correctly) rejected in the ordinary expression path.
TEST(ParserNegativeTest, SwallowedErrorInGroupingElementOrValuesRow) {
    // VALUES rows (bare and under INSERT).
    expect_parse_error("INSERT INTO t VALUES (1 + )");
    expect_parse_error("VALUES (1 + )");
    expect_parse_error("VALUES (a BETWEEN 1)");
    // ROLLUP / CUBE / GROUPING SETS elements.
    expect_parse_error("SELECT x FROM t GROUP BY ROLLUP(a + )");
    expect_parse_error("SELECT x FROM t GROUP BY ROLLUP(a IS)");
    expect_parse_error("SELECT x FROM t GROUP BY CUBE(a + )");
    expect_parse_error("SELECT x FROM t GROUP BY GROUPING SETS ((a IS))");
}

// Guards: well-formed grouping elements and VALUES rows still parse.
TEST(ParserNegativeTest, ValidGroupingElementsAndValuesRowsStillParse) {
    Parser parser;
    EXPECT_TRUE(parser.parse("INSERT INTO t VALUES (1 + 2)").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("VALUES (1, 2)").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT x FROM t GROUP BY ROLLUP(a, b)").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT x FROM t GROUP BY CUBE(a, b)").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT x FROM t GROUP BY GROUPING SETS ((a), (b))").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT x FROM t GROUP BY ()").has_value());
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

// An unclosed ARRAY[...] constructor must FAIL, not be silently truncated. Its
// element loop is a break-on-null recovery loop that neither called error() nor
// required the closing ']', and '[' is not tracked by the parenthesis-balance
// check - so `SELECT ARRAY[1 2] FROM t WHERE ...` dropped the second element AND
// the whole FROM/WHERE tail and parsed as success. Sibling of the VALUES/ROLLUP
// row loops and the ::type[...] cast-array suffix.
TEST(ParserNegativeTest, UnclosedArrayConstructorRejected) {
    expect_parse_error("SELECT ARRAY[1 2] FROM t WHERE y > 5");  // missing comma
    expect_parse_error("SELECT ARRAY[1 2]");                     // at EOF
    expect_parse_error("SELECT ARRAY[1, 2 FROM t");              // unclosed
    expect_parse_error("SELECT ARRAY[1, FROM] FROM t");          // clause kw element
    expect_parse_error("SELECT ARRAY[1, 2+] FROM t");            // malformed element
}

// Guards: well-formed ARRAY constructors - multi-element, empty, trailing comma,
// with a following clause - still parse.
TEST(ParserNegativeTest, ValidArrayConstructorsStillParse) {
    Parser parser;
    EXPECT_TRUE(parser.parse("SELECT ARRAY[1, 2, 3] FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT ARRAY[] FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT ARRAY[1, 2] FROM t WHERE y > 5").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT ARRAY[a, b] FROM t").has_value());
}

// More break-on-null recovery loops that silently truncated a statement while
// parse() returned success, none caught by the has_error_ backstop (they never
// called error()) nor the paren-balance check:
//   - a comma-separated FROM item that fails to parse (an unsupported form the
//     FROM parser emits no node for, e.g. comma-form LATERAL) was dropped and
//     the tail left as trailing tokens;
//   - a set operator (UNION/INTERSECT/EXCEPT) whose right query fails to parse
//     was erased, yielding the bare left arm;
//   - an UPDATE ... SET assignment with a missing value after `=` emitted a
//     value-less assignment node.
TEST(ParserNegativeTest, SilentlyTruncatedFromSetopUpdateRejected) {
    // FROM item after a comma.
    expect_parse_error("SELECT * FROM users u, LATERAL (SELECT 1) l");
    expect_parse_error("SELECT * FROM a, WHERE x");
    expect_parse_error("SELECT * FROM a,");
    // Set-operator right-hand side (UNION / INTERSECT / EXCEPT, incl. chains).
    expect_parse_error("SELECT a FROM t UNION");
    expect_parse_error("SELECT a FROM t UNION SELECT");
    expect_parse_error("SELECT a FROM t INTERSECT");
    expect_parse_error("SELECT a FROM t EXCEPT ALL");
    expect_parse_error("SELECT 1 UNION SELECT 2 UNION");
    // UPDATE ... SET value.
    expect_parse_error("UPDATE t SET a = WHERE id = 1");
    expect_parse_error("UPDATE t SET a = 1, b = WHERE id = 1");
    expect_parse_error("UPDATE t SET a =");
}

// A WHEN branch of a CASE expression whose condition parses but is not followed
// by THEN silently dropped the parsed condition (the THEN block was the ONLY
// place condition/result got attached) and emitted a childless WHEN node while
// parse() returned success -- another break-on-null / missing-required-keyword
// truncation not caught by the has_error_ backstop. THEN is mandatory.
TEST(ParserNegativeTest, CaseWhenMissingThenRejected) {
    // Searched CASE, single THEN-less branch.
    expect_parse_error("SELECT CASE WHEN x END FROM t");
    // Simple CASE, THEN-less branch drops the WHEN value.
    expect_parse_error("SELECT CASE x WHEN 1 END FROM t");
    // Mid-list THEN-less branch after a well-formed one.
    expect_parse_error("SELECT CASE WHEN a THEN 1 WHEN b END FROM t");
    // THEN-less branch followed by ELSE.
    expect_parse_error("SELECT CASE WHEN x ELSE 0 END FROM t");
}

// A prefix unary operator (NOT / unary - / unary +) with no operand -- a binary
// operator or clause keyword sitting where the operand belongs -- previously
// built a CHILDLESS UnaryExpr that parse() returned success for; the analyzer
// then blessed it clean and the binder could not lower it. The operand is
// mandatory.
TEST(ParserNegativeTest, UnaryOperatorMissingOperandRejected) {
    expect_parse_error("SELECT NOT > 5 FROM t");
    expect_parse_error("SELECT NOT = 5 FROM t");
    expect_parse_error("SELECT - > 5 FROM t");
    expect_parse_error("SELECT + > 5 FROM t");
    expect_parse_error("SELECT id FROM t WHERE age > 1 AND NOT > 2");
    expect_parse_error("SELECT CASE WHEN NOT > 1 THEN 1 ELSE 0 END FROM t");
}

// Guard: well-formed unary operators still parse.
TEST(ParserNegativeTest, ValidUnaryOperatorsStillParse) {
    Parser parser;
    EXPECT_TRUE(parser.parse("SELECT NOT (a > 5) FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT NOT a FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT -a, +b, -5 FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT * FROM t WHERE NOT EXISTS (SELECT 1)").has_value());
}

// ON CONFLICT DO UPDATE SET, the ON CONFLICT conflict target, and RETURNING all
// broke on a missing required element without error(): DO UPDATE SET accepted an
// empty or value-less assignment list, the conflict-target loop silently skipped
// a non-identifier (fabricating a different column list), and RETURNING accepted
// an empty output list. All must be rejected.
TEST(ParserNegativeTest, UpsertReturningTruncationRejected) {
    // DO UPDATE SET: empty, missing '=' value, and value-less then a clause kw.
    expect_parse_error("INSERT INTO t VALUES (1,2) ON CONFLICT (a) DO UPDATE SET");
    expect_parse_error("INSERT INTO t VALUES (1,2) ON CONFLICT (a) DO UPDATE SET b");
    expect_parse_error("INSERT INTO t VALUES (1,2) ON CONFLICT (a) DO UPDATE SET b WHERE x = 1");
    expect_parse_error("INSERT INTO t VALUES (1,2) ON CONFLICT (a) DO UPDATE SET b =");
    // ON CONFLICT conflict target: an index-expression target is not a column
    // list and must not be silently split into separate columns.
    expect_parse_error("INSERT INTO t VALUES (1,2) ON CONFLICT (a + b) DO NOTHING");
    expect_parse_error("INSERT INTO t VALUES (1,2) ON CONFLICT (lower(a)) DO NOTHING");
    // RETURNING with no output list (INSERT helper, inline UPDATE, inline DELETE).
    expect_parse_error("INSERT INTO t DEFAULT VALUES RETURNING");
    expect_parse_error("UPDATE t SET a = 1 RETURNING");
    expect_parse_error("DELETE FROM t WHERE a = 1 RETURNING");
}

// Guard: well-formed UPSERT and RETURNING forms still parse.
TEST(ParserNegativeTest, ValidUpsertReturningStillParse) {
    Parser parser;
    EXPECT_TRUE(parser.parse(
        "INSERT INTO t VALUES (1,2) ON CONFLICT (a) DO UPDATE SET b = 2").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse(
        "INSERT INTO t VALUES (1,2) ON CONFLICT (a, c) DO UPDATE SET b = 2, d = 3 WHERE b > 0")
                    .has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("INSERT INTO t VALUES (1,2) ON CONFLICT (a) DO NOTHING").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("INSERT INTO t DEFAULT VALUES RETURNING *").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("UPDATE t SET a = 1 RETURNING a, b").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("DELETE FROM t WHERE a = 1 RETURNING *").has_value());
}

// More missing-required-element / swallowed-token sites that returned a clean
// parse on a truncated construct: CAST without a target type, EXTRACT without a
// source expression, ORDER BY ... NULLS without FIRST/LAST, LIKE ... ESCAPE
// without an escape character, and an empty IN value list.
TEST(ParserNegativeTest, MoreMissingElementsRejected) {
    // CAST(value AS <type>) - the type is mandatory.
    expect_parse_error("SELECT CAST(1 AS)");
    expect_parse_error("SELECT CAST(1 AS) FROM t WHERE y = 2");
    // EXTRACT(field FROM <source>) - the source is mandatory.
    expect_parse_error("SELECT EXTRACT(YEAR FROM)");
    expect_parse_error("SELECT EXTRACT(YEAR FROM) FROM t");
    // ORDER BY ... NULLS { FIRST | LAST } - the direction word is mandatory.
    expect_parse_error("SELECT 1 ORDER BY x NULLS");
    expect_parse_error("SELECT 1 ORDER BY x NULLS BOGUS");
    expect_parse_error("SELECT 1 ORDER BY x, y NULLS");
    // LIKE ... ESCAPE <char> - the escape character is mandatory.
    expect_parse_error("SELECT a LIKE 'x' ESCAPE");
    expect_parse_error("SELECT * FROM t WHERE a LIKE 'x' ESCAPE ORDER BY a");
    // IN (...) - the value list must be non-empty.
    expect_parse_error("SELECT a IN ()");
    expect_parse_error("SELECT a IN (,)");
    expect_parse_error("SELECT a NOT IN ()");
}

// Guard: the well-formed variants of all of the above still parse.
TEST(ParserNegativeTest, ValidCastExtractNullsEscapeInStillParse) {
    Parser parser;
    EXPECT_TRUE(parser.parse("SELECT CAST(1 AS int)").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT CAST(x AS VARCHAR(10)) FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT EXTRACT(YEAR FROM d) FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT EXTRACT(YEAR FROM DATE '2020-01-01')").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT 1 ORDER BY x NULLS FIRST").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT 1 ORDER BY x DESC NULLS LAST, y NULLS FIRST").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT a LIKE 'x' ESCAPE '!' FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT a IN (1, 2, 3) FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT a IN (SELECT b FROM t) FROM u").has_value());
}

// More missing-required-source sites that silently accepted a mis-structured
// statement (all trailing_token_count==0 silent accepts): a FROM keyword with no
// table reference before a clause boundary, an INSERT with no data source, empty
// CTE / derived-table / scalar-subquery bodies, column-constraint keywords with
// no operand, and DROP / TRUNCATE with no object name.
TEST(ParserNegativeTest, MissingSourceElementsRejected) {
    // FROM with no table reference before a clause boundary / EOF.
    expect_parse_error("SELECT a FROM ORDER BY 1");
    expect_parse_error("SELECT a FROM WHERE b = 1");
    expect_parse_error("SELECT 1 FROM");
    expect_parse_error("SELECT a FROM GROUP BY b");
    // INSERT with no data source.
    expect_parse_error("INSERT INTO t (a, b)");
    expect_parse_error("INSERT INTO t");
    expect_parse_error("INSERT INTO t VALUES");
    expect_parse_error("INSERT INTO t DEFAULT");
    expect_parse_error("INSERT INTO t SELECT");
    // Empty CTE / derived-table / scalar-subquery bodies.
    expect_parse_error("WITH c AS () SELECT * FROM c");
    expect_parse_error("SELECT * FROM (SELECT) x");
    expect_parse_error("SELECT * FROM t WHERE a = (SELECT)");
    // Column-constraint keywords with no operand.
    expect_parse_error("CREATE TABLE t (a INT CHECK)");
    expect_parse_error("CREATE TABLE t (a INT DEFAULT)");
    expect_parse_error("CREATE TABLE t (a INT REFERENCES)");
    // DROP / TRUNCATE with no object name.
    expect_parse_error("DROP TABLE");
    expect_parse_error("DROP");
    expect_parse_error("TRUNCATE");
    expect_parse_error("TRUNCATE TABLE");
}

// Guard: the well-formed variants of all of the above still parse.
TEST(ParserNegativeTest, ValidSourceElementsStillParse) {
    Parser parser;
    EXPECT_TRUE(parser.parse("SELECT a FROM users ORDER BY a").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("INSERT INTO t (a, b) VALUES (1, 2)").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("INSERT INTO t DEFAULT VALUES").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("INSERT INTO t SELECT * FROM u").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("WITH c AS (SELECT 1) SELECT * FROM c").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT * FROM (SELECT 1) x").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT * FROM t WHERE a = (SELECT 1)").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("CREATE TABLE t (a INT CHECK (a > 0))").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("CREATE TABLE t (a INT DEFAULT 0)").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("CREATE TABLE t (a INT REFERENCES u (id))").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("DROP TABLE users").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("TRUNCATE TABLE users").has_value());
}

// Guard: well-formed CASE expressions (searched and simple, with/without ELSE)
// still parse.
TEST(ParserNegativeTest, ValidCaseExpressionsStillParse) {
    Parser parser;
    EXPECT_TRUE(parser.parse("SELECT CASE WHEN x THEN 1 END FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT CASE WHEN x THEN 1 ELSE 0 END FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT CASE x WHEN 1 THEN 'a' WHEN 2 THEN 'b' END FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT CASE x WHEN 1 THEN 'a' ELSE 'z' END FROM t").has_value());
}

// More break-on-null / missing-required-keyword silent-truncation sites, in the
// JOIN, aggregate-FILTER, and window-spec grammar. Each accepted a malformed
// input while parse() returned success, silently dropping a required element:
//   - JOIN ... ON with no condition -> conditionless (cartesian) join;
//   - JOIN ... USING with no column list, or an empty USING () list;
//   - aggregate FILTER with a missing/empty predicate -> the conditional
//     aggregate silently becomes unconditional (a wrong result);
//   - a window frame BETWEEN <start> with the required AND <end> missing;
//   - a window frame unit with no valid bound (OVER (RANGE), empty BETWEEN
//     start);
//   - OVER (PARTITION BY) with no partition expression -> a partitioned window
//     silently degrades to a whole-partition window.
TEST(ParserNegativeTest, SilentlyTruncatedJoinFilterWindowRejected) {
    // JOIN ON / USING.
    expect_parse_error("SELECT * FROM a JOIN b ON");
    expect_parse_error("SELECT * FROM a JOIN b USING");
    expect_parse_error("SELECT * FROM a JOIN b USING ()");
    // Aggregate FILTER.
    expect_parse_error("SELECT count(*) FILTER (WHERE) FROM t");
    expect_parse_error("SELECT count(*) FILTER () FROM t");
    expect_parse_error("SELECT count(*) FILTER FROM t");
    // Window frame.
    expect_parse_error("SELECT sum(x) OVER (ROWS BETWEEN CURRENT ROW) FROM t");
    expect_parse_error("SELECT sum(x) OVER (RANGE) FROM t");
    expect_parse_error("SELECT sum(x) OVER (ROWS BETWEEN AND CURRENT ROW) FROM t");
    // Window PARTITION BY.
    expect_parse_error("SELECT rank() OVER (PARTITION BY) FROM t");
}

// Guards: well-formed JOIN conditions, USING lists, aggregate FILTER clauses,
// and window frame/partition specs still parse.
TEST(ParserNegativeTest, ValidJoinFilterWindowStillParse) {
    Parser parser;
    EXPECT_TRUE(parser.parse("SELECT * FROM a JOIN b ON a.id = b.id").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT * FROM a JOIN b USING (id)").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT * FROM a JOIN b USING (id, x)").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT count(*) FILTER (WHERE amount > 0) FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT sum(x) OVER (ROWS BETWEEN 1 PRECEDING AND CURRENT ROW) FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT sum(x) OVER (ROWS UNBOUNDED PRECEDING) FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT rank() OVER (PARTITION BY dept ORDER BY x) FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT rank() OVER (ORDER BY x) FROM t").has_value());
}

// Guards: well-formed comma FROM lists, set operations, and UPDATE assignments
// still parse.
TEST(ParserNegativeTest, ValidFromSetopUpdateStillParse) {
    Parser parser;
    EXPECT_TRUE(parser.parse("SELECT * FROM a, b, c").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT a FROM t UNION SELECT b FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT a FROM t UNION ALL SELECT b FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT a FROM t INTERSECT SELECT b FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT 1 UNION SELECT 2 UNION SELECT 3").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("UPDATE t SET a = 1, b = 2 WHERE id = 1").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("UPDATE t SET a = b + 1").has_value());
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

// --- CREATE TABLE column-list defects (pass-29 audit) -----------------------
// The CREATE TABLE element loop previously "recovered" from an unexpected token
// by skipping to the next comma/paren, which silently swallowed invalid tokens
// and whole subsequent columns while still returning a clean AST. And several
// truncated column constraints (PRIMARY without KEY, NOT without NULL) consumed
// their leading keyword then returned no constraint, so the definition loop
// broke and the malformed tail parsed cleanly. These must all be rejected.

// Junk between two column definitions must not be skipped over.
TEST(ParserNegativeTest, CreateTableJunkBetweenColumns) {
    expect_parse_error("CREATE TABLE t (a INT %%% b INT)");
}

// An empty column list defines a zero-column table (invalid, no CTAS body).
TEST(ParserNegativeTest, CreateTableEmptyColumnList) {
    expect_parse_error("CREATE TABLE t ()");
}

// A column-level PRIMARY constraint must be PRIMARY KEY.
TEST(ParserNegativeTest, CreateTablePrimaryWithoutKey) {
    expect_parse_error("CREATE TABLE t (a INT PRIMARY)");
}

// NOT must be followed by NULL in a column constraint.
TEST(ParserNegativeTest, CreateTableNotWithoutNull) {
    expect_parse_error("CREATE TABLE t (a INT NOT)");
}

// A dangling CONSTRAINT keyword with no constraint body is not a definition.
TEST(ParserNegativeTest, CreateTableDanglingConstraintKeyword) {
    expect_parse_error("CREATE TABLE t (a INT CONSTRAINT)");
}

// Guard: valid CREATE TABLE forms (including multi-dimensional array columns,
// keyword-named columns like `data`, and the full complement of column
// constraints) must still parse cleanly with no trailing tokens - the strict
// element/constraint checks above must not over-reject.
TEST(ParserNegativeTest, CreateTableValidFormsStillParse) {
    Parser parser;
    struct { const char* sql; } ok[] = {
        {"CREATE TABLE t (a INT PRIMARY KEY)"},
        {"CREATE TABLE t (a INT NOT NULL)"},
        {"CREATE TABLE t (a INT, b TEXT)"},
        {"CREATE TABLE t (a INTEGER[][])"},
        {"CREATE TABLE t (data JSON)"},
        {"CREATE TABLE t (a INT PRIMARY KEY, b TEXT NOT NULL, c INT UNIQUE)"},
        {"CREATE TABLE t (a INT CHECK (a > 0))"},
        {"CREATE TABLE t AS SELECT * FROM x"},
        // Multi-word SQL type names: the strict column-list check must not trip
        // on the trailing type word(s) (regression guard for DOUBLE PRECISION
        // etc. being rejected once the recovery loop was removed).
        {"CREATE TABLE t (f DOUBLE PRECISION)"},
        {"CREATE TABLE t (a CHARACTER VARYING(20))"},
        {"CREATE TABLE t (a TIMESTAMP WITH TIME ZONE)"},
        {"CREATE TABLE t (a TIMESTAMP WITHOUT TIME ZONE)"},
        {"CREATE TABLE t (a DOUBLE PRECISION, b CHARACTER VARYING(20), "
         "c TIMESTAMP WITH TIME ZONE)"},
    };
    for (const auto& c : ok) {
        auto r = parser.parse(c.sql);
        EXPECT_TRUE(r.has_value()) << "expected clean parse for: [" << c.sql << "]";
        if (r.has_value()) {
            EXPECT_EQ(parser.trailing_token_count(), 0u)
                << "unexpected trailing tokens for: [" << c.sql << "]";
        }
    }
}
