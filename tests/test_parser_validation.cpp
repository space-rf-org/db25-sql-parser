/*
 * DB25 Parser Validation Test
 * Tests all major features to ensure parser is working correctly
 */

#include <iostream>
#include <vector>
#include <string>
#include "db25/parser/parser.hpp"

using namespace db25;
using namespace db25::parser;

struct TestCase {
    std::string name;
    std::string sql;
    bool should_pass;
};

int main() {
    std::vector<TestCase> tests = {
        // Basic DML
        {"Simple SELECT", "SELECT * FROM users", true},
        {"SELECT with WHERE", "SELECT id, name FROM users WHERE age > 21", true},
        {"INSERT", "INSERT INTO users (id, name) VALUES (1, 'John')", true},
        {"UPDATE", "UPDATE users SET name = 'Jane' WHERE id = 1", true},
        {"DELETE", "DELETE FROM users WHERE id = 1", true},
        
        // DDL
        {"CREATE TABLE", "CREATE TABLE users (id INT, name VARCHAR(255))", true},
        {"CREATE INDEX", "CREATE INDEX idx_name ON users(name)", true},
        {"CREATE VIEW", "CREATE VIEW v_users AS SELECT * FROM users", true},
        {"DROP TABLE", "DROP TABLE users", true},
        {"ALTER TABLE", "ALTER TABLE users ADD COLUMN age INT", true},
        {"TRUNCATE", "TRUNCATE TABLE users", true},
        
        // JOINs
        {"INNER JOIN", "SELECT * FROM users u INNER JOIN orders o ON u.id = o.user_id", true},
        {"LEFT JOIN", "SELECT * FROM users u LEFT JOIN orders o ON u.id = o.user_id", true},
        {"Multiple JOINs", "SELECT * FROM a JOIN b ON a.id = b.a_id JOIN c ON b.id = c.b_id", true},
        
        // CTEs
        {"Simple CTE", "WITH active AS (SELECT * FROM users WHERE active = 1) SELECT * FROM active", true},
        {"Recursive CTE", "WITH RECURSIVE nums AS (SELECT 1 AS n UNION ALL SELECT n+1 FROM nums WHERE n < 10) SELECT * FROM nums", true},
        
        // CASE expressions
        {"CASE WHEN", "SELECT CASE WHEN age < 18 THEN 'minor' ELSE 'adult' END FROM users", true},
        
        // Complex expressions
        {"Arithmetic", "SELECT price * quantity * (1 - discount / 100) AS total FROM orders", true},
        {"String concat", "SELECT first_name || ' ' || last_name AS full_name FROM users", true},
        {"CAST", "SELECT CAST(age AS VARCHAR(10)) FROM users", true},
        {"EXTRACT", "SELECT EXTRACT(YEAR FROM created_at) FROM users", true},
        
        // Operators
        {"BETWEEN", "SELECT * FROM users WHERE age BETWEEN 18 AND 65", true},
        {"IN list", "SELECT * FROM users WHERE status IN ('active', 'pending')", true},
        {"IN subquery", "SELECT * FROM users WHERE id IN (SELECT user_id FROM orders)", true},
        {"EXISTS", "SELECT * FROM users WHERE EXISTS (SELECT 1 FROM orders WHERE user_id = users.id)", true},
        {"LIKE", "SELECT * FROM users WHERE name LIKE 'John%'", true},
        {"IS NULL", "SELECT * FROM users WHERE deleted_at IS NULL", true},
        {"NOT operators", "SELECT * FROM users WHERE status NOT IN ('deleted') AND age IS NOT NULL", true},
        
        // Aggregate functions
        {"Aggregates", "SELECT COUNT(*), MAX(age), MIN(age), AVG(age), SUM(total) FROM orders", true},
        {"GROUP BY", "SELECT status, COUNT(*) FROM users GROUP BY status", true},
        {"HAVING", "SELECT status, COUNT(*) FROM users GROUP BY status HAVING COUNT(*) > 10", true},
        
        // Window functions: an inline OVER (...) spec parses; a named-window
        // reference OVER <name> is unsupported and must be REJECTED, not silently
        // mis-parsed as a column alias on the function (which drops the window).
        {"Window inline OVER", "SELECT rank() OVER (ORDER BY age) FROM users", true},
        {"Window empty OVER", "SELECT rank() OVER () FROM users", true},
        {"Window named OVER rejected", "SELECT rank() OVER w FROM users", false},

        // ORDER BY and LIMIT
        {"ORDER BY", "SELECT * FROM users ORDER BY created_at DESC, name ASC", true},
        {"LIMIT OFFSET", "SELECT * FROM users LIMIT 10 OFFSET 20", true},
        
        // UNION
        {"UNION", "SELECT id FROM users UNION SELECT id FROM customers", true},
        {"UNION ALL", "SELECT id FROM users UNION ALL SELECT id FROM customers", true},

        // FROM-less SELECT with WHERE / GROUP BY / HAVING / ORDER BY: legal SQL,
        // must NOT be rejected (validate_clause_dependencies used to require FROM
        // for these clauses, inconsistently with bare SELECT and the set-op path).
        {"FROM-less ORDER BY", "SELECT 1 ORDER BY 1", true},
        {"FROM-less ORDER BY alias", "SELECT 1+1 AS x ORDER BY x", true},
        {"FROM-less WHERE", "SELECT 1 WHERE 1 = 1", true},
        {"FROM-less GROUP BY", "SELECT 1 GROUP BY 1", true},
        {"FROM-less HAVING", "SELECT 1 HAVING 1 = 1", true},
        {"FROM-less bare SELECT", "SELECT 1", true},
        {"FROM-less SELECT LIMIT", "SELECT 1 LIMIT 5", true}
    };
    
    Parser parser;
    int passed = 0;
    int failed = 0;
    
    std::cout << "DB25 Parser Validation Test\n";
    std::cout << "============================\n\n";
    
    for (const auto& test : tests) {
        auto result = parser.parse(test.sql);
        bool success = result.has_value();
        
        if (success == test.should_pass) {
            std::cout << "✓ " << test.name << "\n";
            passed++;
        } else {
            std::cout << "✗ " << test.name;
            if (!success) {
                std::cout << " - Error: " << result.error().message;
            }
            std::cout << "\n";
            failed++;
        }
    }
    
    std::cout << "\n============================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "Success rate: " << (passed * 100.0 / tests.size()) << "%\n";
    
    return failed > 0 ? 1 : 0;
}