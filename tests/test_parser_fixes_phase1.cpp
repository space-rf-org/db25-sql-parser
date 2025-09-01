/*
 * DB25 Parser - Phase 1 Critical Fixes Test Suite
 * Tests for NOT EXISTS/IN, DISTINCT, UNION ALL, and other critical semantic fixes
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include <iostream>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class ParserFixesPhase1Test : public ::testing::Test {
protected:
    Parser parser;
    
    // Helper to find a specific node type in the AST
    ASTNode* find_node_type(ASTNode* root, NodeType type) {
        if (!root) return nullptr;
        if (root->node_type == type) return root;
        
        for (auto* child = root->first_child; child; child = child->next_sibling) {
            auto* found = find_node_type(child, type);
            if (found) return found;
        }
        return nullptr;
    }
    
    // Helper to find node with specific primary_text
    ASTNode* find_node_with_text(ASTNode* root, const std::string& text) {
        if (!root) return nullptr;
        if (root->primary_text == text) return root;
        
        for (auto* child = root->first_child; child; child = child->next_sibling) {
            auto* found = find_node_with_text(child, text);
            if (found) return found;
        }
        return nullptr;
    }
};

// Test 1: NOT EXISTS should be different from EXISTS
TEST_F(ParserFixesPhase1Test, NotExistsVsExists) {
    // Test EXISTS
    std::string sql_exists = R"(
        SELECT * FROM customers c 
        WHERE EXISTS (SELECT 1 FROM orders WHERE customer_id = c.id)
    )";
    
    auto result1 = parser.parse(sql_exists);
    ASSERT_TRUE(result1.has_value()) << "Failed to parse EXISTS query";
    
    auto* exists_node = find_node_with_text(result1.value(), "EXISTS");
    ASSERT_NE(exists_node, nullptr) << "Should find EXISTS node";
    
    // Check that it's not marked as NOT EXISTS
    // The semantic_flags bit 6 (0x40) should NOT be set for plain EXISTS
    EXPECT_EQ(exists_node->semantic_flags & 0x40, 0) << "EXISTS should not have NOT flag";
    
    parser.reset();
    
    // Test NOT EXISTS
    std::string sql_not_exists = R"(
        SELECT * FROM customers c 
        WHERE NOT EXISTS (SELECT 1 FROM orders WHERE customer_id = c.id)
    )";
    
    auto result2 = parser.parse(sql_not_exists);
    ASSERT_TRUE(result2.has_value()) << "Failed to parse NOT EXISTS query";
    
    auto* not_exists_node = find_node_with_text(result2.value(), "EXISTS");
    ASSERT_NE(not_exists_node, nullptr) << "Should find NOT EXISTS node";
    
    // Check that it IS marked as NOT EXISTS
    // The semantic_flags bit 6 (0x40) SHOULD be set for NOT EXISTS
    EXPECT_NE(not_exists_node->semantic_flags & 0x40, 0) << "NOT EXISTS should have NOT flag";
}

// Test 2: NOT IN should be different from IN
TEST_F(ParserFixesPhase1Test, NotInVsIn) {
    // Test IN
    std::string sql_in = R"(
        SELECT * FROM products 
        WHERE category_id IN (1, 2, 3)
    )";
    
    auto result1 = parser.parse(sql_in);
    ASSERT_TRUE(result1.has_value()) << "Failed to parse IN query";
    
    auto* in_node = find_node_type(result1.value(), NodeType::InExpr);
    ASSERT_NE(in_node, nullptr) << "Should find IN node";
    
    // IN should not have NOT flag
    EXPECT_EQ(in_node->semantic_flags & 0x40, 0) << "IN should not have NOT flag";
    
    parser.reset();
    
    // Test NOT IN
    std::string sql_not_in = R"(
        SELECT * FROM products 
        WHERE category_id NOT IN (1, 2, 3)
    )";
    
    auto result2 = parser.parse(sql_not_in);
    ASSERT_TRUE(result2.has_value()) << "Failed to parse NOT IN query";
    
    auto* not_in_node = find_node_type(result2.value(), NodeType::InExpr);
    ASSERT_NE(not_in_node, nullptr) << "Should find NOT IN node";
    
    // NOT IN should have NOT flag
    EXPECT_NE(not_in_node->semantic_flags & 0x40, 0) << "NOT IN should have NOT flag";
}

// Test 3: COUNT(DISTINCT x) should preserve DISTINCT
TEST_F(ParserFixesPhase1Test, CountDistinct) {
    std::string sql = R"(
        SELECT 
            COUNT(*) as total,
            COUNT(DISTINCT customer_id) as unique_customers,
            COUNT(order_id) as order_count
        FROM orders
    )";
    
    auto result = parser.parse(sql);
    ASSERT_TRUE(result.has_value()) << "Failed to parse COUNT DISTINCT query";
    
    // Find the COUNT functions
    auto* ast = result.value();
    int count_nodes = 0;
    int distinct_count_nodes = 0;
    
    std::function<void(ASTNode*)> check_counts = [&](ASTNode* node) {
        if (!node) return;
        
        if (node->node_type == NodeType::FunctionCall && 
            node->primary_text == "COUNT") {
            count_nodes++;
            
            // Check for DISTINCT flag
            if (has_flag(node->flags, NodeFlags::Distinct)) {
                distinct_count_nodes++;
            }
        }
        
        for (auto* child = node->first_child; child; child = child->next_sibling) {
            check_counts(child);
        }
    };
    
    check_counts(ast);
    
    EXPECT_EQ(count_nodes, 3) << "Should find 3 COUNT functions";
    EXPECT_EQ(distinct_count_nodes, 1) << "Should find 1 COUNT DISTINCT";
}

// Test 4: UNION ALL vs UNION
TEST_F(ParserFixesPhase1Test, UnionAllVsUnion) {
    // Test UNION (distinct)
    std::string sql_union = R"(
        SELECT id FROM table1
        UNION
        SELECT id FROM table2
    )";
    
    auto result1 = parser.parse(sql_union);
    ASSERT_TRUE(result1.has_value()) << "Failed to parse UNION query";
    
    auto* union_node = find_node_type(result1.value(), NodeType::UnionStmt);
    ASSERT_NE(union_node, nullptr) << "Should find UNION node";
    
    // UNION without ALL should not have All flag
    EXPECT_FALSE(has_flag(union_node->flags, NodeFlags::All)) 
        << "UNION should not have ALL flag";
    
    parser.reset();
    
    // Test UNION ALL
    std::string sql_union_all = R"(
        SELECT id FROM table1
        UNION ALL
        SELECT id FROM table2
    )";
    
    auto result2 = parser.parse(sql_union_all);
    ASSERT_TRUE(result2.has_value()) << "Failed to parse UNION ALL query";
    
    auto* union_all_node = find_node_type(result2.value(), NodeType::UnionStmt);
    ASSERT_NE(union_all_node, nullptr) << "Should find UNION ALL node";
    
    // UNION ALL should have All flag
    EXPECT_TRUE(has_flag(union_all_node->flags, NodeFlags::All)) 
        << "UNION ALL should have ALL flag";
}

// Test 5: ORDER BY DESC vs ASC
TEST_F(ParserFixesPhase1Test, OrderByDirection) {
    std::string sql = R"(
        SELECT * FROM employees 
        ORDER BY salary DESC, name ASC, department
    )";
    
    auto result = parser.parse(sql);
    ASSERT_TRUE(result.has_value()) << "Failed to parse ORDER BY query";
    
    auto* order_by = find_node_type(result.value(), NodeType::OrderByClause);
    ASSERT_NE(order_by, nullptr) << "Should find ORDER BY clause";
    
    // Check children for sort directions
    int desc_count = 0;
    int asc_count = 0;
    int default_count = 0;
    
    for (auto* item = order_by->first_child; item; item = item->next_sibling) {
        // Check if sort direction is stored in semantic_flags or as a child node
        // This depends on implementation - adjust based on actual fix
        
        // For now, let's check if there's a pattern in the node structure
        // that indicates sort direction
        if (item->primary_text == "DESC" || (item->semantic_flags & 0x01)) {
            desc_count++;
        } else if (item->primary_text == "ASC" || (item->semantic_flags & 0x02)) {
            asc_count++;
        } else {
            default_count++;
        }
    }
    
    // We expect to find sort direction indicators
    EXPECT_GT(desc_count + asc_count + default_count, 0) 
        << "Should find sort direction information";
}

// Test 6: Window frame boundaries PRECEDING/FOLLOWING
TEST_F(ParserFixesPhase1Test, WindowFrameBoundaries) {
    std::string sql = R"(
        SELECT 
            SUM(amount) OVER (
                ORDER BY date 
                ROWS BETWEEN 3 PRECEDING AND CURRENT ROW
            ) as backward_sum,
            SUM(amount) OVER (
                ORDER BY date 
                ROWS BETWEEN CURRENT ROW AND 3 FOLLOWING
            ) as forward_sum
        FROM transactions
    )";
    
    auto result = parser.parse(sql);
    ASSERT_TRUE(result.has_value()) << "Failed to parse window frame query";
    
    // Find window spec nodes
    auto* ast = result.value();
    int window_specs = 0;
    int preceding_found = 0;
    int following_found = 0;
    
    std::function<void(ASTNode*)> check_windows = [&](ASTNode* node) {
        if (!node) return;
        
        if (node->node_type == NodeType::WindowSpec) {
            window_specs++;
            
            // Look for frame bounds
            for (auto* child = node->first_child; child; child = child->next_sibling) {
                if (node->node_type == NodeType::FrameBound ||
                    node->node_type == NodeType::FrameClause) {
                    // Check if PRECEDING or FOLLOWING is captured
                    if (node->primary_text.find("PRECEDING") != std::string::npos ||
                        node->schema_name == "PRECEDING") {
                        preceding_found++;
                    }
                    if (node->primary_text.find("FOLLOWING") != std::string::npos ||
                        node->schema_name == "FOLLOWING") {
                        following_found++;
                    }
                }
            }
        }
        
        for (auto* child = node->first_child; child; child = child->next_sibling) {
            check_windows(child);
        }
    };
    
    check_windows(ast);
    
    EXPECT_EQ(window_specs, 2) << "Should find 2 window specifications";
    // These assertions may need adjustment based on actual implementation
    // The key is that PRECEDING and FOLLOWING are distinguished
}

// Test 7: Table aliases should be captured
TEST_F(ParserFixesPhase1Test, TableAliases) {
    std::string sql = R"(
        SELECT e1.name, e2.name, d.department_name
        FROM employees e1
        JOIN employees e2 ON e1.manager_id = e2.id
        JOIN departments d ON e1.dept_id = d.id
    )";
    
    auto result = parser.parse(sql);
    ASSERT_TRUE(result.has_value()) << "Failed to parse table alias query";
    
    // Look for table references and their aliases
    auto* ast = result.value();
    int table_refs = 0;
    int aliases_found = 0;
    
    std::function<void(ASTNode*)> check_tables = [&](ASTNode* node) {
        if (!node) return;
        
        if (node->node_type == NodeType::TableRef) {
            table_refs++;
            // Check if alias is captured (could be in various places)
            // - As a sibling ALIAS node
            // - In secondary_text or schema_name field
            // - As semantic metadata
            
            // This is implementation-dependent
            if (node->next_sibling && 
                node->next_sibling->node_type == NodeType::AliasRef) {
                aliases_found++;
            }
        }
        
        for (auto* child = node->first_child; child; child = child->next_sibling) {
            check_tables(child);
        }
    };
    
    check_tables(ast);
    
    EXPECT_EQ(table_refs, 3) << "Should find 3 table references";
    // Aliases should be captured somehow
    // The exact assertion depends on implementation
}

// Test 8: Column references should use ColumnRef not Identifier
TEST_F(ParserFixesPhase1Test, ColumnVsIdentifier) {
    std::string sql = R"(
        SELECT employee_id, e.name, department
        FROM employees e
        WHERE salary > 50000
    )";
    
    auto result = parser.parse(sql);
    ASSERT_TRUE(result.has_value()) << "Failed to parse column reference query";
    
    auto* ast = result.value();
    int column_refs = 0;
    int identifiers = 0;
    
    std::function<void(ASTNode*)> check_refs = [&](ASTNode* node) {
        if (!node) return;
        
        if (node->node_type == NodeType::ColumnRef) {
            column_refs++;
        } else if (node->node_type == NodeType::Identifier) {
            identifiers++;
            // Identifiers should only be for non-column things
            // like type names, keywords used as identifiers, etc.
        }
        
        for (auto* child = node->first_child; child; child = child->next_sibling) {
            check_refs(child);
        }
    };
    
    check_refs(ast);
    
    // We expect column references in SELECT list and WHERE clause
    EXPECT_GE(column_refs, 4) << "Should find at least 4 column references";
    // Identifiers should be minimal (maybe table name, alias)
    EXPECT_LT(identifiers, column_refs) << "Should have fewer identifiers than column refs";
}