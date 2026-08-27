/*
 * Regression tests for expression-parser hardening:
 *   - bind parameters ('?' and '$1') are preserved as Parameter nodes
 *   - BETWEEN bounds are value expressions and do not absorb a comparison
 *   - get_children() is safe under nested traversal
 *
 * These pin behavior found during code review so future regressions fail loudly.
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include "db25/ast/node_types.hpp"
#include <string>
#include <vector>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class ExprHardeningTest : public ::testing::Test {
protected:
    std::unique_ptr<Parser> parser;
    void SetUp() override { parser = std::make_unique<Parser>(); }

    ASTNode* parse(const std::string& sql) {
        auto result = parser->parse(sql);
        return result.has_value() ? result.value() : nullptr;
    }
    static ASTNode* find(ASTNode* n, NodeType t) {
        if (!n) return nullptr;
        if (n->node_type == t) return n;
        for (auto* c = n->first_child; c; c = c->next_sibling) {
            if (auto* h = find(c, t)) return h;
        }
        return nullptr;
    }
    static int count(ASTNode* n, NodeType t) {
        if (!n) return 0;
        int c = (n->node_type == t) ? 1 : 0;
        for (auto* ch = n->first_child; ch; ch = ch->next_sibling) c += count(ch, t);
        return c;
    }
    ASTNode* where_predicate(ASTNode* root) {
        auto* w = find(root, NodeType::WhereClause);
        return w ? w->first_child : nullptr;
    }
};

// ---- Bind parameters -------------------------------------------------------

TEST_F(ExprHardeningTest, PositionalParamInProjection) {
    auto* ast = parse("SELECT ? FROM t");
    ASSERT_NE(ast, nullptr);
    auto* p = find(ast, NodeType::Parameter);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->primary_text, "?");
}

TEST_F(ExprHardeningTest, PositionalParamInWhere) {
    // Regression: `= ?` was previously dropped, losing the comparison entirely.
    auto* ast = parse("SELECT * FROM t WHERE id = ?");
    ASSERT_NE(ast, nullptr);
    auto* pred = where_predicate(ast);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(pred->primary_text, "=");
    auto* rhs = pred->first_child->next_sibling;
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(rhs->node_type, NodeType::Parameter);
    EXPECT_EQ(rhs->primary_text, "?");
}

TEST_F(ExprHardeningTest, NumberedParamInWhere) {
    auto* ast = parse("SELECT * FROM t WHERE id = $1");
    ASSERT_NE(ast, nullptr);
    auto* pred = where_predicate(ast);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->primary_text, "=");
    auto* rhs = pred->first_child->next_sibling;
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(rhs->node_type, NodeType::Parameter);
    EXPECT_EQ(rhs->primary_text, "$1");
}

TEST_F(ExprHardeningTest, MultipleParamsInValues) {
    auto* ast = parse("INSERT INTO t VALUES (?, ?)");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(count(ast, NodeType::Parameter), 2);
}

TEST_F(ExprHardeningTest, NumberedParamsInArithmetic) {
    auto* ast = parse("SELECT $1 + $2 FROM t");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(count(ast, NodeType::Parameter), 2);
}

// ---- BETWEEN bounds --------------------------------------------------------

TEST_F(ExprHardeningTest, BetweenSimpleBounds) {
    auto* ast = parse("SELECT * FROM t WHERE x BETWEEN 1 AND 10");
    ASSERT_NE(ast, nullptr);
    auto* pred = where_predicate(ast);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->node_type, NodeType::BetweenExpr);
    // value, low, high
    auto* value = pred->first_child;
    ASSERT_NE(value, nullptr);
    auto* low = value->next_sibling;
    ASSERT_NE(low, nullptr);
    auto* high = low->next_sibling;
    ASSERT_NE(high, nullptr);
    EXPECT_EQ(low->primary_text, "1");
    EXPECT_EQ(high->primary_text, "10");
}

TEST_F(ExprHardeningTest, BetweenBoundDoesNotAbsorbComparison) {
    // `x BETWEEN a = c AND b`: a BETWEEN bound is a value expression that binds
    // tighter than comparison, so the low bound must NOT fold in the `= c`
    // comparison (the previous bug produced BetweenExpr -> [x, (a=c), b]).
    // The parse must not crash. If any BetweenExpr is produced, its low bound
    // must not be a comparison node.
    auto* ast = parse("SELECT * FROM t WHERE x BETWEEN a = c AND b");
    ASSERT_NE(ast, nullptr);
    auto* between = find(ast, NodeType::BetweenExpr);
    if (between != nullptr) {
        auto* value = between->first_child;
        ASSERT_NE(value, nullptr);
        auto* low = value->next_sibling;
        ASSERT_NE(low, nullptr);
        // The low bound must never be a `=` comparison absorbed into the bound.
        const bool low_is_comparison =
            (low->node_type == NodeType::BinaryExpr && low->primary_text == "=");
        EXPECT_FALSE(low_is_comparison);
    }
}

TEST_F(ExprHardeningTest, BetweenTrailingAndStillTerminates) {
    // `x BETWEEN a AND b AND c` must be `(x BETWEEN a AND b) AND c`.
    auto* ast = parse("SELECT * FROM t WHERE x BETWEEN a AND b AND c");
    ASSERT_NE(ast, nullptr);
    auto* pred = where_predicate(ast);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(pred->primary_text, "AND");
    auto* left = pred->first_child;
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->node_type, NodeType::BetweenExpr);
}

// ---- get_children() nested traversal safety --------------------------------

TEST_F(ExprHardeningTest, NestedGetChildrenTraversalIsSafe) {
    auto* ast = parse("SELECT a, b, c FROM t WHERE x = 1 AND y = 2");
    ASSERT_NE(ast, nullptr);

    // Nested traversal: for each child, iterate its children while the outer
    // iteration is still live. With a shared thread-local buffer this corrupts
    // the outer span; with an owning return value it is correct.
    int total_grandchildren = 0;
    int outer_seen = 0;
    for (auto* child : ast->get_children()) {
        outer_seen++;
        int inner = 0;
        for (auto* gc : child->get_children()) {
            (void)gc;
            inner++;
        }
        total_grandchildren += inner;
        // The outer collection must remain valid/consistent across inner loops.
        EXPECT_NE(child, nullptr);
    }
    // Sanity: we actually visited the top-level children and some grandchildren.
    EXPECT_GT(outer_seen, 0);
    EXPECT_GT(total_grandchildren, 0);

    // Cross-check: the number of children we iterated equals child_count.
    EXPECT_EQ(static_cast<uint32_t>(outer_seen), ast->child_count);
}

// ---- Numeric literal node types --------------------------------------------
// The tokenizer now lexes hex (0x..) / binary (0b..) integers and leading-dot
// floats (.5) as single Number tokens; the parser must classify them into the
// right literal node. In particular a hex literal is an INTEGER even when its
// digits include 'e'/'E' (0xBEEF), which the naive exponent check misread as a
// float.

TEST_F(ExprHardeningTest, HexLiteralIsInteger) {
    auto* ast = parse("SELECT 0xFF FROM t");
    ASSERT_NE(ast, nullptr);
    auto* lit = find(ast, NodeType::IntegerLiteral);
    ASSERT_NE(lit, nullptr);
    EXPECT_EQ(lit->primary_text, "0xFF");
    EXPECT_EQ(find(ast, NodeType::FloatLiteral), nullptr);
}

TEST_F(ExprHardeningTest, HexLiteralWithHexEIsInteger) {
    // Regression: 'e'/'E' are hex digits here, not an exponent marker.
    auto* ast = parse("SELECT 0xBEEF FROM t");
    ASSERT_NE(ast, nullptr);
    auto* lit = find(ast, NodeType::IntegerLiteral);
    ASSERT_NE(lit, nullptr);
    EXPECT_EQ(lit->primary_text, "0xBEEF");
    EXPECT_EQ(find(ast, NodeType::FloatLiteral), nullptr);
}

TEST_F(ExprHardeningTest, BinaryLiteralIsInteger) {
    auto* ast = parse("SELECT 0b1010 FROM t");
    ASSERT_NE(ast, nullptr);
    auto* lit = find(ast, NodeType::IntegerLiteral);
    ASSERT_NE(lit, nullptr);
    EXPECT_EQ(lit->primary_text, "0b1010");
    EXPECT_EQ(find(ast, NodeType::FloatLiteral), nullptr);
}

TEST_F(ExprHardeningTest, LeadingDotFloatIsFloat) {
    auto* ast = parse("SELECT .5 FROM t");
    ASSERT_NE(ast, nullptr);
    auto* lit = find(ast, NodeType::FloatLiteral);
    ASSERT_NE(lit, nullptr);
    EXPECT_EQ(lit->primary_text, ".5");
}

TEST_F(ExprHardeningTest, DecimalStaysFloatIntegerStaysInteger) {
    // Regression guard for the ordinary decimal forms.
    auto* d = parse("SELECT 3.14 FROM t");
    ASSERT_NE(d, nullptr);
    ASSERT_NE(find(d, NodeType::FloatLiteral), nullptr);
    EXPECT_EQ(find(d, NodeType::IntegerLiteral), nullptr);

    auto* i = parse("SELECT 42 FROM t");
    ASSERT_NE(i, nullptr);
    ASSERT_NE(find(i, NodeType::IntegerLiteral), nullptr);
    EXPECT_EQ(find(i, NodeType::FloatLiteral), nullptr);
}

// ---- Delimited (double-quoted) identifiers ---------------------------------
// A double-quoted lexeme is a delimited identifier, so it must reach the parser
// as a column reference carrying the bare inner text - not a string literal.

TEST_F(ExprHardeningTest, DelimitedIdentifierIsColumnRef) {
    auto* ast = parse("SELECT \"id\" FROM t");
    ASSERT_NE(ast, nullptr);
    auto* col = find(ast, NodeType::ColumnRef);
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->primary_text, "id");
    // It must not have been lexed as a string literal.
    EXPECT_EQ(find(ast, NodeType::StringLiteral), nullptr);
}

TEST_F(ExprHardeningTest, DelimitedIdentifierPreservesSpaceAndCase) {
    auto* ast = parse("SELECT \"User Name\" FROM t");
    ASSERT_NE(ast, nullptr);
    auto* col = find(ast, NodeType::ColumnRef);
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->primary_text, "User Name");
}

TEST_F(ExprHardeningTest, DelimitedKeywordIsIdentifierNotKeyword) {
    // "select" in quotes is a column named select, not the SELECT keyword.
    auto* ast = parse("SELECT \"select\" FROM t");
    ASSERT_NE(ast, nullptr);
    auto* col = find(ast, NodeType::ColumnRef);
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->primary_text, "select");
}

TEST_F(ExprHardeningTest, SingleQuotedStaysStringLiteral) {
    // Regression guard: single quotes remain a string literal, not an identifier.
    auto* ast = parse("SELECT 'id' FROM t");
    ASSERT_NE(ast, nullptr);
    EXPECT_NE(find(ast, NodeType::StringLiteral), nullptr);
}

// ---- COLLATE postfix -------------------------------------------------------
// `<value> COLLATE <name>` annotates a value with a collation. It must parse as
// a CollateClause wrapping the value, with the collation name recorded, instead
// of derailing the surrounding expression (which used to drop the column).

TEST_F(ExprHardeningTest, CollateInProjection) {
    auto* ast = parse("SELECT name COLLATE \"C\" FROM t");
    ASSERT_NE(ast, nullptr);
    auto* col = find(ast, NodeType::CollateClause);
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->schema_name, "C");
    // The annotated value is its child column reference.
    auto* operand = col->first_child;
    ASSERT_NE(operand, nullptr);
    EXPECT_EQ(operand->node_type, NodeType::ColumnRef);
    EXPECT_EQ(operand->primary_text, "name");
}

TEST_F(ExprHardeningTest, CollateBindsTighterThanComparison) {
    // `s COLLATE "C" = 'a'` is `(s COLLATE "C") = 'a'`: the '=' predicate's left
    // operand is the CollateClause, and the column is not lost.
    auto* ast = parse("SELECT * FROM t WHERE s COLLATE \"C\" = 'a'");
    ASSERT_NE(ast, nullptr);
    auto* pred = where_predicate(ast);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(pred->primary_text, "=");
    auto* lhs = pred->first_child;
    ASSERT_NE(lhs, nullptr);
    EXPECT_EQ(lhs->node_type, NodeType::CollateClause);
}

// ---- IS [NOT] TRUE / FALSE / UNKNOWN --------------------------------------

TEST_F(ExprHardeningTest, IsTrueBuildsBooleanTest) {
    // `flag IS TRUE` is a BooleanTestExpr over the column, not an IsNullExpr and
    // not a dropped predicate.
    auto* ast = parse("SELECT * FROM t WHERE flag IS TRUE");
    ASSERT_NE(ast, nullptr);
    auto* pred = where_predicate(ast);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->node_type, NodeType::BooleanTestExpr);
    EXPECT_EQ(pred->primary_text, "IS TRUE");
    auto* operand = pred->first_child;
    ASSERT_NE(operand, nullptr);
    EXPECT_EQ(operand->node_type, NodeType::ColumnRef);
    EXPECT_EQ(operand->primary_text, "flag");
}

TEST_F(ExprHardeningTest, IsNotFalseBuildsNegatedBooleanTest) {
    auto* ast = parse("SELECT * FROM t WHERE flag IS NOT FALSE");
    ASSERT_NE(ast, nullptr);
    auto* pred = where_predicate(ast);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->node_type, NodeType::BooleanTestExpr);
    EXPECT_EQ(pred->primary_text, "IS NOT FALSE");
}

TEST_F(ExprHardeningTest, IsUnknownBuildsBooleanTest) {
    // UNKNOWN is not a reserved keyword; it arrives as an identifier and must
    // still be recognized (case-insensitively) as the boolean-test target.
    auto* ast = parse("SELECT * FROM t WHERE (a > b) is unknown");
    ASSERT_NE(ast, nullptr);
    auto* pred = where_predicate(ast);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->node_type, NodeType::BooleanTestExpr);
    EXPECT_EQ(pred->primary_text, "IS UNKNOWN");
}

TEST_F(ExprHardeningTest, IsNullStillParsesAfterBooleanTest) {
    // Regression guard: the IS handler must still route NULL to IsNullExpr.
    auto* ast = parse("SELECT * FROM t WHERE x IS NOT NULL");
    ASSERT_NE(ast, nullptr);
    auto* pred = where_predicate(ast);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->node_type, NodeType::IsNullExpr);
    EXPECT_EQ(pred->primary_text, "IS NOT NULL");
}

// ---- IS [NOT] DISTINCT FROM (null-safe comparison) ------------------------

TEST_F(ExprHardeningTest, IsDistinctFromBuildsBinaryExpr) {
    // `x IS DISTINCT FROM y` is a binary comparison over both operands, not a
    // dropped predicate (previously the DISTINCT FROM tail was silently lost,
    // leaving just `x`).
    auto* ast = parse("SELECT * FROM t WHERE x IS DISTINCT FROM y");
    ASSERT_NE(ast, nullptr);
    auto* pred = where_predicate(ast);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(pred->primary_text, "IS DISTINCT FROM");
    ASSERT_NE(pred->first_child, nullptr);
    EXPECT_EQ(pred->first_child->node_type, NodeType::ColumnRef);
    EXPECT_EQ(pred->first_child->primary_text, "x");
    ASSERT_NE(pred->first_child->next_sibling, nullptr);
    EXPECT_EQ(pred->first_child->next_sibling->node_type, NodeType::ColumnRef);
    EXPECT_EQ(pred->first_child->next_sibling->primary_text, "y");
}

TEST_F(ExprHardeningTest, IsNotDistinctFromBuildsBinaryExpr) {
    auto* ast = parse("SELECT * FROM t WHERE x IS NOT DISTINCT FROM y");
    ASSERT_NE(ast, nullptr);
    auto* pred = where_predicate(ast);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(pred->primary_text, "IS NOT DISTINCT FROM");
}

TEST_F(ExprHardeningTest, IsDistinctWithoutFromIsRejected) {
    // `IS DISTINCT` with no FROM is malformed and must not silently drop.
    auto* ast = parse("SELECT * FROM t WHERE x IS DISTINCT");
    EXPECT_EQ(ast, nullptr);
}

// ---- ILIKE (case-insensitive LIKE) ----------------------------------------

TEST_F(ExprHardeningTest, IlikeBuildsLikeExpr) {
    // `name ILIKE 'a%'` is a LikeExpr tagged ILIKE, over the column and pattern.
    auto* ast = parse("SELECT * FROM t WHERE name ILIKE 'a%'");
    ASSERT_NE(ast, nullptr);
    auto* pred = where_predicate(ast);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->node_type, NodeType::LikeExpr);
    EXPECT_EQ(pred->primary_text, "ILIKE");
    auto* lhs = pred->first_child;
    ASSERT_NE(lhs, nullptr);
    EXPECT_EQ(lhs->node_type, NodeType::ColumnRef);
    EXPECT_EQ(lhs->primary_text, "name");
}

TEST_F(ExprHardeningTest, NotIlikeBuildsNegatedLikeExpr) {
    auto* ast = parse("SELECT * FROM t WHERE name NOT ILIKE 'a%'");
    ASSERT_NE(ast, nullptr);
    auto* pred = where_predicate(ast);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->node_type, NodeType::LikeExpr);
    EXPECT_EQ(pred->primary_text, "NOT ILIKE");
}

TEST_F(ExprHardeningTest, PlainLikeStillParses) {
    // Regression guard: LIKE must still produce a LikeExpr tagged LIKE.
    auto* ast = parse("SELECT * FROM t WHERE name LIKE 'a%'");
    ASSERT_NE(ast, nullptr);
    auto* pred = where_predicate(ast);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->node_type, NodeType::LikeExpr);
    EXPECT_EQ(pred->primary_text, "LIKE");
}

// ---- Aggregate FILTER (WHERE ...) -----------------------------------------

TEST_F(ExprHardeningTest, AggregateFilterAttachesWhereClause) {
    // COUNT(*) FILTER (WHERE age > 20): the FILTER predicate attaches as a
    // WhereClause child of the call, distinct from the (star) argument.
    auto* ast = parse("SELECT COUNT(*) FILTER (WHERE age > 20) FROM users");
    ASSERT_NE(ast, nullptr);
    auto* call = find(ast, NodeType::FunctionCall);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->primary_text, "COUNT");
    // Find the WhereClause (FILTER) child.
    ASTNode* filter = nullptr;
    for (auto* c = call->first_child; c; c = c->next_sibling)
        if (c->node_type == NodeType::WhereClause) { filter = c; break; }
    ASSERT_NE(filter, nullptr) << "FILTER should attach a WhereClause child";
    EXPECT_EQ(filter->primary_text, "FILTER");
    // Its child is the predicate.
    auto* pred = filter->first_child;
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(pred->primary_text, ">");
}

TEST_F(ExprHardeningTest, AggregateWithoutFilterHasNoWhereClause) {
    // Regression guard: a plain aggregate has no WhereClause child.
    auto* ast = parse("SELECT COUNT(*) FROM users");
    ASSERT_NE(ast, nullptr);
    auto* call = find(ast, NodeType::FunctionCall);
    ASSERT_NE(call, nullptr);
    for (auto* c = call->first_child; c; c = c->next_sibling)
        EXPECT_NE(c->node_type, NodeType::WhereClause);
}

// ---- Ordered aggregates (ORDER BY inside the argument list) ----------------

TEST_F(ExprHardeningTest, OrderedAggregateAttachesOrderByClause) {
    // array_agg(x ORDER BY y): the ORDER BY orders the aggregated input and
    // attaches as an OrderByClause child of the call, after the argument and
    // distinct from it. Previously this failed to parse ("Unclosed parenthesis").
    auto* ast = parse("SELECT array_agg(x ORDER BY y) FROM t");
    ASSERT_NE(ast, nullptr);
    auto* call = find(ast, NodeType::FunctionCall);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->primary_text, "array_agg");
    // The argument x comes first; the OrderByClause is a later child.
    ASTNode* order_by = nullptr;
    for (auto* c = call->first_child; c; c = c->next_sibling)
        if (c->node_type == NodeType::OrderByClause) { order_by = c; break; }
    ASSERT_NE(order_by, nullptr) << "ORDER BY should attach an OrderByClause child";
    // Its first item is the sort key y.
    ASSERT_NE(order_by->first_child, nullptr);
    // The FROM clause survived (the ORDER BY did not swallow the rest / was not
    // dropped by leftover-token tolerance).
    EXPECT_NE(find(ast, NodeType::FromClause), nullptr) << "FROM must still parse";
}

TEST_F(ExprHardeningTest, OrderedAggregateWithMultipleArgsAndDesc) {
    // string_agg(v, ',' ORDER BY v DESC): two arguments then an ORDER BY DESC.
    auto* ast = parse("SELECT string_agg(v, ',' ORDER BY v DESC) FROM t");
    ASSERT_NE(ast, nullptr);
    auto* call = find(ast, NodeType::FunctionCall);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->primary_text, "string_agg");
    ASTNode* order_by = nullptr;
    int args = 0;
    for (auto* c = call->first_child; c; c = c->next_sibling) {
        if (c->node_type == NodeType::OrderByClause) order_by = c;
        else ++args;
    }
    EXPECT_EQ(args, 2) << "both string_agg arguments must remain";
    ASSERT_NE(order_by, nullptr);
    // DESC is recorded on the sort item (bit 7 of semantic_flags).
    ASSERT_NE(order_by->first_child, nullptr);
    EXPECT_TRUE(order_by->first_child->semantic_flags & (1 << 7)) << "DESC flag set";
}

TEST_F(ExprHardeningTest, PlainAggregateHasNoOrderByClause) {
    // Regression guard: a plain aggregate has no OrderByClause child.
    auto* ast = parse("SELECT array_agg(x) FROM t");
    ASSERT_NE(ast, nullptr);
    auto* call = find(ast, NodeType::FunctionCall);
    ASSERT_NE(call, nullptr);
    for (auto* c = call->first_child; c; c = c->next_sibling)
        EXPECT_NE(c->node_type, NodeType::OrderByClause);
}

TEST_F(ExprHardeningTest, IncompleteOrderedAggregateIsRejected) {
    // An aggregate ORDER BY with no sort key (or no BY) is a syntax error, not a
    // silently-accepted plain aggregate. Previously the ORDER/BY tokens were
    // consumed and dropped and the statement parsed OK.
    EXPECT_FALSE(parser->parse("SELECT array_agg(x ORDER BY) FROM t").has_value())
        << "array_agg(x ORDER BY) with no sort key must be rejected";
    EXPECT_FALSE(parser->parse("SELECT array_agg(x ORDER) FROM t").has_value())
        << "array_agg(x ORDER) with no BY must be rejected";
    // A complete ordered aggregate still parses (guard against over-rejection).
    EXPECT_TRUE(parser->parse("SELECT array_agg(x ORDER BY y) FROM t").has_value());
}

// ---- Value array-subscript -------------------------------------------------

TEST_F(ExprHardeningTest, ValueSubscriptIsRejectedNotTruncated) {
    // `a[1]` is a value array-subscript, which this grammar does not support.
    // It must be REJECTED, not silently dropped along with the rest of the
    // statement (previously `SELECT a[1], b FROM t` parsed as just `a`, losing
    // `b` and the whole FROM; `WHERE a[1] = 5` became `WHERE a`).
    EXPECT_FALSE(parser->parse("SELECT a[1], b FROM t").has_value())
        << "a value subscript must be rejected";
    EXPECT_FALSE(parser->parse("SELECT x FROM t WHERE a[1] = 5").has_value())
        << "a value subscript in a predicate must be rejected";
    EXPECT_FALSE(parser->parse("SELECT t.a[1] FROM t").has_value())
        << "a qualified value subscript must be rejected";

    // Guard against over-rejection: the ARRAY[...] constructor, the `::type[]`
    // array-type cast suffix, and a DDL array type all legitimately use brackets
    // and must still parse.
    EXPECT_TRUE(parser->parse("SELECT ARRAY[1, 2, 3] FROM t").has_value());
    EXPECT_TRUE(parser->parse("SELECT x::int[] FROM t").has_value());
    EXPECT_TRUE(parser->parse("SELECT x::int[][] FROM t").has_value());
    EXPECT_TRUE(parser->parse("CREATE TABLE t (a INTEGER[])").has_value());
}

// ---- Row constructors ------------------------------------------------------

TEST_F(ExprHardeningTest, BareTupleBuildsRowConstructor) {
    // (a, b) with a top-level comma is a RowConstructor with two children.
    auto* ast = parse("SELECT * FROM t WHERE (a, b) = (1, 2)");
    ASSERT_NE(ast, nullptr);
    auto* pred = where_predicate(ast);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(pred->primary_text, "=");
    auto* lhs = pred->first_child;
    ASSERT_NE(lhs, nullptr);
    EXPECT_EQ(lhs->node_type, NodeType::RowConstructor);
    EXPECT_EQ(lhs->child_count, 2);
    auto* rhs = lhs->next_sibling;
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(rhs->node_type, NodeType::RowConstructor);
    EXPECT_EQ(rhs->child_count, 2);
}

TEST_F(ExprHardeningTest, ExplicitRowKeywordBuildsRowConstructor) {
    // ROW(a, b, c) is a RowConstructor, not a function call named ROW.
    auto* ast = parse("SELECT * FROM t WHERE ROW(a, b, c) = ROW(1, 2, 3)");
    ASSERT_NE(ast, nullptr);
    auto* pred = where_predicate(ast);
    ASSERT_NE(pred, nullptr);
    auto* lhs = pred->first_child;
    ASSERT_NE(lhs, nullptr);
    EXPECT_EQ(lhs->node_type, NodeType::RowConstructor);
    EXPECT_EQ(lhs->child_count, 3);
    EXPECT_EQ(find(ast, NodeType::FunctionCall), nullptr) << "ROW must not be a FunctionCall";
}

// ---- DDL CHECK / DEFAULT expression source text ---------------------------

TEST_F(ExprHardeningTest, CheckConstraintCapturesExprText) {
    auto* ast = parse("CREATE TABLE t (age INTEGER CHECK (age >= 18))");
    ASSERT_NE(ast, nullptr);
    auto* chk = find(ast, NodeType::CheckConstraint);
    ASSERT_NE(chk, nullptr);
    EXPECT_EQ(chk->primary_text, "age >= 18");
}

TEST_F(ExprHardeningTest, TableCheckCapturesExprText) {
    auto* ast = parse("CREATE TABLE t (a INTEGER, b INTEGER, CHECK (a < b))");
    ASSERT_NE(ast, nullptr);
    auto* chk = find(ast, NodeType::CheckConstraint);
    ASSERT_NE(chk, nullptr);
    EXPECT_EQ(chk->primary_text, "a < b");
}

TEST_F(ExprHardeningTest, DefaultClauseCapturesExprText) {
    auto* lit = parse("CREATE TABLE t (a INTEGER DEFAULT 0)");
    ASSERT_NE(lit, nullptr);
    auto* d1 = find(lit, NodeType::DefaultClause);
    ASSERT_NE(d1, nullptr);
    EXPECT_EQ(d1->primary_text, "0");

    auto* fn = parse("CREATE TABLE t (a TIMESTAMP DEFAULT now())");
    ASSERT_NE(fn, nullptr);
    auto* d2 = find(fn, NodeType::DefaultClause);
    ASSERT_NE(d2, nullptr);
    EXPECT_EQ(d2->primary_text, "now()");
}

TEST_F(ExprHardeningTest, AlterColumnSetDefaultCapturesExprText) {
    // ALTER COLUMN SET DEFAULT wraps the new default in a DefaultClause carrying
    // the verbatim source text, exactly like a column-definition DEFAULT.
    auto* ast = parse("ALTER TABLE t ALTER COLUMN a SET DEFAULT 42");
    ASSERT_NE(ast, nullptr);
    auto* d = find(ast, NodeType::DefaultClause);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->primary_text, "42");

    auto* fn = parse("ALTER TABLE t ALTER COLUMN ts SET DEFAULT now()");
    ASSERT_NE(fn, nullptr);
    auto* d2 = find(fn, NodeType::DefaultClause);
    ASSERT_NE(d2, nullptr);
    EXPECT_EQ(d2->primary_text, "now()");
}

TEST_F(ExprHardeningTest, AlterTableAddConstraint) {
    // ADD [table-constraint] parses to the matching constraint node under the
    // ALTER action, not a column definition.
    auto* pk = parse("ALTER TABLE t ADD PRIMARY KEY (a, b)");
    ASSERT_NE(pk, nullptr);
    ASSERT_NE(find(pk, NodeType::PrimaryKeyConstraint), nullptr);
    EXPECT_EQ(find(pk, NodeType::ColumnDefinition), nullptr);

    auto* uq = parse("ALTER TABLE t ADD UNIQUE (email)");
    ASSERT_NE(uq, nullptr);
    EXPECT_NE(find(uq, NodeType::UniqueConstraint), nullptr);

    auto* ck = parse("ALTER TABLE t ADD CHECK (age >= 0)");
    ASSERT_NE(ck, nullptr);
    auto* chk = find(ck, NodeType::CheckConstraint);
    ASSERT_NE(chk, nullptr);
    EXPECT_EQ(chk->primary_text, "age >= 0");

    auto* fk = parse("ALTER TABLE t ADD FOREIGN KEY (pid) REFERENCES parent (id)");
    ASSERT_NE(fk, nullptr);
    EXPECT_NE(find(fk, NodeType::ForeignKeyConstraint), nullptr);

    // ADD COLUMN still works (no constraint keyword).
    auto* col = parse("ALTER TABLE t ADD COLUMN c INTEGER");
    ASSERT_NE(col, nullptr);
    EXPECT_NE(find(col, NodeType::ColumnDefinition), nullptr);
}

TEST_F(ExprHardeningTest, NamedConstraintCapturesName) {
    // The optional CONSTRAINT <name> is captured on the constraint node's
    // schema_name (not primary_text, which a CHECK uses for its expression).
    auto* ast = parse("CREATE TABLE t (a INTEGER, CONSTRAINT uq_a UNIQUE (a))");
    ASSERT_NE(ast, nullptr);
    auto* u = find(ast, NodeType::UniqueConstraint);
    ASSERT_NE(u, nullptr);
    EXPECT_EQ(u->schema_name, "uq_a");

    auto* ck = parse("CREATE TABLE t (a INTEGER, CONSTRAINT ck_a CHECK (a > 0))");
    ASSERT_NE(ck, nullptr);
    auto* c = find(ck, NodeType::CheckConstraint);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->schema_name, "ck_a");     // name
    EXPECT_EQ(c->primary_text, "a > 0");   // expression, unaffected
}

TEST_F(ExprHardeningTest, AlterDropConstraint) {
    // DROP CONSTRAINT <name> flags the action (0x20) and names it as an
    // Identifier child, distinct from DROP COLUMN.
    auto* ast = parse("ALTER TABLE t DROP CONSTRAINT uq_a");
    ASSERT_NE(ast, nullptr);
    auto* act = find(ast, NodeType::AlterTableAction);
    ASSERT_NE(act, nullptr);
    EXPECT_TRUE(act->semantic_flags & 0x20);
    auto* nm = find(act, NodeType::Identifier);
    ASSERT_NE(nm, nullptr);
    EXPECT_EQ(nm->primary_text, "uq_a");

    // DROP COLUMN is still not flagged as a constraint drop.
    auto* col = parse("ALTER TABLE t DROP COLUMN a");
    ASSERT_NE(col, nullptr);
    auto* act2 = find(col, NodeType::AlterTableAction);
    ASSERT_NE(act2, nullptr);
    EXPECT_FALSE(act2->semantic_flags & 0x20);
}

TEST_F(ExprHardeningTest, AlterColumnSetDropNotNull) {
    // SET NOT NULL / DROP NOT NULL are recorded as flags on the action node.
    auto* a1 = parse("ALTER TABLE t ALTER COLUMN c SET NOT NULL");
    ASSERT_NE(a1, nullptr);
    auto* act1 = find(a1, NodeType::AlterTableAction);
    ASSERT_NE(act1, nullptr);
    EXPECT_TRUE(act1->semantic_flags & 0x08) << "SET NOT NULL flag";
    EXPECT_FALSE(act1->semantic_flags & 0x10);

    auto* a2 = parse("ALTER TABLE t ALTER COLUMN c DROP NOT NULL");
    ASSERT_NE(a2, nullptr);
    auto* act2 = find(a2, NodeType::AlterTableAction);
    ASSERT_NE(act2, nullptr);
    EXPECT_TRUE(act2->semantic_flags & 0x10) << "DROP NOT NULL flag";
    EXPECT_FALSE(act2->semantic_flags & 0x08);
}

// ---- DDL column lists (previously stubbed) --------------------------------

TEST_F(ExprHardeningTest, CreateIndexCapturesColumns) {
    auto* ast = parse("CREATE INDEX idx_uc ON users (last, first)");
    ASSERT_NE(ast, nullptr);
    auto* idx = find(ast, NodeType::CreateIndexStmt);
    ASSERT_NE(idx, nullptr);
    EXPECT_EQ(idx->primary_text, "idx_uc");
    EXPECT_EQ(idx->schema_name, "users");
    EXPECT_EQ(count(idx, NodeType::Identifier), 2);  // last, first
    ASSERT_NE(idx->first_child, nullptr);
    EXPECT_EQ(idx->first_child->node_type, NodeType::Identifier);
    EXPECT_EQ(idx->first_child->primary_text, "last");
}

TEST_F(ExprHardeningTest, TableLevelForeignKeyCapturesColumnsAndReferences) {
    auto* ast = parse(
        "CREATE TABLE orders (uid INTEGER, "
        "FOREIGN KEY (uid) REFERENCES users (id))");
    ASSERT_NE(ast, nullptr);
    auto* fk = find(ast, NodeType::ForeignKeyConstraint);
    ASSERT_NE(fk, nullptr);
    // First child is the local column; then a ReferencesClause.
    ASSERT_NE(fk->first_child, nullptr);
    EXPECT_EQ(fk->first_child->node_type, NodeType::Identifier);
    EXPECT_EQ(fk->first_child->primary_text, "uid");
    auto* ref = find(fk, NodeType::ReferencesClause);
    ASSERT_NE(ref, nullptr);
    EXPECT_EQ(ref->primary_text, "users");                     // referenced table
    ASSERT_NE(ref->first_child, nullptr);
    EXPECT_EQ(ref->first_child->node_type, NodeType::Identifier);
    EXPECT_EQ(ref->first_child->primary_text, "id");           // referenced column
}

TEST_F(ExprHardeningTest, TableLevelUniqueCapturesColumns) {
    auto* ast = parse("CREATE TABLE t (a INTEGER, b INTEGER, UNIQUE (a, b))");
    ASSERT_NE(ast, nullptr);
    auto* uq = find(ast, NodeType::UniqueConstraint);
    ASSERT_NE(uq, nullptr);
    EXPECT_EQ(count(uq, NodeType::Identifier), 2);
}

TEST_F(ExprHardeningTest, SingleParenIsGroupingNotRow) {
    // CRITICAL regression guard: (a + b) with NO comma is ordinary grouping,
    // NOT a one-element row constructor.
    auto* ast = parse("SELECT * FROM t WHERE (a + b) = 3");
    ASSERT_NE(ast, nullptr);
    auto* pred = where_predicate(ast);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->node_type, NodeType::BinaryExpr);
    auto* lhs = pred->first_child;
    ASSERT_NE(lhs, nullptr);
    EXPECT_EQ(lhs->node_type, NodeType::BinaryExpr);  // the (a + b) group
    EXPECT_EQ(lhs->primary_text, "+");
    EXPECT_EQ(find(ast, NodeType::RowConstructor), nullptr) << "no comma -> no row";
}

// ---- IN requires a parenthesised list or subquery -------------------------

TEST_F(ExprHardeningTest, InWithoutParensIsRejected) {
    // `a IN 5` (no parens) is malformed; it must error rather than silently
    // dropping the IN and leaving the predicate as the bare column `a`.
    EXPECT_EQ(parse("SELECT a FROM t WHERE a IN 5"), nullptr);
    EXPECT_EQ(parse("SELECT a FROM t WHERE a NOT IN 5"), nullptr);
}

TEST_F(ExprHardeningTest, InListAndSubqueryStillParse) {
    // Regression guard: the valid parenthesised forms are unaffected.
    {
        auto* ast = parse("SELECT a FROM t WHERE a IN (1, 2, 3)");
        ASSERT_NE(ast, nullptr);
        auto* pred = where_predicate(ast);
        ASSERT_NE(pred, nullptr);
        EXPECT_EQ(pred->node_type, NodeType::InExpr);
        EXPECT_EQ(pred->primary_text, "IN");
    }
    {
        auto* ast = parse("SELECT a FROM t WHERE a IN (SELECT id FROM u)");
        ASSERT_NE(ast, nullptr);
        auto* pred = where_predicate(ast);
        ASSERT_NE(pred, nullptr);
        EXPECT_EQ(pred->node_type, NodeType::InExpr);
    }
    {
        auto* ast = parse("SELECT a FROM t WHERE a NOT IN (1, 2)");
        ASSERT_NE(ast, nullptr);
        auto* pred = where_predicate(ast);
        ASSERT_NE(pred, nullptr);
        EXPECT_EQ(pred->node_type, NodeType::InExpr);
        EXPECT_EQ(pred->primary_text, "NOT IN");
    }
}
