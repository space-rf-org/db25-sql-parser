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
