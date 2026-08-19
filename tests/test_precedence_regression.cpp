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

// SQL keywords are case-insensitive, so a MIXED-case set-op keyword must fold
// exactly like its UPPER/lower spelling. Regression: the fold matched the
// case-preserved token text against only "UNION"/"union", so `Union` fell
// through, the loop stopped, and the right arm (and operator) were SILENTLY
// dropped -- `SELECT 1 Union SELECT 2` parsed to a lone SELECT 1. Now the fold
// matches on the case-insensitive keyword_id.
TEST_F(PrecedenceRegressionTest, MixedCaseUnionFolds) {
    auto* ast = parse("SELECT 1 Union SELECT 2");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::UnionStmt);
    EXPECT_EQ(ast->child_count, 2u);  // both arms kept, nothing dropped
}

// Mixed-case EXCEPT (any casing) folds like EXCEPT.
TEST_F(PrecedenceRegressionTest, MixedCaseExceptFolds) {
    auto* ast = parse("SELECT 1 ExCePt SELECT 2");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::ExceptStmt);
    EXPECT_EQ(ast->child_count, 2u);
}

// Mixed-case INTERSECT still binds tighter than UNION: `A UNION B Intersect C`
// -> UNION(A, INTERSECT(B, C)). Proves the keyword_id switch preserved both the
// fold and the precedence for a mixed-case inner keyword (regression: the
// `Intersect` arm was dropped, leaving a plain 2-arm UNION).
TEST_F(PrecedenceRegressionTest, MixedCaseIntersectBindsTighterThanUnion) {
    auto* ast = parse("SELECT 1 UNION SELECT 2 Intersect SELECT 3");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::UnionStmt);
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

// A bare top-level VALUES may be the LEFT operand of a set operation. Regression:
// parse_statement dispatched a leading VALUES straight to parse_values_stmt with
// NO set-op fold (unlike the SELECT and leading-'(' paths), so `VALUES (1) UNION
// SELECT 2` silently dropped the operator and the entire right arm, leaving a
// bare ValuesStmt. Now the top-level VALUES is folded like any other operand.
TEST_F(PrecedenceRegressionTest, SetOpBareValuesLeftOperand) {
    {
        auto* root = parse("VALUES (1) UNION SELECT 2");
        ASSERT_NE(root, nullptr);
        EXPECT_EQ(root->node_type, NodeType::UnionStmt);
        EXPECT_EQ(root->child_count, 2u);  // both arms kept
        auto* left = root->first_child;
        auto* right = left ? left->next_sibling : nullptr;
        ASSERT_NE(left, nullptr);
        ASSERT_NE(right, nullptr);
        EXPECT_EQ(left->node_type, NodeType::ValuesStmt);   // VALUES on the left
        EXPECT_EQ(right->node_type, NodeType::SelectStmt);  // SELECT on the right
    }
    {
        auto* root = parse("VALUES (1) INTERSECT SELECT 2");
        ASSERT_NE(root, nullptr);
        EXPECT_EQ(root->node_type, NodeType::IntersectStmt);
        EXPECT_EQ(root->child_count, 2u);
    }
    {
        auto* root = parse("VALUES (1) EXCEPT SELECT 2");
        ASSERT_NE(root, nullptr);
        EXPECT_EQ(root->node_type, NodeType::ExceptStmt);
        EXPECT_EQ(root->child_count, 2u);
    }
}

// A trailing ORDER BY after a `VALUES ... UNION ...` binds to the whole set
// operation; the left VALUES operand does not swallow it.
TEST_F(PrecedenceRegressionTest, SetOpBareValuesLeftOperandTrailingOrderBy) {
    auto* root = parse("VALUES (1) UNION SELECT 2 ORDER BY 1");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::UnionStmt);
    ASTNode* order_by = nullptr;
    ASTNode* values = nullptr;
    for (auto* c = root->first_child; c; c = c->next_sibling) {
        if (c->node_type == NodeType::OrderByClause) order_by = c;
        if (c->node_type == NodeType::ValuesStmt) values = c;
    }
    ASSERT_NE(order_by, nullptr) << "ORDER BY must attach to the UNION node";
    ASSERT_NE(values, nullptr);
    EXPECT_EQ(find(values, NodeType::OrderByClause), nullptr)
        << "the left VALUES operand must not own the trailing ORDER BY";
}

