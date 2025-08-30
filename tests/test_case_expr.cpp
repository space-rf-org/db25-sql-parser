/*
 * Test CASE expressions
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include "db25/ast/node_types.hpp"
#include <string>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class CaseExprTest : public ::testing::Test {
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

// Simple CASE expression
TEST_F(CaseExprTest, SimpleCaseExpression) {
    auto* ast = parse("SELECT CASE WHEN age < 18 THEN 'minor' ELSE 'adult' END FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* case_expr = find_node_by_type(ast, NodeType::CaseExpr);
    ASSERT_NE(case_expr, nullptr);
    
    // Should have WHEN and ELSE branches
    EXPECT_GE(case_expr->child_count, 2);
}

// CASE with multiple WHEN
TEST_F(CaseExprTest, MultipleWhenClauses) {
    auto* ast = parse(
        "SELECT CASE "
        "WHEN age < 13 THEN 'child' "
        "WHEN age < 20 THEN 'teen' "
        "WHEN age < 65 THEN 'adult' "
        "ELSE 'senior' END FROM users"
    );
    ASSERT_NE(ast, nullptr);
    
    auto* case_expr = find_node_by_type(ast, NodeType::CaseExpr);
    ASSERT_NE(case_expr, nullptr);
    
    // Should have 4 WHEN branches + 1 ELSE
    EXPECT_EQ(case_expr->child_count, 4);
}

// CASE without ELSE
TEST_F(CaseExprTest, CaseWithoutElse) {
    auto* ast = parse("SELECT CASE WHEN status = 'active' THEN 1 END FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* case_expr = find_node_by_type(ast, NodeType::CaseExpr);
    ASSERT_NE(case_expr, nullptr);
    
    // Should have just 1 WHEN branch
    EXPECT_EQ(case_expr->child_count, 1);
}

// Searched CASE (with expression)
TEST_F(CaseExprTest, SearchedCase) {
    auto* ast = parse(
        "SELECT CASE status "
        "WHEN 'active' THEN 1 "
        "WHEN 'inactive' THEN 0 "
        "ELSE -1 END FROM users"
    );
    ASSERT_NE(ast, nullptr);
    
    auto* case_expr = find_node_by_type(ast, NodeType::CaseExpr);
    ASSERT_NE(case_expr, nullptr);
    
    // First child should be the search expression (status)
    auto* search_expr = case_expr->first_child;
    ASSERT_NE(search_expr, nullptr);
    EXPECT_EQ(search_expr->node_type, NodeType::Identifier);
    EXPECT_EQ(search_expr->primary_text, "status");
}

// Nested CASE expressions
TEST_F(CaseExprTest, NestedCase) {
    auto* ast = parse(
        "SELECT CASE "
        "WHEN age < 18 THEN "
        "  CASE WHEN age < 13 THEN 'child' ELSE 'teen' END "
        "ELSE 'adult' END FROM users"
    );
    ASSERT_NE(ast, nullptr);
    
    // Should have 2 CASE nodes
    int case_count = 0;
    std::function<void(ASTNode*)> count_cases = [&](ASTNode* node) {
        if (!node) return;
        if (node->node_type == NodeType::CaseExpr) case_count++;
        auto* child = node->first_child;
        while (child) {
            count_cases(child);
            child = child->next_sibling;
        }
    };
    count_cases(ast);
    EXPECT_EQ(case_count, 2);
}

// CASE in WHERE clause
TEST_F(CaseExprTest, CaseInWhere) {
    auto* ast = parse(
        "SELECT * FROM users WHERE "
        "CASE WHEN age >= 18 THEN status = 'active' ELSE parent_approved = 1 END"
    );
    ASSERT_NE(ast, nullptr);
    
    auto* where_clause = find_node_by_type(ast, NodeType::WhereClause);
    ASSERT_NE(where_clause, nullptr);
    
    auto* case_expr = find_node_by_type(where_clause, NodeType::CaseExpr);
    ASSERT_NE(case_expr, nullptr);
}

// CASE with complex expressions
TEST_F(CaseExprTest, CaseWithComplexExpressions) {
    auto* ast = parse(
        "SELECT CASE "
        "WHEN price * quantity > 1000 THEN 'high' "
        "WHEN price * quantity BETWEEN 100 AND 1000 THEN 'medium' "
        "ELSE 'low' END FROM orders"
    );
    ASSERT_NE(ast, nullptr);
    
    auto* case_expr = find_node_by_type(ast, NodeType::CaseExpr);
    ASSERT_NE(case_expr, nullptr);
}

// CASE with NULL handling
TEST_F(CaseExprTest, CaseWithNull) {
    auto* ast = parse(
        "SELECT CASE "
        "WHEN email IS NULL THEN 'no email' "
        "WHEN email LIKE '%@%' THEN 'valid' "
        "ELSE 'invalid' END FROM users"
    );
    ASSERT_NE(ast, nullptr);
    
    auto* case_expr = find_node_by_type(ast, NodeType::CaseExpr);
    ASSERT_NE(case_expr, nullptr);
}