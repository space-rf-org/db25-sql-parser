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

// A SET assignment target must be a bare column name. The parenthesized SQL
// row-assignment form `SET (a, b) = (1, 2)` is not supported; it must be
// REJECTED cleanly, never silently parsed as an UpdateStmt with an empty SET and
// every following clause (WHERE / FROM / RETURNING) dropped. Regression: the SET
// loop broke on the leading '(' without advancing, so the WHERE filter vanished
// with no diagnostic - an UPDATE that would touch every row.
TEST_F(DMLParseTest, UpdateParenthesizedRowAssignmentRejected) {
    EXPECT_EQ(parse("UPDATE t SET (a, b) = (1, 2) WHERE id = 1"), nullptr);
    EXPECT_EQ(parse("UPDATE t SET (a, b) = (1, 2)"), nullptr);
    // A partial parenthesized target after a valid one is rejected too (it must
    // not silently keep only the first assignment and drop the rest + WHERE).
    EXPECT_EQ(parse("UPDATE t SET a = 1, (b, c) = (2, 3) WHERE id = 1"), nullptr);
    // A target with no '=' is likewise malformed, not a silent empty SET.
    EXPECT_EQ(parse("UPDATE t SET a b WHERE id = 1"), nullptr);

    // The ordinary column-assignment forms still parse, WHERE intact.
    auto* ok = parse("UPDATE t SET a = 1, b = 2 WHERE id = 1");
    ASSERT_NE(ok, nullptr);
    EXPECT_EQ(ok->node_type, NodeType::UpdateStmt);
    auto* set = find_child(ok, NodeType::SetClause);
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->child_count, 2u);
    ASSERT_NE(find_child(ok, NodeType::WhereClause), nullptr);
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

// ============================================================================
// UPDATE / DELETE RETURNING - full expression items (pass-29 audit)
// ============================================================================
// The UPDATE and DELETE RETURNING paths parsed each item with parse_column_ref,
// which read only a leading bare column: `RETURNING id, k+1, name` truncated to
// [id, k] (the `+1` desynced the comma loop, dropping `name`), silently losing
// output columns. Both paths now use the shared full-expression helper.

TEST_F(DMLParseTest, UpdateReturningExpressionItemsNotTruncated) {
    auto* ast = parse("UPDATE t SET x = 1 WHERE id = 1 RETURNING id, k + 1, name");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(parser.trailing_token_count(), 0u);
    auto* ret = find_child(ast, NodeType::ReturningClause);
    ASSERT_NE(ret, nullptr);
    EXPECT_EQ(ret->child_count, 3u) << "RETURNING must keep all three items";
}

TEST_F(DMLParseTest, DeleteReturningExpressionItemsNotTruncated) {
    auto* ast = parse("DELETE FROM t WHERE id = 1 RETURNING id, k + 1, name");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(parser.trailing_token_count(), 0u);
    auto* ret = find_child(ast, NodeType::ReturningClause);
    ASSERT_NE(ret, nullptr);
    EXPECT_EQ(ret->child_count, 3u) << "RETURNING must keep all three items";
}

TEST_F(DMLParseTest, UpdateReturningSingleExpression) {
    auto* ast = parse("UPDATE t SET x = 1 RETURNING id + 1");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(parser.trailing_token_count(), 0u);
    auto* ret = find_child(ast, NodeType::ReturningClause);
    ASSERT_NE(ret, nullptr);
    EXPECT_EQ(ret->child_count, 1u);
}

TEST_F(DMLParseTest, DeleteReturningStarStillWorks) {
    auto* ast = parse("DELETE FROM t RETURNING *");
    ASSERT_NE(ast, nullptr);
    auto* ret = find_child(ast, NodeType::ReturningClause);
    ASSERT_NE(ret, nullptr);
    EXPECT_EQ(ret->child_count, 1u);
    ASSERT_NE(ret->first_child, nullptr);
    EXPECT_EQ(ret->first_child->node_type, NodeType::Star);
}

// A dangling RETURNING keyword must still be rejected on both paths.
TEST_F(DMLParseTest, UpdateDanglingReturningRejected) {
    EXPECT_EQ(parse("UPDATE t SET x = 1 RETURNING"), nullptr);
}

TEST_F(DMLParseTest, DeleteDanglingReturningRejected) {
    parser.reset();
    EXPECT_EQ(parse("DELETE FROM t RETURNING"), nullptr);
}
