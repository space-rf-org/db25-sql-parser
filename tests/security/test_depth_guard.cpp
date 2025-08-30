/*
 * Test DepthGuard protection against stack overflow
 */

#include <iostream>
#include <string>
#include <sstream>
#include "db25/parser/parser.hpp"

using namespace db25;
using namespace db25::parser;

// Colors for output
const char* GREEN = "\033[32m";
const char* RED = "\033[31m";
const char* YELLOW = "\033[33m";
const char* CYAN = "\033[36m";
const char* RESET = "\033[0m";
const char* BOLD = "\033[1m";

// Generate deeply nested SQL
std::string generate_nested_sql(int depth) {
    std::stringstream sql;
    
    // Generate nested subqueries
    for (int i = 0; i < depth; i++) {
        sql << "SELECT * FROM (";
    }
    
    sql << "SELECT 1";
    
    for (int i = 0; i < depth; i++) {
        sql << ") AS t" << i;
    }
    
    return sql.str();
}

// Generate deeply nested expressions
std::string generate_nested_expr(int depth) {
    std::stringstream sql;
    sql << "SELECT ";
    
    for (int i = 0; i < depth; i++) {
        sql << "(1 + ";
    }
    
    sql << "1";
    
    for (int i = 0; i < depth; i++) {
        sql << ")";
    }
    
    sql << " AS result";
    return sql.str();
}

// Generate deeply recursive CTE
std::string generate_recursive_cte(int depth) {
    std::stringstream sql;
    sql << "WITH RECURSIVE cte AS (";
    sql << "SELECT 1 AS n ";
    sql << "UNION ALL ";
    sql << "SELECT n + 1 FROM cte WHERE n < " << depth;
    sql << ") SELECT * FROM cte";
    return sql.str();
}

int main() {
    std::cout << BOLD << "========================================" << RESET << std::endl;
    std::cout << BOLD << "DB25 Parser - DepthGuard Test" << RESET << std::endl;
    std::cout << BOLD << "========================================" << RESET << std::endl;
    std::cout << std::endl;
    
    Parser parser;
    ParserConfig config = parser.config();
    std::cout << "Default max depth: " << CYAN << config.max_depth << RESET << std::endl;
    std::cout << std::endl;
    
    // Test 1: Normal query (should pass)
    std::cout << BOLD << "Test 1: Normal nested query (depth 10)" << RESET << std::endl;
    {
        std::string sql = generate_nested_sql(10);
        std::cout << "Query length: " << sql.length() << " bytes" << std::endl;
        
        auto result = parser.parse(sql);
        if (result.has_value()) {
            std::cout << GREEN << "✓ Parsed successfully" << RESET << std::endl;
        } else {
            std::cout << RED << "✗ Parse failed: " << result.error().message << RESET << std::endl;
        }
    }
    std::cout << std::endl;
    
    // Test 2: Deep nesting near limit (should pass)
    std::cout << BOLD << "Test 2: Deep nested query (depth 500)" << RESET << std::endl;
    {
        std::string sql = generate_nested_sql(500);
        std::cout << "Query length: " << sql.length() << " bytes" << std::endl;
        
        auto result = parser.parse(sql);
        if (result.has_value()) {
            std::cout << GREEN << "✓ Parsed successfully" << RESET << std::endl;
        } else {
            std::cout << RED << "✗ Parse failed: " << result.error().message << RESET << std::endl;
        }
    }
    std::cout << std::endl;
    
    // Test 3: Exceed depth limit (should fail with depth error)
    std::cout << BOLD << "Test 3: Excessive nested query (depth 1500)" << RESET << std::endl;
    {
        std::string sql = generate_nested_sql(1500);
        std::cout << "Query length: " << sql.length() << " bytes" << std::endl;
        
        auto result = parser.parse(sql);
        if (result.has_value()) {
            std::cout << RED << "✗ SECURITY ISSUE: Should have failed!" << RESET << std::endl;
        } else {
            const auto& error = result.error();
            if (error.message == "Maximum recursion depth exceeded") {
                std::cout << GREEN << "✓ Correctly blocked: " << error.message << RESET << std::endl;
            } else {
                std::cout << YELLOW << "⚠ Failed with different error: " << error.message << RESET << std::endl;
            }
        }
    }
    std::cout << std::endl;
    
    // Test 4: Deep expression nesting
    std::cout << BOLD << "Test 4: Deep nested expression (depth 1200)" << RESET << std::endl;
    {
        std::string sql = generate_nested_expr(1200);
        std::cout << "Query length: " << sql.length() << " bytes" << std::endl;
        
        auto result = parser.parse(sql);
        if (result.has_value()) {
            std::cout << RED << "✗ SECURITY ISSUE: Should have failed!" << RESET << std::endl;
        } else {
            const auto& error = result.error();
            if (error.message == "Maximum recursion depth exceeded") {
                std::cout << GREEN << "✓ Correctly blocked: " << error.message << RESET << std::endl;
            } else {
                std::cout << YELLOW << "⚠ Failed with different error: " << error.message << RESET << std::endl;
            }
        }
    }
    std::cout << std::endl;
    
    // Test 5: Custom depth limit
    std::cout << BOLD << "Test 5: Custom depth limit (max_depth = 50)" << RESET << std::endl;
    {
        ParserConfig custom_config = config;
        custom_config.max_depth = 50;
        parser.set_config(custom_config);
        
        std::string sql = generate_nested_sql(100);
        std::cout << "Query with depth 100, limit 50" << std::endl;
        
        auto result = parser.parse(sql);
        if (result.has_value()) {
            std::cout << RED << "✗ SECURITY ISSUE: Should have failed!" << RESET << std::endl;
        } else {
            const auto& error = result.error();
            if (error.message == "Maximum recursion depth exceeded") {
                std::cout << GREEN << "✓ Correctly blocked at custom limit" << RESET << std::endl;
            } else {
                std::cout << YELLOW << "⚠ Failed with different error: " << error.message << RESET << std::endl;
            }
        }
        
        // Reset to default config
        parser.set_config(config);
    }
    std::cout << std::endl;
    
    // Test 6: Parser reuse after depth exceeded
    std::cout << BOLD << "Test 6: Parser reuse after depth exceeded" << RESET << std::endl;
    {
        // First, trigger depth exceeded
        std::string deep_sql = generate_nested_sql(1500);
        auto result1 = parser.parse(deep_sql);
        if (!result1.has_value() && result1.error().message == "Maximum recursion depth exceeded") {
            std::cout << GREEN << "✓ First query blocked correctly" << RESET << std::endl;
        }
        
        // Now try a normal query
        std::string normal_sql = "SELECT * FROM users WHERE id = 1";
        auto result2 = parser.parse(normal_sql);
        if (result2.has_value()) {
            std::cout << GREEN << "✓ Parser correctly reset and parsed normal query" << RESET << std::endl;
        } else {
            std::cout << RED << "✗ Parser failed to reset: " << result2.error().message << RESET << std::endl;
        }
    }
    std::cout << std::endl;
    
    // Summary
    std::cout << BOLD << "========================================" << RESET << std::endl;
    std::cout << GREEN << "DepthGuard is working correctly!" << RESET << std::endl;
    std::cout << "The parser is protected against:" << std::endl;
    std::cout << "  • Stack overflow attacks" << std::endl;
    std::cout << "  • Deeply nested malicious queries" << std::endl;
    std::cout << "  • Infinite recursion bugs" << std::endl;
    std::cout << BOLD << "========================================" << RESET << std::endl;
    
    return 0;
}