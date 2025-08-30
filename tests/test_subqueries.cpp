/*
 * DB25 Parser - Subquery Tests
 * Testing subquery parsing in various contexts
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class SubqueryTest : public ::testing::Test {
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
    
    // Helper to find a child node by type
    ASTNode* find_child_by_type(ASTNode* parent, NodeType type) {
        if (!parent) return nullptr;
        
        ASTNode* child = parent->first_child;
        while (child) {
            if (child->node_type == type) {
                return child;
            }
            child = child->next_sibling;
        }
        return nullptr;
    }
    
    // Recursively find node by type
    ASTNode* find_node_by_type(ASTNode* root, NodeType type) {
        if (!root) return nullptr;
        if (root->node_type == type) return root;
        
        ASTNode* child = root->first_child;
        while (child) {
            ASTNode* found = find_node_by_type(child, type);
            if (found) return found;
            child = child->next_sibling;
        }
        return nullptr;
    }
};

// ============================================================================
// SUBQUERY IN WHERE CLAUSE
// ============================================================================

TEST_F(SubqueryTest, SimpleSubqueryInWhere) {
    auto* ast = parse("SELECT * FROM users WHERE id IN (SELECT user_id FROM orders)");
    ASSERT_NE(ast, nullptr);
    
    auto* where_clause = find_child_by_type(ast, NodeType::WhereClause);
    ASSERT_NE(where_clause, nullptr);
    
    auto* in_expr = find_child_by_type(where_clause, NodeType::InExpr);
    ASSERT_NE(in_expr, nullptr);
    
    // Should have a subquery as second child
    auto* subquery = find_node_by_type(in_expr, NodeType::Subquery);
    ASSERT_NE(subquery, nullptr) << "IN expression should contain a subquery";
    
    // The subquery should contain a SELECT statement
    auto* inner_select = find_child_by_type(subquery, NodeType::SelectStmt);
    ASSERT_NE(inner_select, nullptr) << "Subquery should contain a SELECT statement";
}

TEST_F(SubqueryTest, ExistsSubquery) {
    auto* ast = parse("SELECT * FROM users WHERE EXISTS (SELECT 1 FROM orders WHERE orders.user_id = users.id)");
    ASSERT_NE(ast, nullptr);
    
    auto* where_clause = find_child_by_type(ast, NodeType::WhereClause);
    ASSERT_NE(where_clause, nullptr);
    
    // Should have EXISTS operator with subquery
    auto* exists_expr = find_node_by_type(where_clause, NodeType::UnaryExpr);
    ASSERT_NE(exists_expr, nullptr);
    EXPECT_EQ(exists_expr->primary_text, "EXISTS");
    
    auto* subquery = find_child_by_type(exists_expr, NodeType::Subquery);
    ASSERT_NE(subquery, nullptr) << "EXISTS should contain a subquery";
}

TEST_F(SubqueryTest, NotExistsSubquery) {
    auto* ast = parse("SELECT * FROM users WHERE NOT EXISTS (SELECT 1 FROM orders WHERE user_id = users.id)");
    ASSERT_NE(ast, nullptr);
    
    auto* where_clause = find_child_by_type(ast, NodeType::WhereClause);
    ASSERT_NE(where_clause, nullptr);
    
    // Should handle NOT EXISTS
    auto* not_expr = find_node_by_type(where_clause, NodeType::UnaryExpr);
    ASSERT_NE(not_expr, nullptr);
    EXPECT_TRUE(not_expr->semantic_flags & 0x40) << "Should have NOT flag";
}

// ============================================================================
// SCALAR SUBQUERIES
// ============================================================================

TEST_F(SubqueryTest, ScalarSubqueryInSelect) {
    auto* ast = parse("SELECT name, (SELECT COUNT(*) FROM orders WHERE user_id = users.id) AS order_count FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    // Second item should be a subquery with alias
    ASTNode* first_item = select_list->first_child;
    ASSERT_NE(first_item, nullptr);
    ASTNode* second_item = first_item->next_sibling;
    ASSERT_NE(second_item, nullptr);
    
    EXPECT_EQ(second_item->node_type, NodeType::Subquery);
    EXPECT_EQ(second_item->schema_name, "order_count") << "Subquery should have alias";
    EXPECT_TRUE(second_item->semantic_flags & static_cast<uint16_t>(NodeFlags::HasAlias));
}

TEST_F(SubqueryTest, ScalarSubqueryComparison) {
    auto* ast = parse("SELECT * FROM products WHERE price > (SELECT AVG(price) FROM products)");
    ASSERT_NE(ast, nullptr);
    
    auto* where_clause = find_child_by_type(ast, NodeType::WhereClause);
    ASSERT_NE(where_clause, nullptr);
    
    auto* binary_expr = find_child_by_type(where_clause, NodeType::BinaryExpr);
    ASSERT_NE(binary_expr, nullptr);
    EXPECT_EQ(binary_expr->primary_text, ">");
    
    // Right side should be a subquery
    ASTNode* left = binary_expr->first_child;
    ASSERT_NE(left, nullptr);
    ASTNode* right = left->next_sibling;
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(right->node_type, NodeType::Subquery);
}

// ============================================================================
// SUBQUERIES IN FROM CLAUSE
// ============================================================================

TEST_F(SubqueryTest, SubqueryInFrom) {
    auto* ast = parse("SELECT * FROM (SELECT id, name FROM users WHERE active = true) AS active_users");
    ASSERT_NE(ast, nullptr);
    
    auto* from_clause = find_child_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr);
    
    // Should have a subquery with alias
    auto* subquery = find_child_by_type(from_clause, NodeType::Subquery);
    ASSERT_NE(subquery, nullptr);
    EXPECT_EQ(subquery->schema_name, "active_users");
    EXPECT_TRUE(subquery->semantic_flags & static_cast<uint16_t>(NodeFlags::HasAlias));
}

TEST_F(SubqueryTest, SubqueryWithJoin) {
    auto* ast = parse("SELECT * FROM users u JOIN (SELECT user_id, COUNT(*) as cnt FROM orders GROUP BY user_id) o ON u.id = o.user_id");
    ASSERT_NE(ast, nullptr);
    
    auto* from_clause = find_child_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr);
    
    auto* join_clause = find_child_by_type(from_clause, NodeType::JoinClause);
    ASSERT_NE(join_clause, nullptr);
    
    // Join should have subquery as table reference
    auto* subquery = find_child_by_type(join_clause, NodeType::Subquery);
    ASSERT_NE(subquery, nullptr);
    EXPECT_EQ(subquery->schema_name, "o") << "Subquery should have alias 'o'";
}

// ============================================================================
// NESTED SUBQUERIES
// ============================================================================

TEST_F(SubqueryTest, NestedSubqueries) {
    auto* ast = parse(
        "SELECT * FROM users "
        "WHERE id IN (SELECT user_id FROM orders "
        "             WHERE product_id IN (SELECT id FROM products WHERE category = 'Electronics'))"
    );
    ASSERT_NE(ast, nullptr);
    
    // Find the outer IN expression
    auto* where_clause = find_child_by_type(ast, NodeType::WhereClause);
    ASSERT_NE(where_clause, nullptr);
    
    auto* outer_in = find_child_by_type(where_clause, NodeType::InExpr);
    ASSERT_NE(outer_in, nullptr);
    
    // Find the outer subquery
    auto* outer_subquery = find_node_by_type(outer_in, NodeType::Subquery);
    ASSERT_NE(outer_subquery, nullptr);
    
    // Find the nested subquery
    auto* inner_subquery = find_node_by_type(outer_subquery->first_child, NodeType::Subquery);
    ASSERT_NE(inner_subquery, nullptr) << "Should have nested subquery";
}

// ============================================================================
// ANY/ALL/SOME OPERATORS
// ============================================================================

TEST_F(SubqueryTest, AnyOperator) {
    auto* ast = parse("SELECT * FROM products WHERE price > ANY (SELECT price FROM products WHERE category = 'Books')");
    ASSERT_NE(ast, nullptr);
    
    auto* where_clause = find_child_by_type(ast, NodeType::WhereClause);
    ASSERT_NE(where_clause, nullptr);
    
    // Should parse > ANY as a special binary expression
    auto* binary_expr = find_child_by_type(where_clause, NodeType::BinaryExpr);
    ASSERT_NE(binary_expr, nullptr);
    EXPECT_EQ(binary_expr->primary_text, "> ANY");
    
    auto* subquery = find_node_by_type(binary_expr, NodeType::Subquery);
    ASSERT_NE(subquery, nullptr);
}

TEST_F(SubqueryTest, AllOperator) {
    auto* ast = parse("SELECT * FROM employees WHERE salary >= ALL (SELECT salary FROM employees WHERE department = 'Sales')");
    ASSERT_NE(ast, nullptr);
    
    auto* where_clause = find_child_by_type(ast, NodeType::WhereClause);
    ASSERT_NE(where_clause, nullptr);
    
    auto* binary_expr = find_child_by_type(where_clause, NodeType::BinaryExpr);
    ASSERT_NE(binary_expr, nullptr);
    EXPECT_EQ(binary_expr->primary_text, ">= ALL");
}

// ============================================================================
// CORRELATED SUBQUERIES
// ============================================================================

TEST_F(SubqueryTest, CorrelatedSubquery) {
    auto* ast = parse(
        "SELECT e1.name, e1.salary "
        "FROM employees e1 "
        "WHERE salary > (SELECT AVG(salary) FROM employees e2 WHERE e2.department = e1.department)"
    );
    ASSERT_NE(ast, nullptr);
    
    // Should parse the correlated reference e1.department
    auto* where_clause = find_child_by_type(ast, NodeType::WhereClause);
    ASSERT_NE(where_clause, nullptr);
    
    auto* subquery = find_node_by_type(where_clause, NodeType::Subquery);
    ASSERT_NE(subquery, nullptr);
    
    // The subquery should have a WHERE clause with correlation
    auto* inner_where = find_node_by_type(subquery, NodeType::WhereClause);
    ASSERT_NE(inner_where, nullptr);
    
    // Find the binary expression in WHERE
    auto* binary = find_child_by_type(inner_where, NodeType::BinaryExpr);
    ASSERT_NE(binary, nullptr);
    
    // The right side should reference the outer table (e1.department)
    auto* right_col = binary->first_child->next_sibling;
    ASSERT_NE(right_col, nullptr);
    EXPECT_EQ(right_col->node_type, NodeType::ColumnRef);
    EXPECT_EQ(right_col->primary_text, "e1.department");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}