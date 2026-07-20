/*
 * DB25 Parser - DML Statement Tests
 * Regression coverage for INSERT column lists, UPDATE/DELETE WHERE clauses,
 * negative LIMIT operands, and numeric literal typing.
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class DMLParseTest : public ::testing::Test {
protected:
    Parser parser;

    void SetUp() override {
        parser.reset();
    }

    ASTNode* parse(const std::string& sql) {
        auto result = parser.parse(sql);
        if (!result.has_value()) {
            return nullptr;
        }
        return result.value();
    }

    static ASTNode* find_child(ASTNode* node, NodeType type) {
        for (auto* c = node ? node->first_child : nullptr; c; c = c->next_sibling) {
            if (c->node_type == type) {
                return c;
            }
        }
        return nullptr;
    }
};

// ============================================================================
// INSERT explicit column list
// ============================================================================

TEST_F(DMLParseTest, InsertWithColumnList) {
    auto* ast = parse("INSERT INTO t (a, b) VALUES (1, 2)");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::InsertStmt);

    // The parenthesised column list must be parsed into a ColumnList child.
    auto* column_list = find_child(ast, NodeType::ColumnList);
    ASSERT_NE(column_list, nullptr);
    EXPECT_EQ(column_list->child_count, 2u);

    auto* col_a = column_list->first_child;
    ASSERT_NE(col_a, nullptr);
    EXPECT_EQ(col_a->primary_text, "a");
    auto* col_b = col_a->next_sibling;
    ASSERT_NE(col_b, nullptr);
    EXPECT_EQ(col_b->primary_text, "b");

    // The VALUES clause must survive alongside the column list.
    auto* values = find_child(ast, NodeType::ValuesClause);
    ASSERT_NE(values, nullptr);
    ASSERT_NE(values->first_child, nullptr);
    EXPECT_EQ(values->first_child->child_count, 2u);  // (1, 2)
}

TEST_F(DMLParseTest, InsertWithoutColumnListStillWorks) {
    auto* ast = parse("INSERT INTO t VALUES (1, 2)");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::InsertStmt);

    // No column list should be present for the unqualified form.
    EXPECT_EQ(find_child(ast, NodeType::ColumnList), nullptr);

    auto* values = find_child(ast, NodeType::ValuesClause);
    ASSERT_NE(values, nullptr);
    ASSERT_NE(values->first_child, nullptr);
    EXPECT_EQ(values->first_child->child_count, 2u);
}

// ============================================================================
// UPDATE / DELETE WHERE clause
// ============================================================================

TEST_F(DMLParseTest, UpdateWhereClause) {
    auto* ast = parse("UPDATE t SET a=1 WHERE a=2");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::UpdateStmt);

    auto* where = find_child(ast, NodeType::WhereClause);
    ASSERT_NE(where, nullptr);

    // The condition must be a real predicate, not a stray ColumnRef "WHERE".
    ASSERT_NE(where->first_child, nullptr);
    EXPECT_NE(where->first_child->node_type, NodeType::ColumnRef);
    EXPECT_EQ(where->first_child->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(where->first_child->child_count, 2u);
}

TEST_F(DMLParseTest, DeleteWhereClause) {
    auto* ast = parse("DELETE FROM t WHERE a=2");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::DeleteStmt);

    auto* where = find_child(ast, NodeType::WhereClause);
    ASSERT_NE(where, nullptr);

    ASSERT_NE(where->first_child, nullptr);
    EXPECT_NE(where->first_child->node_type, NodeType::ColumnRef);
    EXPECT_EQ(where->first_child->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(where->first_child->child_count, 2u);
}

// ============================================================================
// LIMIT negative operand
// ============================================================================

TEST_F(DMLParseTest, LimitNegativeOperandPreserved) {
    auto* ast = parse("SELECT * FROM t LIMIT -1");
    ASSERT_NE(ast, nullptr);

    auto* limit = find_child(ast, NodeType::LimitClause);
    ASSERT_NE(limit, nullptr);

    // The operand must be present and carry the negative value.
    ASSERT_NE(limit->first_child, nullptr);
    EXPECT_EQ(limit->first_child->node_type, NodeType::IntegerLiteral);
    EXPECT_EQ(limit->first_child->primary_text, "-1");
}

TEST_F(DMLParseTest, LimitPositiveOperandStillWorks) {
    auto* ast = parse("SELECT * FROM t LIMIT 5");
    ASSERT_NE(ast, nullptr);

    auto* limit = find_child(ast, NodeType::LimitClause);
    ASSERT_NE(limit, nullptr);
    ASSERT_NE(limit->first_child, nullptr);
    EXPECT_EQ(limit->first_child->primary_text, "5");
}

// ============================================================================
// Numeric literal typing
// ============================================================================

TEST_F(DMLParseTest, FloatLiteralTyping) {
    auto* ast = parse("SELECT 1.5");
    ASSERT_NE(ast, nullptr);
    auto* select_list = find_child(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    ASSERT_NE(select_list->first_child, nullptr);
    EXPECT_EQ(select_list->first_child->node_type, NodeType::FloatLiteral);
    EXPECT_EQ(select_list->first_child->primary_text, "1.5");
}

TEST_F(DMLParseTest, ExponentLiteralIsFloat) {
    auto* ast = parse("SELECT 1e3");
    ASSERT_NE(ast, nullptr);
    auto* select_list = find_child(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    ASSERT_NE(select_list->first_child, nullptr);
    EXPECT_EQ(select_list->first_child->node_type, NodeType::FloatLiteral);
}

TEST_F(DMLParseTest, IntegerLiteralTyping) {
    auto* ast = parse("SELECT 1");
    ASSERT_NE(ast, nullptr);
    auto* select_list = find_child(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    ASSERT_NE(select_list->first_child, nullptr);
    EXPECT_EQ(select_list->first_child->node_type, NodeType::IntegerLiteral);
    EXPECT_EQ(select_list->first_child->primary_text, "1");
}
