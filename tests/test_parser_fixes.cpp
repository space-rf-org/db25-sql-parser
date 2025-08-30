/*
 * Test file for recent parser fixes:
 * - Alias parsing (AS keyword)
 * - DISTINCT in aggregates
 * - ORDER BY with functions and LIMIT
 * - ASC/DESC direction storage
 * - NOT operators (NOT LIKE, NOT IN, NOT BETWEEN)
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include "db25/ast/node_types.hpp"
#include <string>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class ParserFixesTest : public ::testing::Test {
protected:
    std::unique_ptr<Parser> parser;
    
    void SetUp() override {
        parser = std::make_unique<Parser>();
    }
    
    ASTNode* parse(const std::string& sql) {
        auto result = parser->parse(sql);
        if (!result.has_value()) {
            ADD_FAILURE() << "Parse error: " << result.error().message;
            return nullptr;
        }
        return result.value();
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
    
    bool has_flag(ASTNode* node, uint16_t flag) {
        return node && (node->semantic_flags & flag) != 0;
    }
};

// ========== Alias Parsing Tests ==========

TEST_F(ParserFixesTest, SelectColumnAlias) {
    auto* ast = parse("SELECT name AS customer_name FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_node_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    auto* col = select_list->first_child;
    ASSERT_NE(col, nullptr);
    
    // Check alias flag and value
    EXPECT_TRUE(has_flag(col, static_cast<uint16_t>(NodeFlags::HasAlias)));
    EXPECT_EQ(col->schema_name, "customer_name");
}

TEST_F(ParserFixesTest, TableAlias) {
    auto* ast = parse("SELECT u.id FROM users u");
    ASSERT_NE(ast, nullptr);
    
    auto* table_ref = find_node_by_type(ast, NodeType::TableRef);
    ASSERT_NE(table_ref, nullptr);
    
    EXPECT_TRUE(has_flag(table_ref, static_cast<uint16_t>(NodeFlags::HasAlias)));
    EXPECT_EQ(table_ref->schema_name, "u");
    EXPECT_EQ(table_ref->primary_text, "users");
}

TEST_F(ParserFixesTest, FunctionAlias) {
    auto* ast = parse("SELECT COUNT(*) AS total FROM orders");
    ASSERT_NE(ast, nullptr);
    
    auto* func = find_node_by_type(ast, NodeType::FunctionCall);
    ASSERT_NE(func, nullptr);
    
    EXPECT_TRUE(has_flag(func, static_cast<uint16_t>(NodeFlags::HasAlias)));
    EXPECT_EQ(func->schema_name, "total");
}

// ========== DISTINCT in Aggregates Tests ==========

TEST_F(ParserFixesTest, CountDistinct) {
    auto* ast = parse("SELECT COUNT(DISTINCT user_id) FROM orders");
    ASSERT_NE(ast, nullptr);
    
    auto* func = find_node_by_type(ast, NodeType::FunctionCall);
    ASSERT_NE(func, nullptr);
    
    EXPECT_EQ(func->primary_text, "COUNT");
    EXPECT_TRUE(has_flag(func, static_cast<uint16_t>(NodeFlags::Distinct)));
    
    // Should have the argument
    EXPECT_EQ(func->child_count, 1);
    EXPECT_NE(func->first_child, nullptr);
}

TEST_F(ParserFixesTest, CountDistinctQualified) {
    auto* ast = parse("SELECT COUNT(DISTINCT o.user_id) FROM orders o");
    ASSERT_NE(ast, nullptr);
    
    auto* func = find_node_by_type(ast, NodeType::FunctionCall);
    ASSERT_NE(func, nullptr);
    
    EXPECT_TRUE(has_flag(func, static_cast<uint16_t>(NodeFlags::Distinct)));
    
    // Check the argument is a column reference
    auto* arg = func->first_child;
    ASSERT_NE(arg, nullptr);
    EXPECT_EQ(arg->node_type, NodeType::ColumnRef);
    EXPECT_EQ(arg->primary_text, "o.user_id");
}

// ========== ORDER BY with Functions and LIMIT Tests ==========

TEST_F(ParserFixesTest, OrderByFunctionWithLimit) {
    auto* ast = parse("SELECT name FROM users ORDER BY LENGTH(name) LIMIT 10");
    ASSERT_NE(ast, nullptr);
    
    // Should have both ORDER BY and LIMIT
    auto* order_by = find_node_by_type(ast, NodeType::OrderByClause);
    auto* limit = find_node_by_type(ast, NodeType::LimitClause);
    
    ASSERT_NE(order_by, nullptr);
    ASSERT_NE(limit, nullptr);
    
    // ORDER BY should contain a function call
    auto* func = order_by->first_child;
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->node_type, NodeType::FunctionCall);
    EXPECT_EQ(func->primary_text, "LENGTH");
    
    // LIMIT should have the value
    auto* limit_val = limit->first_child;
    ASSERT_NE(limit_val, nullptr);
    EXPECT_EQ(limit_val->primary_text, "10");
}

TEST_F(ParserFixesTest, OrderByComplexFunction) {
    auto* ast = parse("SELECT * FROM products ORDER BY COALESCE(sale_price, price) LIMIT 5");
    ASSERT_NE(ast, nullptr);
    
    auto* order_by = find_node_by_type(ast, NodeType::OrderByClause);
    ASSERT_NE(order_by, nullptr);
    
    auto* func = order_by->first_child;
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->node_type, NodeType::FunctionCall);
    EXPECT_EQ(func->primary_text, "COALESCE");
    EXPECT_EQ(func->child_count, 2); // Two arguments
}

// ========== ASC/DESC Direction Tests ==========

TEST_F(ParserFixesTest, OrderByDesc) {
    auto* ast = parse("SELECT name FROM users ORDER BY age DESC");
    ASSERT_NE(ast, nullptr);
    
    auto* order_by = find_node_by_type(ast, NodeType::OrderByClause);
    ASSERT_NE(order_by, nullptr);
    
    auto* order_item = order_by->first_child;
    ASSERT_NE(order_item, nullptr);
    
    // Check DESC flag (bit 7 = 0x80)
    EXPECT_TRUE(has_flag(order_item, 0x80));
}

TEST_F(ParserFixesTest, OrderByAsc) {
    auto* ast = parse("SELECT name FROM users ORDER BY age ASC");
    ASSERT_NE(ast, nullptr);
    
    auto* order_by = find_node_by_type(ast, NodeType::OrderByClause);
    ASSERT_NE(order_by, nullptr);
    
    auto* order_item = order_by->first_child;
    ASSERT_NE(order_item, nullptr);
    
    // ASC is default, no DESC flag
    EXPECT_FALSE(has_flag(order_item, 0x80));
}

TEST_F(ParserFixesTest, OrderByFunctionDesc) {
    auto* ast = parse("SELECT name FROM users ORDER BY LENGTH(name) DESC");
    ASSERT_NE(ast, nullptr);
    
    auto* order_by = find_node_by_type(ast, NodeType::OrderByClause);
    ASSERT_NE(order_by, nullptr);
    
    auto* func = order_by->first_child;
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->node_type, NodeType::FunctionCall);
    
    // DESC flag should be on the function node
    EXPECT_TRUE(has_flag(func, 0x80));
}

// ========== NOT Operator Tests ==========

TEST_F(ParserFixesTest, NotLike) {
    auto* ast = parse("SELECT * FROM users WHERE name NOT LIKE '%admin%'");
    ASSERT_NE(ast, nullptr);
    
    auto* like_expr = find_node_by_type(ast, NodeType::LikeExpr);
    ASSERT_NE(like_expr, nullptr);
    
    // Check NOT flag (bit 6 = 0x40)
    EXPECT_TRUE(has_flag(like_expr, 0x40));
    EXPECT_EQ(like_expr->primary_text, "NOT LIKE");
    
    // Should have two children: left operand and pattern
    EXPECT_EQ(like_expr->child_count, 2);
}

TEST_F(ParserFixesTest, NotIn) {
    auto* ast = parse("SELECT * FROM orders WHERE status NOT IN ('cancelled', 'refunded')");
    ASSERT_NE(ast, nullptr);
    
    auto* in_expr = find_node_by_type(ast, NodeType::InExpr);
    ASSERT_NE(in_expr, nullptr);
    
    EXPECT_TRUE(has_flag(in_expr, 0x40));
    EXPECT_EQ(in_expr->primary_text, "NOT IN");
    
    // Should have 3 children: left operand + 2 list items
    EXPECT_EQ(in_expr->child_count, 3);
}

TEST_F(ParserFixesTest, NotBetween) {
    auto* ast = parse("SELECT * FROM products WHERE price NOT BETWEEN 10 AND 100");
    ASSERT_NE(ast, nullptr);
    
    auto* between_expr = find_node_by_type(ast, NodeType::BetweenExpr);
    ASSERT_NE(between_expr, nullptr);
    
    EXPECT_TRUE(has_flag(between_expr, 0x40));
    EXPECT_EQ(between_expr->primary_text, "NOT BETWEEN");
    
    // Should have 3 children: value, lower bound, upper bound
    EXPECT_EQ(between_expr->child_count, 3);
}

TEST_F(ParserFixesTest, IsNotNull) {
    auto* ast = parse("SELECT * FROM users WHERE email IS NOT NULL");
    ASSERT_NE(ast, nullptr);
    
    auto* is_null = find_node_by_type(ast, NodeType::IsNullExpr);
    ASSERT_NE(is_null, nullptr);
    
    EXPECT_EQ(is_null->primary_text, "IS NOT NULL");
    EXPECT_EQ(is_null->child_count, 1);
}

// ========== Complex Query Tests ==========

TEST_F(ParserFixesTest, ComplexQueryWithAllFeatures) {
    auto* ast = parse(
        "SELECT DISTINCT u.name AS customer, "
        "COUNT(DISTINCT o.id) AS order_count "
        "FROM users u "
        "LEFT JOIN orders o ON u.id = o.user_id "
        "WHERE u.status NOT IN ('banned', 'suspended') "
        "GROUP BY u.id "
        "HAVING COUNT(o.id) > 0 "
        "ORDER BY order_count DESC "
        "LIMIT 10"
    );
    ASSERT_NE(ast, nullptr);
    
    // Check DISTINCT on SELECT
    EXPECT_TRUE(has_flag(ast, static_cast<uint16_t>(NodeFlags::Distinct)));
    
    // Check aliases
    auto* select_list = find_node_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    auto* first_col = select_list->first_child;
    ASSERT_NE(first_col, nullptr);
    EXPECT_TRUE(has_flag(first_col, static_cast<uint16_t>(NodeFlags::HasAlias)));
    
    // Check NOT IN
    auto* in_expr = find_node_by_type(ast, NodeType::InExpr);
    ASSERT_NE(in_expr, nullptr);
    EXPECT_TRUE(has_flag(in_expr, 0x40));
    
    // Check ORDER BY DESC
    auto* order_by = find_node_by_type(ast, NodeType::OrderByClause);
    ASSERT_NE(order_by, nullptr);
    auto* order_item = order_by->first_child;
    ASSERT_NE(order_item, nullptr);
    EXPECT_TRUE(has_flag(order_item, 0x80));
    
    // Check LIMIT exists
    auto* limit = find_node_by_type(ast, NodeType::LimitClause);
    ASSERT_NE(limit, nullptr);
}