// Control: a standalone top-level VALUES with its own ORDER BY is unchanged (no
// set-op tail, so the fold returns it as-is and it keeps its ORDER BY).
TEST_F(PrecedenceRegressionTest, StandaloneValuesUnchangedByFold) {
    auto* root = parse("VALUES (3), (1), (2) ORDER BY 1");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::ValuesStmt);
    EXPECT_NE(find(root, NodeType::OrderByClause), nullptr);
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

// ---------------------------------------------------------------------------
// GUARDRAIL MATRIX: VALUES as a query primary / set-op operand, and set-op
// keyword casing. Every prior pass fixed ONE adjacent cell of these two
// neighborhoods (VALUES on the RHS, then parenthesized, then bare-LHS, then
// inside parens; UNION casing) - a recurring "fix one, miss the neighbor"
// pattern. This table pins the WHOLE matrix so a future change that drops any
// single cell fails loudly here instead of surviving to the next audit.
// ---------------------------------------------------------------------------

namespace {
// A parsed statement's direct child count.
int direct_children(const ASTNode* n) {
    int c = 0;
    for (const ASTNode* k = n ? n->first_child : nullptr; k; k = k->next_sibling) ++c;
    return c;
}
}  // namespace

// VALUES is a query primary and a legal set-op operand in EVERY position, on
// either side, parenthesized or bare, and nests. None of these may silently drop
// an operator or an arm (the recurring VALUES regression class).
TEST_F(PrecedenceRegressionTest, ValuesSetOpOperandMatrix) {
    struct Case { const char* sql; NodeType root; int min_children; };
    const Case cases[] = {
        // standalone (with / without trailing clauses)
        {"VALUES (1),(2)",                          NodeType::ValuesStmt,    1},
        {"VALUES (1),(2) ORDER BY 1",               NodeType::ValuesStmt,    2},
        {"VALUES (1) LIMIT 5",                      NodeType::ValuesStmt,    2},
        // VALUES as LEFT operand, all three ops (+ ALL)
        {"VALUES (1) UNION SELECT 2",               NodeType::UnionStmt,     2},
        {"VALUES (1) INTERSECT SELECT 2",           NodeType::IntersectStmt, 2},
        {"VALUES (1) EXCEPT SELECT 2",              NodeType::ExceptStmt,    2},
        {"VALUES (1) UNION ALL SELECT 2",           NodeType::UnionStmt,     2},
        // VALUES as RIGHT operand
        {"SELECT 1 UNION VALUES (2)",               NodeType::UnionStmt,     2},
        {"SELECT 1 INTERSECT VALUES (2)",           NodeType::IntersectStmt, 2},
        {"SELECT 1 EXCEPT VALUES (2)",              NodeType::ExceptStmt,    2},
        // parenthesized operands, either side / both
        {"(VALUES (1)) UNION SELECT 2",             NodeType::UnionStmt,     2},
        {"SELECT 1 UNION (VALUES (2))",             NodeType::UnionStmt,     2},
        {"(VALUES (1)) UNION (VALUES (2))",         NodeType::UnionStmt,     2},
        // both operands bare VALUES
        {"VALUES (1) UNION VALUES (2)",             NodeType::UnionStmt,     2},
        // trailing ORDER BY / LIMIT binds to the whole set op (3rd child)
        {"VALUES (1) UNION SELECT 2 ORDER BY 1",    NodeType::UnionStmt,     3},
        {"SELECT 1 UNION VALUES (2) ORDER BY 1",    NodeType::UnionStmt,     3},
        {"VALUES (1) UNION SELECT 2 LIMIT 3",       NodeType::UnionStmt,     3},
        // precedence: a VALUES INTERSECT arm binds tighter than UNION
        {"SELECT 1 UNION VALUES (2) INTERSECT SELECT 3", NodeType::UnionStmt, 2},
        {"VALUES (1) INTERSECT SELECT 2 UNION SELECT 3", NodeType::UnionStmt, 2},
        // chains with VALUES at either / both ends
        {"VALUES (1) UNION SELECT 2 UNION VALUES (3)",   NodeType::UnionStmt, 2},
        {"SELECT 1 UNION SELECT 2 UNION VALUES (3)",     NodeType::UnionStmt, 2},
        // a parenthesized query whose body is itself a set op starting with VALUES
        {"(VALUES (1) UNION SELECT 2)",             NodeType::UnionStmt,     2},
        {"(VALUES (1) UNION SELECT 2) UNION SELECT 3",   NodeType::UnionStmt, 2},
        {"SELECT 1 UNION (VALUES (2) UNION SELECT 3)",   NodeType::UnionStmt, 2},
        {"(VALUES (1) UNION SELECT 2) ORDER BY 1",  NodeType::UnionStmt,     3},
        {"(VALUES (1) UNION SELECT 2 ORDER BY 1)",  NodeType::UnionStmt,     3},
        {"VALUES (1) UNION (VALUES (2) INTERSECT SELECT 3)", NodeType::UnionStmt, 2},
        // VALUES as a derived table stays a SELECT
        {"SELECT * FROM (VALUES (1),(2)) t",        NodeType::SelectStmt,    1},
    };
    for (const Case& c : cases) {
        ASTNode* root = parse(c.sql);
        ASSERT_NE(root, nullptr) << "dropped/failed: " << c.sql;
        EXPECT_EQ(root->node_type, c.root) << c.sql;
        EXPECT_GE(direct_children(root), c.min_children)
            << c.sql << " -> an operator or arm was dropped";
    }
}

