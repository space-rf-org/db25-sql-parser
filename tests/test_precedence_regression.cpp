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
