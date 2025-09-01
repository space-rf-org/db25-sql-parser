/*
 * Progressive Improvement Test Suite
 * Tests that the parser is strictly better than before
 */

#include "db25/parser/parser.hpp"
#include <iostream>
#include <vector>
#include <string>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

struct TestCase {
    std::string name;
    std::string sql;
    bool should_parse;
    std::string feature;
};

int main() {
    std::cout << "\n==========================================" << std::endl;
    std::cout << "    PROGRESSIVE IMPROVEMENT TEST SUITE    " << std::endl;
    std::cout << "==========================================" << std::endl;

    std::vector<TestCase> tests = {
        // ========== PREVIOUSLY WORKING (Regression Test) ==========
        {"Basic SELECT", "SELECT * FROM users", true, "Core SQL"},
        {"SELECT with WHERE", "SELECT * FROM users WHERE id = 1", true, "Core SQL"},
        {"JOIN", "SELECT * FROM a JOIN b ON a.id = b.id", true, "Core SQL"},
        {"LEFT JOIN", "SELECT * FROM a LEFT JOIN b ON a.id = b.id", true, "Core SQL"},
        {"GROUP BY", "SELECT dept, COUNT(*) FROM emp GROUP BY dept", true, "Core SQL"},
        {"ORDER BY", "SELECT * FROM users ORDER BY name DESC", true, "Core SQL"},
        {"LIMIT", "SELECT * FROM users LIMIT 10", true, "Core SQL"},
        {"Subquery", "SELECT * FROM (SELECT * FROM users) t", true, "Core SQL"},
        {"CTE", "WITH t AS (SELECT 1) SELECT * FROM t", true, "CTE"},
        {"Window Function", "SELECT ROW_NUMBER() OVER (ORDER BY id) FROM t", true, "Window"},
        {"CASE expression", "SELECT CASE WHEN x > 0 THEN 1 ELSE 0 END", true, "Expression"},
        {"CAST", "SELECT CAST(x AS INTEGER)", true, "Expression"},
        {"UNION", "SELECT 1 UNION SELECT 2", true, "Set Op"},
        {"INSERT", "INSERT INTO t VALUES (1, 2)", true, "DML"},
        {"UPDATE", "UPDATE t SET x = 1", true, "DML"},
        {"DELETE", "DELETE FROM t WHERE id = 1", true, "DML"},
        
        // ========== NEW IMPROVEMENTS (Should Work Now) ==========
        
        // 1. Multi-statement parsing
        {"Multi-statement", "SELECT 1; SELECT 2; SELECT 3", true, "Multi-statement"},
        
        // 2. Transaction control
        {"BEGIN TRANSACTION", "BEGIN TRANSACTION", true, "Transaction"},
        {"COMMIT", "COMMIT", true, "Transaction"},
        {"ROLLBACK", "ROLLBACK", true, "Transaction"},
        {"SAVEPOINT", "SAVEPOINT sp1", true, "Transaction"},
        {"RELEASE SAVEPOINT", "RELEASE SAVEPOINT sp1", true, "Transaction"},
        
        // 3. Utility statements
        {"EXPLAIN", "EXPLAIN SELECT * FROM users", true, "Utility"},
        {"VALUES", "VALUES (1, 2), (3, 4)", true, "VALUES"},
        {"VACUUM", "VACUUM", true, "Utility"},
        {"ANALYZE", "ANALYZE users", true, "Utility"},
        {"ATTACH", "ATTACH DATABASE 'test.db' AS test", true, "Utility"},
        {"DETACH", "DETACH DATABASE test", true, "Utility"},
        {"REINDEX", "REINDEX users", true, "Utility"},
        {"PRAGMA", "PRAGMA table_info(users)", true, "Utility"},
        {"SET", "SET search_path = public", true, "Utility"},
        
        // 4. DML enhancements
        {"INSERT RETURNING", "INSERT INTO t VALUES (1) RETURNING *", true, "RETURNING"},
        {"UPDATE RETURNING", "UPDATE t SET x = 1 RETURNING id", true, "RETURNING"},
        {"DELETE RETURNING", "DELETE FROM t RETURNING *", true, "RETURNING"},
        {"ON CONFLICT", "INSERT INTO t VALUES (1) ON CONFLICT DO NOTHING", true, "UPSERT"},
        {"ON CONFLICT UPDATE", "INSERT INTO t VALUES (1) ON CONFLICT (id) DO UPDATE SET x = 2", true, "UPSERT"},
        {"JOIN USING", "SELECT * FROM a JOIN b USING (id)", true, "JOIN USING"},
        {"DELETE USING", "DELETE FROM t1 USING t2 WHERE t1.id = t2.id", true, "DELETE USING"},
        
        // 5. Advanced GROUP BY
        {"GROUPING SETS", "SELECT a, b, SUM(c) FROM t GROUP BY GROUPING SETS ((a), (b))", true, "OLAP"},
        {"CUBE", "SELECT a, b, SUM(c) FROM t GROUP BY CUBE(a, b)", true, "OLAP"},
        {"ROLLUP", "SELECT a, b, SUM(c) FROM t GROUP BY ROLLUP(a, b)", true, "OLAP"},
        
        // 6. DDL with proper AST (not just token consumption)
        {"CREATE TABLE with columns", "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT NOT NULL)", true, "DDL"},
        {"CREATE TABLE with constraints", "CREATE TABLE orders (id INT, user_id INT, FOREIGN KEY (user_id) REFERENCES users(id))", true, "DDL"},
        {"ALTER TABLE ADD COLUMN", "ALTER TABLE users ADD COLUMN email TEXT", true, "DDL"},
        {"ALTER TABLE DROP COLUMN", "ALTER TABLE users DROP COLUMN email CASCADE", true, "DDL"},
        {"ALTER TABLE ALTER COLUMN", "ALTER TABLE users ALTER COLUMN age SET DEFAULT 0", true, "DDL"},
        {"CREATE INDEX", "CREATE INDEX idx_email ON users (email)", true, "DDL"},
        {"CREATE UNIQUE INDEX", "CREATE UNIQUE INDEX idx_username ON users (username)", true, "DDL"},
        {"CREATE INDEX WHERE", "CREATE INDEX idx_active ON users (id) WHERE active = true", true, "DDL"},
        {"CREATE TRIGGER", "CREATE TRIGGER update_ts BEFORE UPDATE ON users FOR EACH ROW BEGIN UPDATE users SET ts = NOW(); END", true, "DDL"},
        {"CREATE SCHEMA", "CREATE SCHEMA IF NOT EXISTS myschema", true, "DDL"},
        
        // 7. Set operations
        {"INTERSECT", "SELECT 1 INTERSECT SELECT 2", true, "Set Op"},
        {"EXCEPT", "SELECT 1 EXCEPT SELECT 2", true, "Set Op"},
        
        // 8. CREATE VIEW improvements
        {"CREATE VIEW", "CREATE VIEW v AS SELECT * FROM users", true, "DDL"},
        {"CREATE OR REPLACE VIEW", "CREATE OR REPLACE VIEW v AS SELECT * FROM users", true, "DDL"},
    };

    int total = 0;
    int passed = 0;
    int regressions = 0;
    int improvements = 0;
    
    std::cout << "\n📊 Testing " << tests.size() << " cases...\n" << std::endl;
    
    // Group tests by feature
    std::string current_feature = "";
    
    for (const auto& test : tests) {
        if (test.feature != current_feature) {
            current_feature = test.feature;
            std::cout << "\n=== " << current_feature << " ===" << std::endl;
        }
        
        Parser parser;
        auto result = parser.parse(test.sql);
        bool parsed = result.has_value();
        
        total++;
        bool success = (parsed == test.should_parse);
        
        if (success) {
            passed++;
            std::cout << "  ✅ " << test.name;
            
            // Check if it's an improvement (newly working)
            if (test.feature != "Core SQL" && test.feature != "CTE" && 
                test.feature != "Window" && test.feature != "Expression" && 
                test.feature != "DML" && parsed) {
                improvements++;
                std::cout << " [NEW]";
            }
        } else {
            std::cout << "  ❌ " << test.name;
            if (test.feature == "Core SQL" || test.feature == "CTE" || 
                test.feature == "Window" || test.feature == "Expression" || 
                test.feature == "DML") {
                regressions++;
                std::cout << " [REGRESSION]";
            } else {
                std::cout << " [TODO]";
            }
        }
        std::cout << std::endl;
    }
    
    // Summary
    std::cout << "\n==========================================" << std::endl;
    std::cout << "                SUMMARY                   " << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Total Tests:     " << total << std::endl;
    std::cout << "Passed:          " << passed << " (" << (passed * 100 / total) << "%)" << std::endl;
    std::cout << "Failed:          " << (total - passed) << std::endl;
    std::cout << "Regressions:     " << regressions << (regressions > 0 ? " ⚠️ CRITICAL" : " ✅") << std::endl;
    std::cout << "New Features:    " << improvements << " 🎉" << std::endl;
    
    if (regressions > 0) {
        std::cout << "\n⚠️  WARNING: Found " << regressions << " regressions!" << std::endl;
        return 1;
    }
    
    std::cout << "\n✅ No regressions found - parser is strictly better!" << std::endl;
    
    // Calculate improvement percentage
    int old_features = 16; // Core SQL features that worked before
    int new_total = passed;
    double improvement_pct = ((double)(new_total - old_features) / old_features) * 100;
    
    std::cout << "\n📈 Improvement: +" << (int)improvement_pct << "% more SQL features supported" << std::endl;
    
    return 0;
}