// Set-operation keywords are case-insensitive in EVERY casing (UPPER / lower /
// MiXeD), with and without ALL. A mixed-case keyword must fold identically, not
// be dropped (the mixed-case regression class).
TEST_F(PrecedenceRegressionTest, SetOpKeywordCasingMatrix) {
    struct Case { const char* kw; NodeType root; };
    const Case cases[] = {
        {"UNION", NodeType::UnionStmt},     {"union", NodeType::UnionStmt},     {"UnIoN", NodeType::UnionStmt},
        {"INTERSECT", NodeType::IntersectStmt}, {"intersect", NodeType::IntersectStmt}, {"InTeRsEcT", NodeType::IntersectStmt},
        {"EXCEPT", NodeType::ExceptStmt},   {"except", NodeType::ExceptStmt},   {"ExCePt", NodeType::ExceptStmt},
    };
    for (const Case& c : cases) {
        {
            const std::string sql = std::string("SELECT 1 ") + c.kw + " SELECT 2";
            ASTNode* root = parse(sql);
            ASSERT_NE(root, nullptr) << sql;
            EXPECT_EQ(root->node_type, c.root) << sql;
            EXPECT_EQ(direct_children(root), 2) << sql << " -> arm dropped";
        }
        {
            const std::string sql = std::string("SELECT 1 ") + c.kw + " ALL SELECT 2";
            ASTNode* root = parse(sql);
            ASSERT_NE(root, nullptr) << sql;
            EXPECT_EQ(root->node_type, c.root) << sql;
        }
    }
}

// ---------------------------------------------------------------------------
// GUARDRAIL MATRIX: VALUES as a query BODY in every query-block context. VALUES
// is a query primary, so it must parse anywhere a SELECT query body can - a FROM
// derived table, a CTE body, a scalar / IN / EXISTS subquery, and CREATE VIEW AS
// - both bare and as a set-operation. These sites historically each had their
// own query-body dispatch and several accepted only SELECT, silently dropping or
// MIS-PARSING a VALUES body (e.g. `SELECT (VALUES (1))` -> a `VALUES(1)` function
// call; `x IN (VALUES (1),(2))` -> a 2-item value list). All sites now route
// through the shared parse_query_body(); this table pins every context so a
// future site cannot regress to a SELECT-only dispatch.
// ---------------------------------------------------------------------------
namespace {
bool subtree_has(const ASTNode* n, NodeType t) {
    if (!n) return false;
    if (n->node_type == t) return true;
    for (const ASTNode* c = n->first_child; c; c = c->next_sibling)
        if (subtree_has(c, t)) return true;
    return false;
}
}  // namespace

