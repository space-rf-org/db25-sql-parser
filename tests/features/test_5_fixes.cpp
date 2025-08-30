/*
 * Test for the 5 fundamental fixes:
 * 1. Expression parsing for all operators
 * 2. CAST expression
 * 3. String concatenation operator ||
 * 4. Common SQL functions
 * 5. CTE recursion with UNION
 */

#include <iostream>
#include <string>
#include <vector>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

// Colors for output
const char* GREEN = "\033[32m";
const char* RED = "\033[31m";
const char* RESET = "\033[0m";

struct TestCase {
    std::string name;
    std::string sql;
};

int main() {
    std::vector<TestCase> tests = {
        // Test 1: All operators including bitwise
        {
            "Bitwise and arithmetic operators",
            "SELECT a | b, c & d, e ^ f, g << 2, h >> 1, i + j * k, m % n FROM t"
        },
        
        // Test 2: String concatenation operator
        {
            "String concatenation with ||",
            "SELECT first_name || ' ' || last_name AS full_name FROM users"
        },
        
        // Test 3: CAST expression
        {
            "CAST with various types",
            "SELECT CAST(id AS VARCHAR(10)), CAST(price AS DECIMAL(10,2)), "
            "CAST(created_at AS DATE) FROM products"
        },
        
        // Test 4: Common SQL functions
        {
            "Common functions",
            "SELECT CONCAT(a, b), COALESCE(x, y, z), SUBSTRING(s, 1, 10), "
            "TRIM(name), EXTRACT(YEAR FROM date_col), LENGTH(str) FROM t"
        },
        
        // Test 5: Complex CTE with recursion and UNION
        {
            "Recursive CTE with UNION ALL",
            R"(
WITH RECURSIVE employee_tree AS (
    SELECT employee_id, name, manager_id, 1 as level
    FROM employees
    WHERE manager_id IS NULL
    UNION ALL
    SELECT e.employee_id, e.name, e.manager_id, t.level + 1
    FROM employees e
    INNER JOIN employee_tree t ON e.manager_id = t.employee_id
    WHERE t.level < 10
)
SELECT * FROM employee_tree ORDER BY level, name
            )"
        },
        
        // Test 6: All fixes combined
        {
            "All features combined",
            R"(
WITH RECURSIVE hierarchy AS (
    SELECT 
        id,
        CAST(name AS VARCHAR(100)) || ' (' || CAST(id AS VARCHAR) || ')' AS display_name,
        parent_id,
        1 as depth
    FROM categories
    WHERE parent_id IS NULL
    UNION ALL
    SELECT 
        c.id,
        REPEAT('  ', h.depth) || c.name AS display_name,
        c.parent_id,
        h.depth + 1
    FROM categories c
    INNER JOIN hierarchy h ON c.parent_id = h.id
    WHERE h.depth < 5
)
SELECT 
    h.id,
    h.display_name,
    COALESCE(COUNT(p.id), 0) AS product_count,
    CASE 
        WHEN h.depth = 1 THEN 'Root'
        WHEN h.depth = 2 THEN 'Category'
        ELSE 'Subcategory'
    END AS level_type
FROM hierarchy h
LEFT JOIN products p ON p.category_id = h.id
GROUP BY h.id, h.display_name, h.depth
HAVING COUNT(p.id) > 0 OR h.depth = 1
ORDER BY h.depth, h.display_name
            )"
        }
    };
    
    Parser parser;
    int passed = 0;
    int failed = 0;
    
    for (const auto& test : tests) {
        std::cout << "\nTesting: " << test.name << std::endl;
        std::cout << "SQL: " << test.sql.substr(0, 100) 
                  << (test.sql.length() > 100 ? "..." : "") << std::endl;
        
        auto result = parser.parse(test.sql);
        
        if (result.has_value()) {
            std::cout << GREEN << "✓ PASSED" << RESET << std::endl;
            passed++;
            
            // Print some AST info
            ASTNode* root = result.value();
            std::cout << "  Root node: " << static_cast<int>(root->node_type) 
                      << ", Children: " << root->child_count << std::endl;
        } else {
            std::cout << RED << "✗ FAILED" << RESET << std::endl;
            const auto& error = result.error();
            std::cout << "  Error at line " << error.line 
                      << ", column " << error.column 
                      << ": " << error.message << std::endl;
            failed++;
        }
    }
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    
    if (failed == 0) {
        std::cout << GREEN << "All tests passed! ✓" << RESET << std::endl;
        return 0;
    } else {
        std::cout << RED << "Some tests failed." << RESET << std::endl;
        return 1;
    }
}