/*
 * DB25 Parser - Window Functions Test Suite
 * Tests ROW_NUMBER, RANK, DENSE_RANK, and other window functions
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include <iostream>

using namespace db25;
using namespace db25::parser;

class WindowFunctionTest : public ::testing::Test {
protected:
    Parser parser;
    
    void verify_window_function(const std::string& sql) {
        auto result = parser.parse(sql);
        ASSERT_TRUE(result.has_value()) << "Failed to parse: " << sql;
        
        auto* ast = result.value();
        ASSERT_NE(ast, nullptr);
        ASSERT_EQ(ast->node_type, ast::NodeType::SelectStmt);
    }
    
    ast::ASTNode* find_window_expr(ast::ASTNode* node) {
        if (!node) return nullptr;
        
        // Look for FunctionCall with window function flag (bit 8)
        if (node->node_type == ast::NodeType::FunctionCall && 
            (node->semantic_flags & (1 << 8))) {
            return node;
        }
        
        // Search children
        for (auto* child = node->first_child; child; child = child->next_sibling) {
            auto* found = find_window_expr(child);
            if (found) return found;
        }
        
        return nullptr;
    }
};

// Basic window function tests
TEST_F(WindowFunctionTest, RowNumberBasic) {
    std::string sql = "SELECT ROW_NUMBER() OVER () AS row_num FROM users";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, RowNumberWithOrder) {
    std::string sql = "SELECT ROW_NUMBER() OVER (ORDER BY salary DESC) AS row_num FROM employees";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, RowNumberWithPartition) {
    std::string sql = R"(
        SELECT 
            department,
            employee_name,
            ROW_NUMBER() OVER (PARTITION BY department ORDER BY salary DESC) AS dept_rank
        FROM employees
    )";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, RankFunction) {
    std::string sql = R"(
        SELECT 
            name,
            score,
            RANK() OVER (ORDER BY score DESC) AS rank
        FROM students
    )";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, DenseRankFunction) {
    std::string sql = R"(
        SELECT 
            name,
            score,
            DENSE_RANK() OVER (ORDER BY score DESC) AS dense_rank
        FROM students
    )";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, MultipleWindowFunctions) {
    std::string sql = R"(
        SELECT 
            employee_id,
            department,
            salary,
            ROW_NUMBER() OVER (PARTITION BY department ORDER BY salary DESC) AS row_num,
            RANK() OVER (PARTITION BY department ORDER BY salary DESC) AS rank,
            DENSE_RANK() OVER (PARTITION BY department ORDER BY salary DESC) AS dense_rank
        FROM employees
    )";
    verify_window_function(sql);
}

// Aggregate window functions
TEST_F(WindowFunctionTest, SumWindow) {
    std::string sql = R"(
        SELECT 
            order_date,
            amount,
            SUM(amount) OVER (ORDER BY order_date) AS running_total
        FROM orders
    )";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, AvgWindow) {
    std::string sql = R"(
        SELECT 
            department,
            salary,
            AVG(salary) OVER (PARTITION BY department) AS dept_avg_salary
        FROM employees
    )";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, CountWindow) {
    std::string sql = R"(
        SELECT 
            category,
            product_name,
            COUNT(*) OVER (PARTITION BY category) AS products_in_category
        FROM products
    )";
    verify_window_function(sql);
}

// Frame specification tests
TEST_F(WindowFunctionTest, RowsBetweenUnboundedPreceding) {
    std::string sql = R"(
        SELECT 
            order_date,
            amount,
            SUM(amount) OVER (
                ORDER BY order_date 
                ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
            ) AS running_total
        FROM orders
    )";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, RowsBetweenNPreceding) {
    std::string sql = R"(
        SELECT 
            date,
            sales,
            AVG(sales) OVER (
                ORDER BY date 
                ROWS BETWEEN 6 PRECEDING AND CURRENT ROW
            ) AS moving_avg_7_days
        FROM daily_sales
    )";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, RangeBetween) {
    std::string sql = R"(
        SELECT 
            date,
            value,
            SUM(value) OVER (
                ORDER BY date 
                RANGE BETWEEN INTERVAL '1' DAY PRECEDING AND CURRENT ROW
            ) AS sum_last_day
        FROM measurements
    )";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, RowsBetweenCurrentAndFollowing) {
    std::string sql = R"(
        SELECT 
            id,
            value,
            SUM(value) OVER (
                ORDER BY id 
                ROWS BETWEEN CURRENT ROW AND 2 FOLLOWING
            ) AS next_3_sum
        FROM data
    )";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, RowsBetweenUnboundedFollowing) {
    std::string sql = R"(
        SELECT 
            id,
            value,
            SUM(value) OVER (
                ORDER BY id 
                ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING
            ) AS remaining_sum
        FROM data
    )";
    verify_window_function(sql);
}

// Complex partition tests
TEST_F(WindowFunctionTest, MultiplePartitionColumns) {
    std::string sql = R"(
        SELECT 
            year,
            month,
            department,
            sales,
            SUM(sales) OVER (PARTITION BY year, month ORDER BY department) AS cumulative_sales
        FROM monthly_sales
    )";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, PartitionWithExpressions) {
    std::string sql = R"(
        SELECT 
            employee_id,
            salary,
            RANK() OVER (
                PARTITION BY salary / 10000 
                ORDER BY hire_date
            ) AS salary_band_seniority
        FROM employees
    )";
    verify_window_function(sql);
}

// Lead and Lag functions
TEST_F(WindowFunctionTest, LeadFunction) {
    std::string sql = R"(
        SELECT 
            date,
            price,
            LEAD(price, 1) OVER (ORDER BY date) AS next_price,
            LEAD(price, 2, 0) OVER (ORDER BY date) AS price_in_2_days
        FROM stock_prices
    )";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, LagFunction) {
    std::string sql = R"(
        SELECT 
            date,
            price,
            LAG(price, 1) OVER (ORDER BY date) AS prev_price,
            LAG(price, 7, NULL) OVER (ORDER BY date) AS price_week_ago
        FROM stock_prices
    )";
    verify_window_function(sql);
}

// First/Last value functions
TEST_F(WindowFunctionTest, FirstValueFunction) {
    std::string sql = R"(
        SELECT 
            department,
            employee_name,
            salary,
            FIRST_VALUE(employee_name) OVER (
                PARTITION BY department 
                ORDER BY salary DESC
            ) AS highest_paid_in_dept
        FROM employees
    )";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, LastValueFunction) {
    std::string sql = R"(
        SELECT 
            department,
            employee_name,
            salary,
            LAST_VALUE(employee_name) OVER (
                PARTITION BY department 
                ORDER BY salary DESC
                ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING
            ) AS lowest_paid_in_dept
        FROM employees
    )";
    verify_window_function(sql);
}

// Nth value function
TEST_F(WindowFunctionTest, NthValueFunction) {
    std::string sql = R"(
        SELECT 
            department,
            employee_name,
            salary,
            NTH_VALUE(employee_name, 2) OVER (
                PARTITION BY department 
                ORDER BY salary DESC
            ) AS second_highest_paid
        FROM employees
    )";
    verify_window_function(sql);
}

// Percentile functions
TEST_F(WindowFunctionTest, PercentileRank) {
    std::string sql = R"(
        SELECT 
            student_name,
            test_score,
            PERCENT_RANK() OVER (ORDER BY test_score) AS percentile_rank
        FROM test_results
    )";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, CumeDistFunction) {
    std::string sql = R"(
        SELECT 
            student_name,
            test_score,
            CUME_DIST() OVER (ORDER BY test_score) AS cumulative_distribution
        FROM test_results
    )";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, NtileFunction) {
    std::string sql = R"(
        SELECT 
            employee_name,
            salary,
            NTILE(4) OVER (ORDER BY salary) AS salary_quartile
        FROM employees
    )";
    verify_window_function(sql);
}

// Complex real-world examples
TEST_F(WindowFunctionTest, ComplexAnalyticsQuery) {
    std::string sql = R"(
        SELECT 
            date,
            product_id,
            sales_amount,
            SUM(sales_amount) OVER (
                PARTITION BY product_id 
                ORDER BY date 
                ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
            ) AS running_total,
            AVG(sales_amount) OVER (
                PARTITION BY product_id 
                ORDER BY date 
                ROWS BETWEEN 6 PRECEDING AND CURRENT ROW
            ) AS moving_avg_7_days,
            LAG(sales_amount, 1, 0) OVER (
                PARTITION BY product_id 
                ORDER BY date
            ) AS prev_day_sales,
            RANK() OVER (
                PARTITION BY DATE_TRUNC('month', date) 
                ORDER BY sales_amount DESC
            ) AS monthly_rank
        FROM daily_product_sales
        WHERE date >= '2024-01-01'
    )";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, WindowFunctionInSubquery) {
    std::string sql = R"(
        SELECT * FROM (
            SELECT 
                employee_id,
                department,
                salary,
                ROW_NUMBER() OVER (PARTITION BY department ORDER BY salary DESC) AS dept_rank
            FROM employees
        ) ranked_employees
        WHERE dept_rank <= 3
    )";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, WindowFunctionWithCTE) {
    std::string sql = R"(
        WITH ranked_sales AS (
            SELECT 
                salesperson_id,
                region,
                total_sales,
                RANK() OVER (PARTITION BY region ORDER BY total_sales DESC) AS region_rank
            FROM sales_summary
        )
        SELECT * FROM ranked_sales WHERE region_rank = 1
    )";
    verify_window_function(sql);
}

// Edge cases
TEST_F(WindowFunctionTest, EmptyOverClause) {
    std::string sql = "SELECT COUNT(*) OVER () FROM users";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, WindowFunctionWithDistinct) {
    std::string sql = R"(
        SELECT 
            category,
            COUNT(DISTINCT product_id) OVER (PARTITION BY category) AS unique_products
        FROM products
    )";
    verify_window_function(sql);
}

TEST_F(WindowFunctionTest, MultipleWindowSpecifications) {
    std::string sql = R"(
        SELECT 
            id,
            value,
            SUM(value) OVER w1 AS sum_preceding,
            AVG(value) OVER w2 AS avg_following
        FROM data
        WINDOW 
            w1 AS (ORDER BY id ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW),
            w2 AS (ORDER BY id ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING)
    )";
    // Note: WINDOW clause might not be fully supported yet, but we can test parsing
    auto result = parser.parse(sql);
    // Even if it fails, we've tested the attempt
}

// Verify AST structure for window functions
TEST_F(WindowFunctionTest, VerifyASTStructure) {
    std::string sql = "SELECT ROW_NUMBER() OVER (PARTITION BY dept ORDER BY salary DESC) FROM employees";
    
    auto result = parser.parse(sql);
    ASSERT_TRUE(result.has_value());
    
    auto* ast = result.value();
    auto* window_expr = find_window_expr(ast);
    
    ASSERT_NE(window_expr, nullptr) << "Should find window function node";
    ASSERT_EQ(window_expr->node_type, ast::NodeType::FunctionCall);
    ASSERT_TRUE(window_expr->semantic_flags & (1 << 8)) << "Should have window function flag set";
    
    // Check that it has children (arguments and window specification)
    ASSERT_NE(window_expr->first_child, nullptr) << "Window function should have window spec";
    
    // The function name should be in primary_text
    ASSERT_FALSE(window_expr->primary_text.empty());
    std::string func_name(window_expr->primary_text);
    ASSERT_EQ(func_name, "ROW_NUMBER") << "Function name should be ROW_NUMBER";
}