TEST_F(PrecedenceRegressionTest, ValuesQueryBodyEveryContextMatrix) {
    // Each case: the SQL, and the node type its query body must contain. The
    // *_setop variant must fold to a UnionStmt (not drop the arm).
    struct Case { const char* sql; NodeType must_contain; };
    const Case cases[] = {
        // FROM derived table
        {"SELECT * FROM (VALUES (1),(2)) t",                         NodeType::ValuesStmt},
        {"SELECT * FROM (VALUES (1) UNION SELECT 2) t",             NodeType::UnionStmt},
        // CTE body (even the bare form was rejected before)
        {"WITH t AS (VALUES (1),(2)) SELECT * FROM t",              NodeType::ValuesStmt},
        {"WITH t AS (VALUES (1) UNION SELECT 2) SELECT * FROM t",   NodeType::UnionStmt},
        // scalar subquery in the SELECT list
        {"SELECT (VALUES (1))",                                     NodeType::ValuesStmt},
        {"SELECT (VALUES (1) UNION SELECT 2)",                      NodeType::UnionStmt},
        // IN subquery (was mis-parsed as a value list)
        {"SELECT * FROM t WHERE x IN (VALUES (1),(2))",             NodeType::ValuesStmt},
        {"SELECT * FROM t WHERE x IN (VALUES (1) UNION SELECT 2)",  NodeType::UnionStmt},
        // EXISTS subquery
        {"SELECT * FROM t WHERE EXISTS (VALUES (1))",               NodeType::ValuesStmt},
        // CREATE VIEW AS (bare form was silently dropped)
        {"CREATE VIEW v AS VALUES (1),(2)",                        NodeType::ValuesStmt},
        {"CREATE VIEW v AS VALUES (1) UNION SELECT 2",             NodeType::UnionStmt},
    };
    for (const Case& c : cases) {
        ASTNode* root = parse(c.sql);
        ASSERT_NE(root, nullptr) << "dropped/failed to parse: " << c.sql;
        EXPECT_TRUE(subtree_has(root, c.must_contain))
            << c.sql << " -> query body missing/mis-parsed (expected node "
            << static_cast<int>(c.must_contain) << ")";
    }
}

// GUARDRAIL: a parenthesized query body that begins with '(' (a set operation
// over parenthesized branches, `((SELECT 1) UNION (SELECT 2))`) is a valid query
// / subquery wherever a keyword-led one is. The query-body DISPATCH was unified
// earlier, but the ENTRY GATES that decide whether to call it (the FROM derived-
// table vs join-group split, the scalar-subquery test, the IN peek) recognized
// only a leading KEYWORD, so a leading '(' was rejected in FROM / scalar / IN /
// EXISTS while it worked at statement / CTE / CREATE VIEW. This pins every
// context so a leading-'(' query body cannot regress at any gate.
TEST_F(PrecedenceRegressionTest, ParenLedQueryBodyEveryContextMatrix) {
    const char* queries[] = {
        "((SELECT 1) UNION (SELECT 2)) LIMIT 1",                       // statement
        "WITH t AS ((SELECT 1) UNION (SELECT 2)) SELECT * FROM t",     // CTE body
        "CREATE VIEW v AS (SELECT 1) UNION (SELECT 2)",               // view (control)
        "SELECT * FROM ((SELECT 1) UNION (SELECT 2)) t",              // FROM derived
        "SELECT ((SELECT 1) UNION (SELECT 2)) FROM z",                // scalar subquery
        "SELECT * FROM z WHERE x IN ((SELECT 1) UNION (SELECT 2))",   // IN subquery
        "SELECT * FROM z WHERE EXISTS ((SELECT 1) UNION (SELECT 2))", // EXISTS subquery
    };
    for (const char* sql : queries) {
        ASTNode* root = parse(sql);
        ASSERT_NE(root, nullptr) << "rejected a legal parenthesized query body: " << sql;
        EXPECT_NE(find(root, NodeType::UnionStmt), nullptr)
            << sql << " -> the set-op body was dropped / mis-parsed (no UnionStmt)";
    }
    // A parenthesized FROM JOIN GROUP must still NOT be taken as a derived table.
    {
        ASTNode* root = parse("SELECT * FROM (a JOIN b ON a.id = b.id)");
        ASSERT_NE(root, nullptr);
        EXPECT_EQ(find(root, NodeType::Subquery), nullptr)
            << "a FROM join group must not be classified as a derived-table subquery";
    }
    // The double-paren derived table retains its column-alias list `(x)` (was
    // silently dropped when it took the spurious join-group path).
    {
        ASTNode* root = parse("SELECT * FROM ((SELECT 1)) t(x)");
        ASSERT_NE(root, nullptr);
        EXPECT_NE(find(root, NodeType::Subquery), nullptr) << "((SELECT 1)) t(x) is a derived table";
        // The `(x)` alias list is captured as a ColumnList somewhere in the tree.
        EXPECT_NE(find(root, NodeType::ColumnList), nullptr)
            << "the (x) column-alias list must be retained";
    }
}

