/*
 * Comprehensive JOIN Testing
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include "db25/ast/node_types.hpp"
#include <string>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class JoinComprehensiveTest : public ::testing::Test {
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
    
    int count_nodes_of_type(ASTNode* root, NodeType type) {
        if (!root) return 0;
        int count = (root->node_type == type) ? 1 : 0;
        
        auto* child = root->first_child;
        while (child) {
            count += count_nodes_of_type(child, type);
            child = child->next_sibling;
        }
        return count;
    }
};

// Test different JOIN types
TEST_F(JoinComprehensiveTest, InnerJoin) {
    auto* ast = parse("SELECT * FROM users INNER JOIN orders ON users.id = orders.user_id");
    ASSERT_NE(ast, nullptr);
    
    auto* join = find_node_by_type(ast, NodeType::JoinClause);
    ASSERT_NE(join, nullptr);
    EXPECT_EQ(join->primary_text, "INNER JOIN");
    EXPECT_EQ(join->child_count, 2); // table and condition
}

TEST_F(JoinComprehensiveTest, LeftJoin) {
    auto* ast = parse("SELECT * FROM users LEFT JOIN orders ON users.id = orders.user_id");
    ASSERT_NE(ast, nullptr);
    
    auto* join = find_node_by_type(ast, NodeType::JoinClause);
    ASSERT_NE(join, nullptr);
    EXPECT_EQ(join->primary_text, "LEFT JOIN");
}

TEST_F(JoinComprehensiveTest, LeftOuterJoin) {
    auto* ast = parse("SELECT * FROM users LEFT OUTER JOIN orders ON users.id = orders.user_id");
    ASSERT_NE(ast, nullptr);
    
    auto* join = find_node_by_type(ast, NodeType::JoinClause);
    ASSERT_NE(join, nullptr);
    EXPECT_EQ(join->primary_text, "LEFT OUTER JOIN");
}

TEST_F(JoinComprehensiveTest, RightJoin) {
    auto* ast = parse("SELECT * FROM users RIGHT JOIN orders ON users.id = orders.user_id");
    ASSERT_NE(ast, nullptr);
    
    auto* join = find_node_by_type(ast, NodeType::JoinClause);
    ASSERT_NE(join, nullptr);
    EXPECT_EQ(join->primary_text, "RIGHT JOIN");
}

TEST_F(JoinComprehensiveTest, FullOuterJoin) {
    auto* ast = parse("SELECT * FROM users FULL OUTER JOIN orders ON users.id = orders.user_id");
    ASSERT_NE(ast, nullptr);
    
    auto* join = find_node_by_type(ast, NodeType::JoinClause);
    ASSERT_NE(join, nullptr);
    EXPECT_EQ(join->primary_text, "FULL OUTER JOIN");
}

TEST_F(JoinComprehensiveTest, CrossJoin) {
    auto* ast = parse("SELECT * FROM users CROSS JOIN orders");
    ASSERT_NE(ast, nullptr);
    
    auto* join = find_node_by_type(ast, NodeType::JoinClause);
    ASSERT_NE(join, nullptr);
    EXPECT_EQ(join->primary_text, "CROSS JOIN");
    EXPECT_EQ(join->child_count, 1); // only table, no ON condition
}

TEST_F(JoinComprehensiveTest, MultipleJoins) {
    auto* ast = parse(
        "SELECT * FROM users u "
        "INNER JOIN orders o ON u.id = o.user_id "
        "LEFT JOIN products p ON o.product_id = p.id "
        "RIGHT JOIN categories c ON p.category_id = c.id"
    );
    ASSERT_NE(ast, nullptr);
    
    // Should have 3 JoinClause nodes
    int join_count = count_nodes_of_type(ast, NodeType::JoinClause);
    EXPECT_EQ(join_count, 3);
    
    // Should have 4 TableRef nodes
    int table_count = count_nodes_of_type(ast, NodeType::TableRef);
    EXPECT_EQ(table_count, 4);
}

TEST_F(JoinComprehensiveTest, JoinWithComplexCondition) {
    auto* ast = parse(
        "SELECT * FROM users u "
        "JOIN orders o ON u.id = o.user_id AND o.status = 'active' AND o.amount > 100"
    );
    ASSERT_NE(ast, nullptr);
    
    auto* join = find_node_by_type(ast, NodeType::JoinClause);
    ASSERT_NE(join, nullptr);
    
    // The ON condition should be a complex expression
    auto* condition = join->first_child->next_sibling;
    ASSERT_NE(condition, nullptr);
    EXPECT_EQ(condition->node_type, NodeType::BinaryExpr);
}

TEST_F(JoinComprehensiveTest, MixedJoinStyles) {
    // Mix old-style comma joins with new-style JOINs
    auto* ast = parse(
        "SELECT * FROM users u, departments d "
        "JOIN orders o ON u.id = o.user_id "
        "WHERE u.dept_id = d.id"
    );
    ASSERT_NE(ast, nullptr);
    
    auto* from_clause = find_node_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr);
    
    // Should have: users (TableRef), departments (TableRef), JOIN clause
    EXPECT_EQ(from_clause->child_count, 3);
    
    // First child: users
    auto* first = from_clause->first_child;
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->node_type, NodeType::TableRef);
    
    // Second child: departments
    auto* second = first->next_sibling;
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->node_type, NodeType::TableRef);
    
    // Third child: JOIN clause
    auto* third = second->next_sibling;
    ASSERT_NE(third, nullptr);
    EXPECT_EQ(third->node_type, NodeType::JoinClause);
}

TEST_F(JoinComprehensiveTest, JoinWithUsing) {
    // USING clause is now properly parsed
    auto* ast = parse("SELECT * FROM users JOIN orders USING (user_id)");
    ASSERT_NE(ast, nullptr);
    
    auto* join = find_node_by_type(ast, NodeType::JoinClause);
    ASSERT_NE(join, nullptr);
    EXPECT_EQ(join->primary_text, "JOIN");
    // USING clause is now parsed into AST
    EXPECT_EQ(join->child_count, 2); // table + using clause
    
    // Verify USING clause is present
    auto* using_clause = find_node_by_type(join, NodeType::UsingClause);
    ASSERT_NE(using_clause, nullptr);
    EXPECT_EQ(using_clause->child_count, 1); // one column: user_id
}

TEST_F(JoinComprehensiveTest, JoinFollowedByWhere) {
    auto* ast = parse(
        "SELECT * FROM users u "
        "JOIN orders o ON u.id = o.user_id "
        "WHERE u.status = 'active'"
    );
    ASSERT_NE(ast, nullptr);
    
    // Verify both JOIN and WHERE are parsed
    auto* join = find_node_by_type(ast, NodeType::JoinClause);
    ASSERT_NE(join, nullptr);
    
    auto* where = find_node_by_type(ast, NodeType::WhereClause);
    ASSERT_NE(where, nullptr);
}

TEST_F(JoinComprehensiveTest, JoinWithTableAliases) {
    auto* ast = parse(
        "SELECT * FROM users AS u "
        "JOIN orders AS o ON u.id = o.user_id"
    );
    ASSERT_NE(ast, nullptr);
    
    // Verify tables are parsed with aliases
    auto* from_clause = find_node_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr);
    
    auto* first_table = from_clause->first_child;
    ASSERT_NE(first_table, nullptr);
    EXPECT_EQ(first_table->node_type, NodeType::TableRef);
    EXPECT_EQ(first_table->primary_text, "users");
    
    auto* join_clause = first_table->next_sibling;
    ASSERT_NE(join_clause, nullptr);
    EXPECT_EQ(join_clause->node_type, NodeType::JoinClause);
    
    auto* second_table = join_clause->first_child;
    ASSERT_NE(second_table, nullptr);
    EXPECT_EQ(second_table->node_type, NodeType::TableRef);
    EXPECT_EQ(second_table->primary_text, "orders");
}