/*
 * Test GROUP BY and HAVING clauses with aggregate functions
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include "db25/ast/node_types.hpp"
#include <string>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class GroupByTest : public ::testing::Test {
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

// Test simple GROUP BY with single column
TEST_F(GroupByTest, SimpleGroupBy) {
    auto* ast = parse("SELECT department, COUNT(*) FROM employees GROUP BY department");
    ASSERT_NE(ast, nullptr);
    
    auto* group_by = find_node_by_type(ast, NodeType::GroupByClause);
    ASSERT_NE(group_by, nullptr);
    EXPECT_EQ(group_by->child_count, 1); // One grouping column
    
    // First child should be the column
    auto* column = group_by->first_child;
    ASSERT_NE(column, nullptr);
    EXPECT_EQ(column->node_type, NodeType::ColumnRef);
    EXPECT_EQ(column->primary_text, "department");
}

// Test GROUP BY with multiple columns
TEST_F(GroupByTest, MultipleGroupByColumns) {
    auto* ast = parse("SELECT department, job_title, COUNT(*) FROM employees GROUP BY department, job_title");
    ASSERT_NE(ast, nullptr);
    
    auto* group_by = find_node_by_type(ast, NodeType::GroupByClause);
    ASSERT_NE(group_by, nullptr);
    EXPECT_EQ(group_by->child_count, 2); // Two grouping columns
    
    // First column
    auto* col1 = group_by->first_child;
    ASSERT_NE(col1, nullptr);
    EXPECT_EQ(col1->node_type, NodeType::ColumnRef);
    EXPECT_EQ(col1->primary_text, "department");
    
    // Second column
    auto* col2 = col1->next_sibling;
    ASSERT_NE(col2, nullptr);
    EXPECT_EQ(col2->node_type, NodeType::ColumnRef);
    EXPECT_EQ(col2->primary_text, "job_title");
}

// Test GROUP BY with expression
TEST_F(GroupByTest, GroupByWithExpression) {
    auto* ast = parse("SELECT YEAR(hire_date), COUNT(*) FROM employees GROUP BY YEAR(hire_date)");
    ASSERT_NE(ast, nullptr);
    
    auto* group_by = find_node_by_type(ast, NodeType::GroupByClause);
    ASSERT_NE(group_by, nullptr);
    EXPECT_EQ(group_by->child_count, 1);
    
    // Should have a function call
    auto* func = group_by->first_child;
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->node_type, NodeType::FunctionCall);
    EXPECT_EQ(func->primary_text, "YEAR");
}

// Test GROUP BY with HAVING
TEST_F(GroupByTest, GroupByWithHaving) {
    auto* ast = parse("SELECT department, AVG(salary) FROM employees GROUP BY department HAVING AVG(salary) > 50000");
    ASSERT_NE(ast, nullptr);
    
    // Should have both GROUP BY and HAVING
    auto* group_by = find_node_by_type(ast, NodeType::GroupByClause);
    ASSERT_NE(group_by, nullptr);
    
    auto* having = find_node_by_type(ast, NodeType::HavingClause);
    ASSERT_NE(having, nullptr);
    
    // HAVING should contain a comparison
    auto* comparison = having->first_child;
    ASSERT_NE(comparison, nullptr);
    EXPECT_EQ(comparison->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(comparison->primary_text, ">");
}

// Test HAVING without GROUP BY (should parse but semantic error)
TEST_F(GroupByTest, HavingWithoutGroupBy) {
    auto* ast = parse("SELECT * FROM employees HAVING COUNT(*) > 10");
    ASSERT_NE(ast, nullptr);
    
    // Should still parse HAVING
    auto* having = find_node_by_type(ast, NodeType::HavingClause);
    ASSERT_NE(having, nullptr);
}

// Test aggregate functions
TEST_F(GroupByTest, AggregateFunction_COUNT) {
    auto* ast = parse("SELECT COUNT(*) FROM employees");
    ASSERT_NE(ast, nullptr);
    
    auto* func = find_node_by_type(ast, NodeType::FunctionCall);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->primary_text, "COUNT");
    
    // COUNT(*) should have a Star child
    auto* star = func->first_child;
    ASSERT_NE(star, nullptr);
    EXPECT_EQ(star->node_type, NodeType::Star);
}

TEST_F(GroupByTest, AggregateFunction_SUM) {
    auto* ast = parse("SELECT SUM(salary) FROM employees");
    ASSERT_NE(ast, nullptr);
    
    auto* func = find_node_by_type(ast, NodeType::FunctionCall);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->primary_text, "SUM");
    
    // SUM should have column argument
    auto* arg = func->first_child;
    ASSERT_NE(arg, nullptr);
    EXPECT_EQ(arg->node_type, NodeType::Identifier);
    EXPECT_EQ(arg->primary_text, "salary");
}

TEST_F(GroupByTest, AggregateFunction_AVG) {
    auto* ast = parse("SELECT AVG(salary) FROM employees");
    ASSERT_NE(ast, nullptr);
    
    auto* func = find_node_by_type(ast, NodeType::FunctionCall);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->primary_text, "AVG");
}

TEST_F(GroupByTest, AggregateFunction_MIN) {
    auto* ast = parse("SELECT MIN(salary) FROM employees");
    ASSERT_NE(ast, nullptr);
    
    auto* func = find_node_by_type(ast, NodeType::FunctionCall);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->primary_text, "MIN");
}

TEST_F(GroupByTest, AggregateFunction_MAX) {
    auto* ast = parse("SELECT MAX(salary) FROM employees");
    ASSERT_NE(ast, nullptr);
    
    auto* func = find_node_by_type(ast, NodeType::FunctionCall);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->primary_text, "MAX");
}

// Test multiple aggregates
TEST_F(GroupByTest, MultipleAggregates) {
    auto* ast = parse("SELECT department, COUNT(*), AVG(salary), MAX(salary) FROM employees GROUP BY department");
    ASSERT_NE(ast, nullptr);
    
    // Should have 3 function calls
    int func_count = count_nodes_of_type(ast, NodeType::FunctionCall);
    EXPECT_EQ(func_count, 3);
}

// Test GROUP BY with ORDER BY
TEST_F(GroupByTest, GroupByWithOrderBy) {
    auto* ast = parse("SELECT department, COUNT(*) FROM employees GROUP BY department ORDER BY COUNT(*) DESC");
    ASSERT_NE(ast, nullptr);
    
    // Should have both clauses
    EXPECT_NE(find_node_by_type(ast, NodeType::GroupByClause), nullptr);
    EXPECT_NE(find_node_by_type(ast, NodeType::OrderByClause), nullptr);
}

// Test complex HAVING condition
TEST_F(GroupByTest, ComplexHavingCondition) {
    auto* ast = parse("SELECT department FROM employees GROUP BY department HAVING COUNT(*) > 5 AND AVG(salary) > 50000");
    ASSERT_NE(ast, nullptr);
    
    auto* having = find_node_by_type(ast, NodeType::HavingClause);
    ASSERT_NE(having, nullptr);
    
    // Root of HAVING should be AND
    auto* and_expr = having->first_child;
    ASSERT_NE(and_expr, nullptr);
    EXPECT_EQ(and_expr->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(and_expr->primary_text, "AND");
}

// Test GROUP BY with qualified columns
TEST_F(GroupByTest, GroupByQualifiedColumns) {
    auto* ast = parse("SELECT e.department, COUNT(*) FROM employees e GROUP BY e.department");
    ASSERT_NE(ast, nullptr);
    
    auto* group_by = find_node_by_type(ast, NodeType::GroupByClause);
    ASSERT_NE(group_by, nullptr);
    
    // Should have qualified column
    auto* column = group_by->first_child;
    ASSERT_NE(column, nullptr);
    EXPECT_EQ(column->node_type, NodeType::ColumnRef);
    EXPECT_EQ(column->primary_text, "e.department");
}

// Test GROUP BY position (GROUP BY 1, 2)
TEST_F(GroupByTest, GroupByPosition) {
    auto* ast = parse("SELECT department, job_title FROM employees GROUP BY 1, 2");
    ASSERT_NE(ast, nullptr);
    
    auto* group_by = find_node_by_type(ast, NodeType::GroupByClause);
    ASSERT_NE(group_by, nullptr);
    EXPECT_EQ(group_by->child_count, 2);
    
    // Should have integer literals
    auto* pos1 = group_by->first_child;
    ASSERT_NE(pos1, nullptr);
    EXPECT_EQ(pos1->node_type, NodeType::IntegerLiteral);
    EXPECT_EQ(pos1->primary_text, "1");
    
    auto* pos2 = pos1->next_sibling;
    ASSERT_NE(pos2, nullptr);
    EXPECT_EQ(pos2->node_type, NodeType::IntegerLiteral);
    EXPECT_EQ(pos2->primary_text, "2");
}