// The dual of the matrix above: a parenthesized group whose LEFT operand merely
// happens to be a parenthesized subquery - `((SELECT 1) + 2)` - is a grouped
// SCALAR expression, not a query body. The leading-'(' query-body recognizer must
// not swallow the whole group as a subquery and strand the trailing operator
// (which produced a spurious 'Unclosed parenthesis' at every gate: scalar, IN,
// EXISTS, FROM). Each of these is legal SQL (PostgreSQL: SELECT ((SELECT 1)+2)=3).
TEST_F(PrecedenceRegressionTest, ParenSubqueryLeftOperandStaysScalarExpr) {
    // Binary-operator forms: the projection is a BinaryExpr with the subquery as
    // one operand, never a bare Subquery/UnionStmt that ate the whole group.
    const char* binary_queries[] = {
        "SELECT ((SELECT 1) + 2) FROM z",
        "SELECT ((SELECT max(a) FROM t) * 2) FROM z",
        "SELECT ((SELECT 1) = 2) FROM z",
    };
    for (const char* sql : binary_queries) {
        ASTNode* root = parse(sql);
        ASSERT_NE(root, nullptr) << "rejected a legal grouped scalar expression: " << sql;
        ASTNode* proj = first_projection(root);
        ASSERT_NE(proj, nullptr) << sql;
        EXPECT_EQ(proj->node_type, NodeType::BinaryExpr)
            << sql << " -> the grouped scalar expression was mis-parsed as a query body";
        EXPECT_NE(find(root, NodeType::Subquery), nullptr)
            << sql << " -> the subquery operand was lost";
    }
    // All grouped-scalar forms (including the postfix `IS NULL`) must parse and the
    // projection must NOT be swallowed as a query body - it is neither a bare
    // Subquery nor a set-operation node at the projection root.
    const char* scalar_queries[] = {
        "SELECT ((SELECT 1) + 2) FROM z",
        "SELECT ((SELECT 1) = 2) FROM z",
        "SELECT ((SELECT 1) IS NULL) FROM z",
    };
    for (const char* sql : scalar_queries) {
        ASTNode* root = parse(sql);
        ASSERT_NE(root, nullptr) << "rejected a legal grouped scalar expression: " << sql;
        ASTNode* proj = first_projection(root);
        ASSERT_NE(proj, nullptr) << sql;
        EXPECT_NE(proj->node_type, NodeType::Subquery)
            << sql << " -> the group was mis-parsed as a bare subquery";
        EXPECT_EQ(find(proj, NodeType::UnionStmt), nullptr)
            << sql << " -> the group was mis-parsed as a set-operation query body";
        EXPECT_NE(find(root, NodeType::Subquery), nullptr)
            << sql << " -> the subquery operand was lost";
    }
    // Same misfire reached the IN and FROM/derived gates; both must accept these.
    EXPECT_NE(parse("SELECT * FROM z WHERE x IN ((SELECT 1) + 2)"), nullptr)
        << "IN gate rejected `IN ((SELECT 1) + 2)`";
    EXPECT_NE(parse("SELECT * FROM z WHERE EXISTS ((SELECT 1) + 2)"), nullptr)
        << "EXISTS gate rejected `EXISTS ((SELECT 1) + 2)`";
    // A parenthesized FROM join group whose first ref is itself a derived table
    // must still be a join group, not swallowed as one big derived table.
    {
        ASTNode* root = parse("SELECT * FROM ((SELECT 1) t1 JOIN u ON t1.a = u.a)");
        ASSERT_NE(root, nullptr) << "((SELECT 1) t1 JOIN u ...) join group rejected";
    }
    // And the intended #60 query bodies must still be recognized (no over-correction).
    EXPECT_NE(find(parse("SELECT ((SELECT 1) UNION (SELECT 2)) FROM z"), NodeType::UnionStmt), nullptr)
        << "over-corrected: a real parenthesized set-op body is no longer a query body";
    EXPECT_NE(find(parse("SELECT * FROM ((SELECT 1) UNION (SELECT 2)) t"), NodeType::UnionStmt), nullptr)
        << "over-corrected: a real parenthesized set-op derived table is no longer a query body";
}

