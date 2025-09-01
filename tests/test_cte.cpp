/*
 * DB25 Parser - CTE (Common Table Expression) Tests
 * Testing WITH clause and CTE parsing
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class CTETest : public ::testing::Test {
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
// SIMPLE CTE
// ============================================================================

TEST_F(CTETest, SimpleCTE) {
    auto* ast = parse(
        "WITH users_cte AS (SELECT * FROM users) "
        "SELECT * FROM users_cte"
    );
    ASSERT_NE(ast, nullptr);
    
    // Should have a WITH clause
    auto* cte_clause = find_child_by_type(ast, NodeType::CTEClause);
    ASSERT_NE(cte_clause, nullptr) << "Should have a WITH clause";
    
    // Should have a CTE definition
    auto* cte_def = find_child_by_type(cte_clause, NodeType::CTEDefinition);
    ASSERT_NE(cte_def, nullptr) << "Should have a CTE definition";
    EXPECT_EQ(cte_def->primary_text, "users_cte") << "CTE should have name 'users_cte'";
    
    // CTE definition should contain a SELECT statement
    auto* cte_select = find_child_by_type(cte_def, NodeType::SelectStmt);
    ASSERT_NE(cte_select, nullptr) << "CTE should contain a SELECT statement";
    
    // Main SELECT should reference the CTE
    auto* main_select = find_node_by_type(ast, NodeType::SelectStmt);
    ASSERT_NE(main_select, nullptr);
    
    auto* from_clause = find_child_by_type(main_select, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr);
    
    auto* table_ref = find_child_by_type(from_clause, NodeType::TableRef);
    ASSERT_NE(table_ref, nullptr);
    EXPECT_EQ(table_ref->primary_text, "users_cte");
}

TEST_F(CTETest, CTEWithColumns) {
    auto* ast = parse(
        "WITH user_orders (user_id, order_count) AS ("
        "  SELECT user_id, COUNT(*) FROM orders GROUP BY user_id"
        ") "
        "SELECT * FROM user_orders WHERE order_count > 5"
    );
    ASSERT_NE(ast, nullptr);
    
    auto* cte_clause = find_child_by_type(ast, NodeType::CTEClause);
    ASSERT_NE(cte_clause, nullptr);
    
    auto* cte_def = find_child_by_type(cte_clause, NodeType::CTEDefinition);
    ASSERT_NE(cte_def, nullptr);
    EXPECT_EQ(cte_def->primary_text, "user_orders");
    
    // Should have column list
    auto* col_list = find_child_by_type(cte_def, NodeType::ColumnList);
    ASSERT_NE(col_list, nullptr) << "CTE should have column list";
    
    // Verify columns
    auto* first_col = col_list->first_child;
    ASSERT_NE(first_col, nullptr);
    EXPECT_EQ(first_col->primary_text, "user_id");
    
    auto* second_col = first_col->next_sibling;
    ASSERT_NE(second_col, nullptr);
    EXPECT_EQ(second_col->primary_text, "order_count");
}

// ============================================================================
// MULTIPLE CTEs
// ============================================================================

TEST_F(CTETest, MultipleCTEs) {
    auto* ast = parse(
        "WITH "
        "active_users AS (SELECT * FROM users WHERE active = true), "
        "recent_orders AS (SELECT * FROM orders WHERE order_date > '2024-01-01') "
        "SELECT u.*, o.* FROM active_users u JOIN recent_orders o ON u.id = o.user_id"
    );
    ASSERT_NE(ast, nullptr);
    
    auto* cte_clause = find_child_by_type(ast, NodeType::CTEClause);
    ASSERT_NE(cte_clause, nullptr);
    
    // Should have two CTE definitions
    auto* first_cte = find_child_by_type(cte_clause, NodeType::CTEDefinition);
    ASSERT_NE(first_cte, nullptr);
    EXPECT_EQ(first_cte->primary_text, "active_users");
    
    auto* second_cte = first_cte->next_sibling;
    ASSERT_NE(second_cte, nullptr);
    EXPECT_EQ(second_cte->node_type, NodeType::CTEDefinition);
    EXPECT_EQ(second_cte->primary_text, "recent_orders");
}

// ============================================================================
// RECURSIVE CTE
// ============================================================================

TEST_F(CTETest, RecursiveCTE) {
    auto* ast = parse(
        "WITH RECURSIVE employee_hierarchy AS ("
        "  SELECT id, name, manager_id, 1 as level FROM employees WHERE manager_id IS NULL "
        "  UNION ALL "
        "  SELECT e.id, e.name, e.manager_id, h.level + 1 "
        "  FROM employees e JOIN employee_hierarchy h ON e.manager_id = h.id"
        ") "
        "SELECT * FROM employee_hierarchy"
    );
    ASSERT_NE(ast, nullptr);
    
    auto* cte_clause = find_child_by_type(ast, NodeType::CTEClause);
    ASSERT_NE(cte_clause, nullptr);
    
    // Should be marked as RECURSIVE
    EXPECT_TRUE(cte_clause->semantic_flags & 0x100) << "CTE clause should have RECURSIVE flag";
    
    auto* cte_def = find_child_by_type(cte_clause, NodeType::CTEDefinition);
    ASSERT_NE(cte_def, nullptr);
    EXPECT_EQ(cte_def->primary_text, "employee_hierarchy");
    
    // Should contain a UNION statement
    auto* union_stmt = find_node_by_type(cte_def, NodeType::UnionStmt);
    ASSERT_NE(union_stmt, nullptr) << "Recursive CTE should contain UNION";
}

// ============================================================================
// NESTED CTEs
// ============================================================================

TEST_F(CTETest, NestedCTEReference) {
    auto* ast = parse(
        "WITH "
        "base_data AS (SELECT * FROM sales WHERE year = 2024), "
        "aggregated AS (SELECT product_id, SUM(amount) as total FROM base_data GROUP BY product_id) "
        "SELECT * FROM aggregated WHERE total > 1000"
    );
    ASSERT_NE(ast, nullptr);
    
    auto* cte_clause = find_child_by_type(ast, NodeType::CTEClause);
    ASSERT_NE(cte_clause, nullptr);
    
    // First CTE
    auto* first_cte = find_child_by_type(cte_clause, NodeType::CTEDefinition);
    ASSERT_NE(first_cte, nullptr);
    EXPECT_EQ(first_cte->primary_text, "base_data");
    
    // Second CTE references first
    auto* second_cte = first_cte->next_sibling;
    ASSERT_NE(second_cte, nullptr);
    EXPECT_EQ(second_cte->primary_text, "aggregated");
    
    // Second CTE's FROM should reference base_data
    auto* second_select = find_child_by_type(second_cte, NodeType::SelectStmt);
    ASSERT_NE(second_select, nullptr);
    
    auto* from_clause = find_child_by_type(second_select, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr);
    
    auto* table_ref = find_child_by_type(from_clause, NodeType::TableRef);
    ASSERT_NE(table_ref, nullptr);
    EXPECT_EQ(table_ref->primary_text, "base_data");
}

// ============================================================================
// CTE WITH SUBQUERIES
// ============================================================================

TEST_F(CTETest, CTEWithSubquery) {
    auto* ast = parse(
        "WITH high_value_customers AS ("
        "  SELECT * FROM customers "
        "  WHERE id IN (SELECT customer_id FROM orders WHERE amount > 1000)"
        ") "
        "SELECT * FROM high_value_customers"
    );
    ASSERT_NE(ast, nullptr);
    
    auto* cte_clause = find_child_by_type(ast, NodeType::CTEClause);
    ASSERT_NE(cte_clause, nullptr);
    
    auto* cte_def = find_child_by_type(cte_clause, NodeType::CTEDefinition);
    ASSERT_NE(cte_def, nullptr);
    
    // CTE should contain a subquery in WHERE
    auto* subquery = find_node_by_type(cte_def, NodeType::Subquery);
    ASSERT_NE(subquery, nullptr) << "CTE should contain a subquery";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}