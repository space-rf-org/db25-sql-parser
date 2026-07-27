/*
 * Regression tests for parser correctness fixes:
 *   - operator precedence tree shape (|| and bitwise now bind above comparison;
 *     arithmetic tiers; left-associativity)
 *   - set-operation LEFT associativity (A EXCEPT B EXCEPT C -> (A EXCEPT B) EXCEPT C)
 *   - GROUP BY with a missing item does not crash (was: delete on arena memory)
 *
 * These pin the exact AST shape so a future precedence/associativity regression
 * fails loudly rather than silently producing a different tree. The GROUP BY
 * case also runs under the ASan/UBSan CI job, which is what catches the old
 * undefined-behavior delete.
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include "db25/ast/node_types.hpp"
#include <string>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class PrecedenceRegressionTest : public ::testing::Test {
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
    // First projected expression: SelectList -> first child.
    ASTNode* first_projection(ASTNode* root) {
        auto* list = find(root, NodeType::SelectList);
        return list ? list->first_child : nullptr;
    }
    // The WHERE predicate: WhereClause -> first child.
    ASTNode* where_predicate(ASTNode* root) {
        auto* w = find(root, NodeType::WhereClause);
        return w ? w->first_child : nullptr;
    }
};

// || binds tighter than comparison: `a || b = c` -> `(a || b) = c`.
TEST_F(PrecedenceRegressionTest, ConcatAboveComparison) {
    auto* ast = parse("SELECT a || b = c FROM t");
    ASSERT_NE(ast, nullptr);
    auto* root = first_projection(ast);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(root->primary_text, "=");           // comparison is the root
    auto* left = root->first_child;
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(left->primary_text, "||");          // concat grouped underneath
}

// Bitwise AND binds tighter than comparison: `flags & 4 = 4` -> `(flags & 4) = 4`.
TEST_F(PrecedenceRegressionTest, BitwiseAboveComparison) {
    auto* ast = parse("SELECT * FROM t WHERE flags & 4 = 4");
    ASSERT_NE(ast, nullptr);
    auto* root = where_predicate(ast);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(root->primary_text, "=");
    auto* left = root->first_child;
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(left->primary_text, "&");
}

// Multiplication binds tighter than addition: `1 + 2 * 3` -> `1 + (2 * 3)`.
TEST_F(PrecedenceRegressionTest, MultiplicativeAboveAdditive) {
    auto* ast = parse("SELECT 1 + 2 * 3 FROM t");
    ASSERT_NE(ast, nullptr);
    auto* root = first_projection(ast);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->primary_text, "+");
    auto* right = root->first_child->next_sibling;
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(right->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(right->primary_text, "*");
}

// Subtraction is left-associative: `a - b - c` -> `(a - b) - c`.
TEST_F(PrecedenceRegressionTest, SubtractionLeftAssociative) {
    auto* ast = parse("SELECT a - b - c FROM t");
    ASSERT_NE(ast, nullptr);
    auto* root = first_projection(ast);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->primary_text, "-");
    auto* left = root->first_child;
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(left->primary_text, "-");           // the inner (a - b)
}

// AND binds tighter than OR: `a AND b OR c` -> `(a AND b) OR c`.
TEST_F(PrecedenceRegressionTest, AndAboveOr) {
    auto* ast = parse("SELECT * FROM t WHERE a AND b OR c");
    ASSERT_NE(ast, nullptr);
    auto* root = where_predicate(ast);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(root->primary_text, "OR");
    auto* left = root->first_child;
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->primary_text, "AND");
}

// Set operations fold LEFT: `A EXCEPT B EXCEPT C` -> `(A EXCEPT B) EXCEPT C`.
// The outer node's left child must itself be an EXCEPT (left-deep), not the
// right child (which would be right-associative and wrong for EXCEPT).
TEST_F(PrecedenceRegressionTest, SetOpLeftAssociative) {
    auto* ast = parse(
        "SELECT a FROM t1 EXCEPT SELECT b FROM t2 EXCEPT SELECT c FROM t3");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::ExceptStmt);
    auto* left = ast->first_child;
    auto* right = left ? left->next_sibling : nullptr;
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(left->node_type, NodeType::ExceptStmt);   // (A EXCEPT B) on the left
    EXPECT_EQ(right->node_type, NodeType::SelectStmt);  // C on the right
}

// A UNION also folds left, and mixed chains keep left-deep shape.
TEST_F(PrecedenceRegressionTest, UnionChainLeftAssociative) {
    auto* ast = parse(
        "SELECT a FROM t1 UNION SELECT b FROM t2 UNION SELECT c FROM t3");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::UnionStmt);
    ASSERT_NE(ast->first_child, nullptr);
    EXPECT_EQ(ast->first_child->node_type, NodeType::UnionStmt);
}

// INTERSECT binds TIGHTER than UNION (SQL standard; matches Postgres/Oracle/
// SQL Server/DuckDB/SQLite): `A UNION B INTERSECT C` -> `A UNION (B INTERSECT C)`.
// The root must be UNION and its RIGHT child the INTERSECT (regression: the old
// single-level fold produced root=INTERSECT / left=UNION, i.e. `(A UNION B) INTERSECT C`).
TEST_F(PrecedenceRegressionTest, IntersectBindsTighterThanUnion) {
    auto* ast = parse("SELECT 1 UNION SELECT 2 INTERSECT SELECT 3");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::UnionStmt);        // root is UNION
    auto* left = ast->first_child;
    auto* right = left ? left->next_sibling : nullptr;
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(left->node_type, NodeType::SelectStmt);      // A on the left
    EXPECT_EQ(right->node_type, NodeType::IntersectStmt);  // (B INTERSECT C) on the right
}

// Same precedence rule from the other side: `A INTERSECT B UNION C` ->
// `(A INTERSECT B) UNION C`. Root is UNION with the INTERSECT as its LEFT child.
TEST_F(PrecedenceRegressionTest, IntersectGroupsBeforeUnionOnLeft) {
    auto* ast = parse("SELECT 1 INTERSECT SELECT 2 UNION SELECT 3");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::UnionStmt);        // root is UNION
    auto* left = ast->first_child;
    auto* right = left ? left->next_sibling : nullptr;
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(left->node_type, NodeType::IntersectStmt);   // (A INTERSECT B) on the left
    EXPECT_EQ(right->node_type, NodeType::SelectStmt);     // C on the right
}

// Control: INTERSECT is left-associative among itself, exactly like UNION.
// `A INTERSECT B INTERSECT C` -> `(A INTERSECT B) INTERSECT C` (left-deep). This
// proves the precedence restructure did not break same-level associativity.
TEST_F(PrecedenceRegressionTest, IntersectChainLeftAssociative) {
    auto* ast = parse("SELECT 1 INTERSECT SELECT 2 INTERSECT SELECT 3");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::IntersectStmt);
    auto* left = ast->first_child;
    auto* right = left ? left->next_sibling : nullptr;
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(left->node_type, NodeType::IntersectStmt);   // (A INTERSECT B) on the left
    EXPECT_EQ(right->node_type, NodeType::SelectStmt);     // C on the right
}

// The ALL modifier must ride on the correct operator after the restructure:
// `A UNION ALL B INTERSECT C` -> UNION(ALL) whose right child is the INTERSECT.
TEST_F(PrecedenceRegressionTest, UnionAllModifierSurvivesIntersectRegroup) {
    auto* ast = parse("SELECT 1 UNION ALL SELECT 2 INTERSECT SELECT 3");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::UnionStmt);
    EXPECT_TRUE(ast->has_flag(NodeFlags::All));            // ALL stayed on UNION
    auto* right = ast->first_child ? ast->first_child->next_sibling : nullptr;
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(right->node_type, NodeType::IntersectStmt);
}

// GROUP BY with a missing item must not crash (regression: the old code called
// global delete on an arena-allocated node -> UB). The parser is lenient, so it
// still returns a SelectStmt; the point is that it completes cleanly (and does
// so under ASan/UBSan in CI).
TEST_F(PrecedenceRegressionTest, GroupByMissingItemNoCrash) {
    auto* ast = parse("SELECT x FROM t GROUP BY");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::SelectStmt);
    // No GroupByClause child should have been attached (the item failed to parse).
    EXPECT_EQ(find(ast, NodeType::GroupByClause), nullptr);
}

// ---- Set-operation operand / trailing-clause fixes ------------------------

// A parenthesized right operand of a set operation must be kept, not dropped:
// `SELECT 1 UNION (SELECT 2)` is a UNION over two SELECT branches.
TEST_F(PrecedenceRegressionTest, SetOpParenthesizedRightOperand) {
    auto* root = parse("SELECT 1 UNION (SELECT 2)");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::UnionStmt);
    int selects = 0;
    for (auto* c = root->first_child; c; c = c->next_sibling) {
        if (c->node_type == NodeType::SelectStmt) ++selects;
    }
    EXPECT_EQ(selects, 2) << "both UNION branches must survive";
}

// A trailing ORDER BY after a set operation binds to the whole result: it is a
// direct child of the set-op node, not swallowed by the right branch (which
// previously made `... UNION SELECT 2 ORDER BY 1` fail to parse).
TEST_F(PrecedenceRegressionTest, SetOpTrailingOrderByBindsToWhole) {
    auto* root = parse("SELECT a FROM t UNION SELECT a FROM u ORDER BY 1");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::UnionStmt);
    ASTNode* order_by = nullptr;
    for (auto* c = root->first_child; c; c = c->next_sibling) {
        if (c->node_type == NodeType::OrderByClause) order_by = c;
    }
    ASSERT_NE(order_by, nullptr) << "ORDER BY must attach to the UNION node";
}

TEST_F(PrecedenceRegressionTest, SetOpTrailingOrderByAndLimitBindToWhole) {
    auto* root = parse("SELECT a FROM t UNION SELECT a FROM u ORDER BY 1 LIMIT 5");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::UnionStmt);
    bool has_order = false, has_limit = false;
    for (auto* c = root->first_child; c; c = c->next_sibling) {
        if (c->node_type == NodeType::OrderByClause) has_order = true;
        if (c->node_type == NodeType::LimitClause) has_limit = true;
    }
    EXPECT_TRUE(has_order);
    EXPECT_TRUE(has_limit);
}

// Regression guard: a plain SELECT still attaches its own ORDER BY / LIMIT.
TEST_F(PrecedenceRegressionTest, PlainSelectStillAttachesOrderByLimit) {
    auto* root = parse("SELECT a FROM t ORDER BY a LIMIT 3");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::SelectStmt);
    EXPECT_NE(find(root, NodeType::OrderByClause), nullptr);
    EXPECT_NE(find(root, NodeType::LimitClause), nullptr);
}

// ---- LEADING parenthesized set-op operand ---------------------------------
// A set operation may begin with a parenthesized query block:
// `(SELECT 1) UNION (SELECT 2)`. Previously a leading '(' at statement level was
// not dispatched at all and the whole statement failed to parse.

// Both operands parenthesized: root is UNION over exactly two SELECT branches.
TEST_F(PrecedenceRegressionTest, LeadingParenBothOperands) {
    auto* root = parse("(SELECT 1) UNION (SELECT 2)");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::UnionStmt);
    auto* left = root->first_child;
    auto* right = left ? left->next_sibling : nullptr;
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(left->node_type, NodeType::SelectStmt);
    EXPECT_EQ(right->node_type, NodeType::SelectStmt);
}

// A parenthesized left operand with a bare right operand also folds.
TEST_F(PrecedenceRegressionTest, LeadingParenLeftBareRight) {
    auto* root = parse("(SELECT a FROM t1) UNION SELECT b FROM t2");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::UnionStmt);
    int selects = 0;
    for (auto* c = root->first_child; c; c = c->next_sibling) {
        if (c->node_type == NodeType::SelectStmt) ++selects;
    }
    EXPECT_EQ(selects, 2);
}

// Parentheses OVERRIDE the INTERSECT-binds-tighter precedence:
// `(A UNION B) INTERSECT C` must root at INTERSECT with the UNION as its LEFT
// child -- the opposite grouping from the unparenthesized `A UNION B INTERSECT C`
// (which roots at UNION, verified by IntersectBindsTighterThanUnion above).
TEST_F(PrecedenceRegressionTest, LeadingParenOverridesPrecedence) {
    auto* root = parse("(SELECT 1 UNION SELECT 2) INTERSECT SELECT 3");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::IntersectStmt);
    auto* left = root->first_child;
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->node_type, NodeType::UnionStmt)
        << "parenthesized UNION must be the INTERSECT's left operand";
}

// A trailing ORDER BY after a fully parenthesized set operation binds to the
// whole result, and nested parens `((...) UNION (...))` parse cleanly.
TEST_F(PrecedenceRegressionTest, LeadingParenNestedWithTrailingOrderBy) {
    auto* root = parse("((SELECT 1) UNION (SELECT 2)) ORDER BY 1");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::UnionStmt);
    ASTNode* order_by = nullptr;
    for (auto* c = root->first_child; c; c = c->next_sibling) {
        if (c->node_type == NodeType::OrderByClause) order_by = c;
    }
    ASSERT_NE(order_by, nullptr) << "ORDER BY must attach to the UNION node";
}

// A lone parenthesized query is just that query (no phantom set-op wrapper).
TEST_F(PrecedenceRegressionTest, LoneParenthesizedQueryUnwraps) {
    auto* root = parse("(SELECT a FROM t)");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::SelectStmt);
}

// ---- Parenthesized VALUES / non-query operands -----------------------------
// A parenthesized query block may be a VALUES list, not only a SELECT. Previously
// parse_parenthesized_query fell through to parse_select_stmt, which consumed the
// VALUES keyword as if it were SELECT and silently transposed the rows into a
// one-row select list.

// `(VALUES ...)` is a VALUES statement, identical to the unparenthesized form.
TEST_F(PrecedenceRegressionTest, ParenthesizedValuesIsValues) {
    auto* root = parse("(VALUES (2), (3))");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::ValuesStmt);
    // Two rows survive (the mis-parse produced a single 2-column SELECT list).
    ASTNode* rows = root->first_child;
    ASSERT_NE(rows, nullptr);
    EXPECT_EQ(rows->child_count, 2u);
}

// A VALUES arm of a set operation is kept, whether parenthesized or bare.
TEST_F(PrecedenceRegressionTest, SetOpValuesOperandKept) {
    {
        auto* root = parse("(SELECT 1) UNION (VALUES (2), (3))");
        ASSERT_NE(root, nullptr);
        EXPECT_EQ(root->node_type, NodeType::UnionStmt);
        auto* right = root->first_child ? root->first_child->next_sibling : nullptr;
        ASSERT_NE(right, nullptr);
        EXPECT_EQ(right->node_type, NodeType::ValuesStmt);
    }
    {
        // Bare VALUES operand: previously the whole `UNION VALUES (2)` arm was
        // silently dropped and the statement read as a lone SELECT.
        auto* root = parse("SELECT 1 UNION VALUES (2)");
        ASSERT_NE(root, nullptr);
        EXPECT_EQ(root->node_type, NodeType::UnionStmt);
        auto* right = root->first_child ? root->first_child->next_sibling : nullptr;
        ASSERT_NE(right, nullptr);
        EXPECT_EQ(right->node_type, NodeType::ValuesStmt);
    }
}

// A trailing ORDER BY / LIMIT after a BARE VALUES set-op arm binds to the WHOLE
// set operation, not to the VALUES operand. Regression: parse_values_stmt eats a
// trailing ORDER BY/LIMIT unconditionally, so `SELECT 1 UNION VALUES (2) ORDER BY 1`
// nested the OrderByClause under the VALUES arm (UNION had only 2 children) --
// unlike the SELECT arm and the parenthesized-VALUES arm, which both attach it to
// the UNION. Now the bare-VALUES operand is parsed in setop-rhs mode and does not
// swallow the clause.
TEST_F(PrecedenceRegressionTest, SetOpBareValuesTrailingOrderByBindsToWhole) {
    auto* root = parse("SELECT 1 UNION VALUES (2) ORDER BY 1");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::UnionStmt);
    // ORDER BY is a direct child of the UNION, not of the VALUES arm.
    ASTNode* order_by = nullptr;
    ASTNode* values = nullptr;
    for (auto* c = root->first_child; c; c = c->next_sibling) {
        if (c->node_type == NodeType::OrderByClause) order_by = c;
        if (c->node_type == NodeType::ValuesStmt) values = c;
    }
    ASSERT_NE(order_by, nullptr) << "ORDER BY must attach to the UNION node";
    ASSERT_NE(values, nullptr);
    EXPECT_EQ(find(values, NodeType::OrderByClause), nullptr)
        << "the VALUES arm must not own the trailing ORDER BY";
}

// Same for LIMIT after a bare VALUES set-op arm.
TEST_F(PrecedenceRegressionTest, SetOpBareValuesTrailingLimitBindsToWhole) {
    auto* root = parse("SELECT 1 UNION VALUES (2) LIMIT 5");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::UnionStmt);
    ASTNode* limit = nullptr;
    ASTNode* values = nullptr;
    for (auto* c = root->first_child; c; c = c->next_sibling) {
        if (c->node_type == NodeType::LimitClause) limit = c;
        if (c->node_type == NodeType::ValuesStmt) values = c;
    }
    ASSERT_NE(limit, nullptr) << "LIMIT must attach to the UNION node";
    ASSERT_NE(values, nullptr);
    EXPECT_EQ(find(values, NodeType::LimitClause), nullptr)
        << "the VALUES arm must not own the trailing LIMIT";
}

// Control: a TOP-LEVEL VALUES (not a set-op operand) still keeps its own ORDER BY
// -- the set-op-rhs suppression must not leak to the standalone form.
TEST_F(PrecedenceRegressionTest, TopLevelValuesKeepsOwnOrderBy) {
    auto* root = parse("VALUES (3), (1), (2) ORDER BY 1");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::ValuesStmt);
    EXPECT_NE(find(root, NodeType::OrderByClause), nullptr)
        << "a standalone VALUES must keep its own ORDER BY";
}

// A leading `(` that does not begin a query block (e.g. a parenthesized scalar
// expression) is rejected, not silently mis-parsed. `(1 + 2)` previously became
// `SELECT + 2` (the `1` consumed as if it were the SELECT keyword).
TEST_F(PrecedenceRegressionTest, ParenthesizedNonQueryRejected) {
    EXPECT_EQ(parse("(1 + 2)"), nullptr);
}