// -----------------------------------------------------------------------------
// Unary +/- binds LOOSER than the `::type` postfix cast and COLLATE, so the
// postfix must attach to the OPERAND, not re-bind to the whole unary node.
// Postgres precedence: `::`, `[]`, unary `+`/`-`, `^`, ... Before the fix
// `-a::int` parsed as `(-a)::int` (root CastExpr over a UnaryExpr); it must be
// `-(a::int)` (root UnaryExpr over a CastExpr).
TEST_F(PrecedenceRegressionTest, UnaryMinusBindsLooserThanCast) {
    auto* root = first_projection(parse("SELECT -a::int FROM t"));
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::UnaryExpr);        // `-` is the root
    EXPECT_EQ(root->primary_text, "-");
    auto* inner = root->first_child;
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->node_type, NodeType::CastExpr)          // cast is UNDER the minus
        << "-a::int must parse as -(a::int), not (-a)::int";
    // cast children are [value, Identifier(type)]; the type is `int`.
    auto* type_node = inner->first_child ? inner->first_child->next_sibling : nullptr;
    ASSERT_NE(type_node, nullptr);
    EXPECT_EQ(type_node->primary_text, "int");
}

// `-'5'::int` must be `-( '5'::int )` = -5, not unary-minus over a bare string
// literal (which the analyzer would reject as a type error on a legal query).
TEST_F(PrecedenceRegressionTest, UnaryMinusOverCastStringLiteral) {
    auto* root = first_projection(parse("SELECT -'5'::int"));
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::UnaryExpr);
    ASSERT_NE(root->first_child, nullptr);
    EXPECT_EQ(root->first_child->node_type, NodeType::CastExpr)
        << "-'5'::int must parse as -('5'::int)";
}

// COLLATE is also a tighter-binding postfix: `-a COLLATE \"C\"` is
// `-(a COLLATE \"C\")`, root UnaryExpr over a CollateClause.
TEST_F(PrecedenceRegressionTest, UnaryMinusBindsLooserThanCollate) {
    auto* root = first_projection(parse("SELECT -a COLLATE \"C\" FROM t"));
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->node_type, NodeType::UnaryExpr);
    ASSERT_NE(root->first_child, nullptr);
    EXPECT_EQ(root->first_child->node_type, NodeType::CollateClause)
        << "-a COLLATE \"C\" must parse as -(a COLLATE \"C\")";
}

// Guard against over-correction: a plain unary minus with no postfix is
// unchanged, and `(-a)::int` with explicit parens still casts the negation.
TEST_F(PrecedenceRegressionTest, UnaryMinusPlainAndParenthesizedCastUnchanged) {
    auto* plain = first_projection(parse("SELECT -a FROM t"));
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(plain->node_type, NodeType::UnaryExpr);
    ASSERT_NE(plain->first_child, nullptr);
    EXPECT_NE(plain->first_child->node_type, NodeType::CastExpr);

    auto* paren = first_projection(parse("SELECT (-a)::int FROM t"));
    ASSERT_NE(paren, nullptr);
    EXPECT_EQ(paren->node_type, NodeType::CastExpr)         // explicit parens: cast is root
        << "(-a)::int must still cast the negation";
    ASSERT_NE(paren->first_child, nullptr);
    EXPECT_EQ(paren->first_child->node_type, NodeType::UnaryExpr);
}
