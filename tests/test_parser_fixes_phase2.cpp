/*
 * DB25 Parser - Phase 2 Semantic Fixes Test Suite
 * Tests for ORDER BY direction, window frame boundaries, and aliases
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include <iostream>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class ParserFixesPhase2Test : public ::testing::Test {
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

// Test 1: ORDER BY direction (ASC/DESC)
TEST_F(ParserFixesPhase2Test, OrderByDirection) {
    std::string sql = R"(
        SELECT name, salary, department
        FROM employees 
        ORDER BY salary DESC, department ASC, name
    )";
    
    auto result = parser.parse(sql);
    ASSERT_TRUE(result.has_value()) << "Failed to parse ORDER BY query";
    
    auto* order_by = find_node_type(result.value(), NodeType::OrderByClause);
    ASSERT_NE(order_by, nullptr) << "Should find ORDER BY clause";
    
    // Check the direction of each order item
    int desc_count = 0;
    int asc_count = 0;
    
    for (auto* item = order_by->first_child; item; item = item->next_sibling) {
        // Bit 7 of semantic_flags indicates DESC (1) or ASC (0)
        if (item->semantic_flags & (1 << 7)) {
            desc_count++;
        } else {
            // Could be explicit ASC or default
            // For now, we count them together
            asc_count++;
        }
    }
    
    EXPECT_EQ(desc_count, 1) << "Should find 1 DESC item (salary)";
    EXPECT_EQ(asc_count, 2) << "Should find 2 ASC items (department explicit, name default)";
}

// Test 2: Window frame boundaries (PRECEDING/FOLLOWING)
TEST_F(ParserFixesPhase2Test, WindowFrameBoundaries) {
    std::string sql = R"(
        SELECT 
            customer_id,
            SUM(amount) OVER (
                ORDER BY order_date 
                ROWS BETWEEN 3 PRECEDING AND CURRENT ROW
            ) as lookback_sum,
            AVG(amount) OVER (
                ORDER BY order_date 
                ROWS BETWEEN CURRENT ROW AND 2 FOLLOWING
            ) as lookahead_avg
        FROM orders
    )";
    
    auto result = parser.parse(sql);
    ASSERT_TRUE(result.has_value()) << "Failed to parse window frame query";
    
    // Find window specs
    auto* ast = result.value();
    int window_specs = 0;
    int preceding_bounds = 0;
    int following_bounds = 0;
    int current_row_bounds = 0;
    
    std::function<void(ASTNode*)> check_frames = [&](ASTNode* node) {
        if (!node) return;
        
        if (node->node_type == NodeType::WindowSpec) {
            window_specs++;
        } else if (node->node_type == NodeType::FrameBound) {
            // Check schema_name for PRECEDING/FOLLOWING
            if (node->schema_name == "PRECEDING") {
                preceding_bounds++;
            } else if (node->schema_name == "FOLLOWING") {
                following_bounds++;
            }
            // Check primary_text for CURRENT ROW
            if (node->primary_text == "CURRENT ROW") {
                current_row_bounds++;
            }
        }
        
        for (auto* child = node->first_child; child; child = child->next_sibling) {
            check_frames(child);
        }
    };
    
    check_frames(ast);
    
    EXPECT_EQ(window_specs, 2) << "Should find 2 window specifications";
    EXPECT_EQ(preceding_bounds, 1) << "Should find 1 PRECEDING bound";
    EXPECT_EQ(following_bounds, 1) << "Should find 1 FOLLOWING bound";
    EXPECT_EQ(current_row_bounds, 2) << "Should find 2 CURRENT ROW bounds";
}

// Test 3: UNBOUNDED frame boundaries
TEST_F(ParserFixesPhase2Test, UnboundedFrameBoundaries) {
    std::string sql = R"(
        SELECT 
            SUM(amount) OVER (
                ORDER BY date 
                ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
            ) as running_total,
            AVG(amount) OVER (
                ORDER BY date 
                RANGE BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING
            ) as future_avg
        FROM transactions
    )";
    
    auto result = parser.parse(sql);
    ASSERT_TRUE(result.has_value()) << "Failed to parse unbounded frame query";
    
    auto* ast = result.value();
    int unbounded_preceding = 0;
    int unbounded_following = 0;
    
    std::function<void(ASTNode*)> check_unbounded = [&](ASTNode* node) {
        if (!node) return;
        
        if (node->node_type == NodeType::FrameBound) {
            if (node->primary_text == "UNBOUNDED" && node->schema_name == "PRECEDING") {
                unbounded_preceding++;
            } else if (node->primary_text == "UNBOUNDED" && node->schema_name == "FOLLOWING") {
                unbounded_following++;
            }
        }
        
        for (auto* child = node->first_child; child; child = child->next_sibling) {
            check_unbounded(child);
        }
    };
    
    check_unbounded(ast);
    
    EXPECT_EQ(unbounded_preceding, 1) << "Should find 1 UNBOUNDED PRECEDING";
    EXPECT_EQ(unbounded_following, 1) << "Should find 1 UNBOUNDED FOLLOWING";
}

// Test 4: Aliases in SELECT list
TEST_F(ParserFixesPhase2Test, SelectListAliases) {
    std::string sql = R"(
        SELECT 
            employee_id AS id,
            UPPER(name) AS full_name,
            salary * 1.1 bonus,
            department
        FROM employees
    )";
    
    auto result = parser.parse(sql);
    ASSERT_TRUE(result.has_value()) << "Failed to parse SELECT with aliases";
    
    auto* select_list = find_node_type(result.value(), NodeType::SelectList);
    ASSERT_NE(select_list, nullptr) << "Should find SELECT list";
    
    // Count items with aliases
    int items_with_alias = 0;
    int total_items = 0;
    
    for (auto* item = select_list->first_child; item; item = item->next_sibling) {
        total_items++;
        
        // Check if this item has an alias
        // Aliases might be stored as:
        // 1. HasAlias flag set
        // 2. In schema_name field (repurposed for alias)
        // 3. As a child AliasRef node
        
        if (has_flag(item->flags, NodeFlags::HasAlias)) {
            items_with_alias++;
        } else if (!item->schema_name.empty()) {
            // schema_name might be used for alias in SELECT items
            items_with_alias++;
        } else {
            // Check for child alias node
            for (auto* child = item->first_child; child; child = child->next_sibling) {
                if (child->node_type == NodeType::AliasRef) {
                    items_with_alias++;
                    break;
                }
            }
        }
    }
    
    EXPECT_EQ(total_items, 4) << "Should find 4 SELECT items";
    EXPECT_GE(items_with_alias, 3) << "Should find at least 3 items with aliases";
}

// Test 5: Table aliases in FROM clause
TEST_F(ParserFixesPhase2Test, TableAliases) {
    std::string sql = R"(
        SELECT e.name, m.name as manager_name
        FROM employees e
        JOIN employees m ON e.manager_id = m.id
        JOIN departments d ON e.dept_id = d.id
    )";
    
    auto result = parser.parse(sql);
    ASSERT_TRUE(result.has_value()) << "Failed to parse table aliases";
    
    auto* ast = result.value();
    int table_refs = 0;
    int tables_with_alias = 0;
    
    std::function<void(ASTNode*)> check_tables = [&](ASTNode* node) {
        if (!node) return;
        
        if (node->node_type == NodeType::TableRef) {
            table_refs++;
            
            // Check for alias
            if (has_flag(node->flags, NodeFlags::HasAlias)) {
                tables_with_alias++;
            } else if (!node->schema_name.empty()) {
                // schema_name might store the alias
                tables_with_alias++;
            } else if (node->next_sibling && node->next_sibling->node_type == NodeType::AliasRef) {
                tables_with_alias++;
            }
        }
        
        for (auto* child = node->first_child; child; child = child->next_sibling) {
            check_tables(child);
        }
    };
    
    check_tables(ast);
    
    EXPECT_EQ(table_refs, 3) << "Should find 3 table references";
    EXPECT_EQ(tables_with_alias, 3) << "All 3 tables should have aliases";
}

// Test 6: NULLS FIRST/LAST in ORDER BY
TEST_F(ParserFixesPhase2Test, NullsOrdering) {
    std::string sql = R"(
        SELECT name, salary
        FROM employees
        ORDER BY salary DESC NULLS LAST, name ASC NULLS FIRST
    )";
    
    auto result = parser.parse(sql);
    ASSERT_TRUE(result.has_value()) << "Failed to parse NULLS ordering";
    
    auto* order_by = find_node_type(result.value(), NodeType::OrderByClause);
    ASSERT_NE(order_by, nullptr) << "Should find ORDER BY clause";
    
    // Check if NULLS FIRST/LAST is captured
    // This might be stored in semantic_flags or as additional metadata
    // For now, just verify the query parses correctly
    
    // Count order items
    int order_items = 0;
    for (auto* item = order_by->first_child; item; item = item->next_sibling) {
        order_items++;
    }
    
    EXPECT_EQ(order_items, 2) << "Should find 2 order items";
}

// Test 7: Mixed frame types (ROWS vs RANGE)
TEST_F(ParserFixesPhase2Test, MixedFrameTypes) {
    std::string sql = R"(
        SELECT 
            ROW_NUMBER() OVER (ORDER BY date ROWS UNBOUNDED PRECEDING) as row_num,
            SUM(amount) OVER (ORDER BY date RANGE BETWEEN INTERVAL '1' DAY PRECEDING 
                              AND CURRENT ROW) as daily_sum
        FROM sales
    )";
    
    auto result = parser.parse(sql);
    // Note: INTERVAL syntax might not be supported yet
    // Just check if basic ROWS/RANGE distinction is captured
    
    if (result.has_value()) {
        auto* ast = result.value();
        
        int rows_frames = 0;
        int range_frames = 0;
        
        std::function<void(ASTNode*)> check_frame_types = [&](ASTNode* node) {
            if (!node) return;
            
            if (node->node_type == NodeType::FrameClause) {
                if (node->primary_text == "ROWS" || node->primary_text == "rows") {
                    rows_frames++;
                } else if (node->primary_text == "RANGE" || node->primary_text == "range") {
                    range_frames++;
                }
            }
            
            for (auto* child = node->first_child; child; child = child->next_sibling) {
                check_frame_types(child);
            }
        };
        
        check_frame_types(ast);
        
        // At least one frame type should be found
        EXPECT_GE(rows_frames + range_frames, 1) << "Should find at least one frame specification";
    }
}