/*
 * Test SQL-specific operators: BETWEEN, IN, LIKE, IS NULL/IS NOT NULL, NOT, unary +/-
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include "db25/ast/node_types.hpp"
#include <string>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class SQLOperatorsTest : public ::testing::Test {
protected:
    std::unique_ptr<Parser> parser;
    
    void SetUp() override {
        parser = std::make_unique<Parser>();
    }
    
    ASTNode* parse(const std::string& sql) {
        auto result = parser->parse(sql);
        return result.has_value() ? result.value() : nullptr;
    }
    
    ASTNode* find_node_by_type(ASTNode* root, NodeType type) {
        if (!root) return nullptr;
        if (root->node_type == type) return root;
        
        auto* child = root->first_child;
        while (child) {
            auto* found = find_node_by_type(child, type);
            if (found) return found;
            child = child->next_sibling;
        }
        return nullptr;
    }
};

// Test BETWEEN operator
TEST_F(SQLOperatorsTest, BetweenOperator) {
    auto* ast = parse("SELECT * FROM products WHERE price BETWEEN 10 AND 100");
    ASSERT_NE(ast, nullptr);
    
    auto* between = find_node_by_type(ast, NodeType::BetweenExpr);
    ASSERT_NE(between, nullptr);
    EXPECT_EQ(between->primary_text, "BETWEEN");
    EXPECT_EQ(between->child_count, 3); // value, lower, upper
    
    // First child should be the column ref
    auto* value = between->first_child;
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->node_type, NodeType::ColumnRef);
    EXPECT_EQ(value->primary_text, "price");
    
    // Second child should be lower bound
    auto* lower = value->next_sibling;
    ASSERT_NE(lower, nullptr);
    EXPECT_EQ(lower->node_type, NodeType::IntegerLiteral);
    EXPECT_EQ(lower->primary_text, "10");
    
    // Third child should be upper bound
    auto* upper = lower->next_sibling;
    ASSERT_NE(upper, nullptr);
    EXPECT_EQ(upper->node_type, NodeType::IntegerLiteral);
    EXPECT_EQ(upper->primary_text, "100");
}

// Test IN operator
TEST_F(SQLOperatorsTest, InOperator) {
    auto* ast = parse("SELECT * FROM users WHERE status IN ('active', 'pending', 'verified')");
    ASSERT_NE(ast, nullptr);
    
    auto* in_expr = find_node_by_type(ast, NodeType::InExpr);
    ASSERT_NE(in_expr, nullptr);
    EXPECT_EQ(in_expr->primary_text, "IN");
    EXPECT_EQ(in_expr->child_count, 4); // column + 3 values
    
    // First child should be column
    auto* column = in_expr->first_child;
    ASSERT_NE(column, nullptr);
    EXPECT_EQ(column->node_type, NodeType::ColumnRef);
    EXPECT_EQ(column->primary_text, "status");
    
    // Next children should be string literals
    auto* val1 = column->next_sibling;
    ASSERT_NE(val1, nullptr);
    EXPECT_EQ(val1->node_type, NodeType::StringLiteral);
    
    auto* val2 = val1->next_sibling;
    ASSERT_NE(val2, nullptr);
    EXPECT_EQ(val2->node_type, NodeType::StringLiteral);
    
    auto* val3 = val2->next_sibling;
    ASSERT_NE(val3, nullptr);
    EXPECT_EQ(val3->node_type, NodeType::StringLiteral);
}

// Test IN with numbers
TEST_F(SQLOperatorsTest, InOperatorNumbers) {
    auto* ast = parse("SELECT * FROM orders WHERE order_id IN (1, 2, 3, 4, 5)");
    ASSERT_NE(ast, nullptr);
    
    auto* in_expr = find_node_by_type(ast, NodeType::InExpr);
    ASSERT_NE(in_expr, nullptr);
    EXPECT_EQ(in_expr->child_count, 6); // column + 5 numbers
}

// Test LIKE operator
TEST_F(SQLOperatorsTest, LikeOperator) {
    auto* ast = parse("SELECT * FROM customers WHERE name LIKE '%Smith%'");
    ASSERT_NE(ast, nullptr);
    
    auto* like_expr = find_node_by_type(ast, NodeType::LikeExpr);
    ASSERT_NE(like_expr, nullptr);
    EXPECT_EQ(like_expr->primary_text, "LIKE");
    EXPECT_EQ(like_expr->child_count, 2); // column, pattern
    
    // First child should be column
    auto* column = like_expr->first_child;
    ASSERT_NE(column, nullptr);
    EXPECT_EQ(column->node_type, NodeType::ColumnRef);
    EXPECT_EQ(column->primary_text, "name");
    
    // Second child should be pattern
    auto* pattern = column->next_sibling;
    ASSERT_NE(pattern, nullptr);
    EXPECT_EQ(pattern->node_type, NodeType::StringLiteral);
}

// Test IS NULL
TEST_F(SQLOperatorsTest, IsNullOperator) {
    auto* ast = parse("SELECT * FROM users WHERE deleted_at IS NULL");
    ASSERT_NE(ast, nullptr);
    
    auto* is_null = find_node_by_type(ast, NodeType::IsNullExpr);
    ASSERT_NE(is_null, nullptr);
    EXPECT_EQ(is_null->primary_text, "IS NULL");
    EXPECT_EQ(is_null->child_count, 1); // just the column
    
    auto* column = is_null->first_child;
    ASSERT_NE(column, nullptr);
    EXPECT_EQ(column->node_type, NodeType::ColumnRef);
    EXPECT_EQ(column->primary_text, "deleted_at");
}

// Test IS NOT NULL
TEST_F(SQLOperatorsTest, IsNotNullOperator) {
    auto* ast = parse("SELECT * FROM users WHERE email IS NOT NULL");
    ASSERT_NE(ast, nullptr);
    
    auto* is_not_null = find_node_by_type(ast, NodeType::IsNullExpr);
    ASSERT_NE(is_not_null, nullptr);
    EXPECT_EQ(is_not_null->primary_text, "IS NOT NULL");
    EXPECT_EQ(is_not_null->child_count, 1);
}

// Test NOT operator
TEST_F(SQLOperatorsTest, NotOperator) {
    auto* ast = parse("SELECT * FROM users WHERE NOT active");
    ASSERT_NE(ast, nullptr);
    
    auto* not_expr = find_node_by_type(ast, NodeType::UnaryExpr);
    ASSERT_NE(not_expr, nullptr);
    EXPECT_EQ(not_expr->primary_text, "NOT");
    EXPECT_EQ(not_expr->child_count, 1);
    
    auto* operand = not_expr->first_child;
    ASSERT_NE(operand, nullptr);
    EXPECT_EQ(operand->node_type, NodeType::ColumnRef);
    EXPECT_EQ(operand->primary_text, "active");
}

// Test NOT with comparison
TEST_F(SQLOperatorsTest, NotWithComparison) {
    auto* ast = parse("SELECT * FROM users WHERE NOT age > 18");
    ASSERT_NE(ast, nullptr);
    
    auto* not_expr = find_node_by_type(ast, NodeType::UnaryExpr);
    ASSERT_NE(not_expr, nullptr);
    EXPECT_EQ(not_expr->primary_text, "NOT");
    
    auto* comparison = not_expr->first_child;
    ASSERT_NE(comparison, nullptr);
    EXPECT_EQ(comparison->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(comparison->primary_text, ">");
}

// Test unary minus
TEST_F(SQLOperatorsTest, UnaryMinus) {
    auto* ast = parse("SELECT -price FROM products");
    ASSERT_NE(ast, nullptr);
    
    auto* unary = find_node_by_type(ast, NodeType::UnaryExpr);
    ASSERT_NE(unary, nullptr);
    EXPECT_EQ(unary->primary_text, "-");
    EXPECT_EQ(unary->child_count, 1);
    
    auto* operand = unary->first_child;
    ASSERT_NE(operand, nullptr);
    EXPECT_EQ(operand->node_type, NodeType::ColumnRef);
}

// Test negative number literal
TEST_F(SQLOperatorsTest, NegativeNumberLiteral) {
    auto* ast = parse("SELECT * FROM products WHERE price > -100");
    ASSERT_NE(ast, nullptr);
    
    auto* comparison = find_node_by_type(ast, NodeType::BinaryExpr);
    ASSERT_NE(comparison, nullptr);
    
    // Right side should be negative number
    auto* right = comparison->first_child->next_sibling;
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(right->node_type, NodeType::IntegerLiteral);
    EXPECT_EQ(right->primary_text, "-100");
}

// Test complex expression with multiple operators
TEST_F(SQLOperatorsTest, ComplexExpression) {
    auto* ast = parse("SELECT * FROM products WHERE price BETWEEN 10 AND 100 AND category IN ('electronics', 'books') AND name LIKE '%sale%'");
    ASSERT_NE(ast, nullptr);
    
    // Should have BETWEEN, IN, and LIKE nodes
    EXPECT_NE(find_node_by_type(ast, NodeType::BetweenExpr), nullptr);
    EXPECT_NE(find_node_by_type(ast, NodeType::InExpr), nullptr);
    EXPECT_NE(find_node_by_type(ast, NodeType::LikeExpr), nullptr);
}

// Test operator precedence with new operators
TEST_F(SQLOperatorsTest, OperatorPrecedence) {
    auto* ast = parse("SELECT * FROM users WHERE age > 18 AND status IN ('active', 'verified') OR deleted_at IS NULL");
    ASSERT_NE(ast, nullptr);
    
    // Root of WHERE should be OR (lowest precedence)
    auto* where = find_node_by_type(ast, NodeType::WhereClause);
    ASSERT_NE(where, nullptr);
    auto* or_expr = where->first_child;
    ASSERT_NE(or_expr, nullptr);
    EXPECT_EQ(or_expr->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(or_expr->primary_text, "OR");
    
    // Left side of OR should be AND
    auto* and_expr = or_expr->first_child;
    ASSERT_NE(and_expr, nullptr);
    EXPECT_EQ(and_expr->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(and_expr->primary_text, "AND");
}