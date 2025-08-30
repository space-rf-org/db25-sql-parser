/*
 * DB25 Parser - Comprehensive Validation Test
 * Validates parser capabilities, advanced types, and overall correctness
 */

#include "db25/parser/parser.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <chrono>

using namespace db25::parser;
using namespace db25::ast;

struct TestCategory {
    std::string name;
    std::vector<std::pair<std::string, std::string>> tests; // query, description
    int passed = 0;
    int total = 0;
};

class ParserValidator {
private:
    Parser parser;
    std::vector<TestCategory> categories;
    
    void add_category(const std::string& name, 
                      const std::vector<std::pair<std::string, std::string>>& tests) {
        categories.push_back({name, tests, 0, static_cast<int>(tests.size())});
    }
    
    bool has_node_type(ASTNode* node, NodeType type) {
        if (!node) return false;
        if (node->node_type == type) return true;
        
        for (auto* child = node->first_child; child; child = child->next_sibling) {
            if (has_node_type(child, type)) return true;
        }
        return false;
    }
    
    bool contains_text(ASTNode* node, const std::string& text) {
        if (!node) return false;
        if (node->primary_text.find(text) != std::string::npos) return true;
        
        for (auto* child = node->first_child; child; child = child->next_sibling) {
            if (contains_text(child, text)) return true;
        }
        return false;
    }
    
public:
    void setup_tests() {
        // Core SQL Features
        add_category("Core SQL", {
            {"SELECT * FROM table", "Basic SELECT"},
            {"SELECT DISTINCT col FROM table", "DISTINCT"},
            {"SELECT * FROM t WHERE id = 1", "WHERE clause"},
            {"SELECT * FROM t ORDER BY col DESC", "ORDER BY"},
            {"SELECT * FROM t LIMIT 10 OFFSET 5", "LIMIT/OFFSET"},
            {"SELECT * FROM a JOIN b ON a.id = b.id", "JOIN"},
            {"SELECT * FROM a LEFT JOIN b ON a.id = b.id", "LEFT JOIN"},
            {"SELECT COUNT(*) FROM t GROUP BY col", "GROUP BY"},
            {"SELECT COUNT(*) FROM t GROUP BY col HAVING COUNT(*) > 1", "HAVING"},
            {"SELECT * FROM (SELECT * FROM t) AS sub", "Subquery"}
        });
        
        // Advanced SQL Features
        add_category("Advanced SQL", {
            {"WITH cte AS (SELECT 1) SELECT * FROM cte", "Common Table Expression"},
            {"WITH RECURSIVE cte AS (SELECT 1 UNION ALL SELECT n+1 FROM cte WHERE n < 10) SELECT * FROM cte", "Recursive CTE"},
            {"SELECT ROW_NUMBER() OVER (ORDER BY id) FROM t", "Window Function"},
            {"SELECT SUM(val) OVER (PARTITION BY grp ORDER BY date ROWS BETWEEN 1 PRECEDING AND CURRENT ROW) FROM t", "Window Frame"},
            {"SELECT CASE WHEN x > 0 THEN 1 ELSE 0 END FROM t", "CASE expression"},
            {"SELECT CAST(col AS INTEGER) FROM t", "CAST expression"},
            {"SELECT EXISTS (SELECT 1 FROM t2 WHERE t2.id = t1.id) FROM t1", "EXISTS"},
            {"SELECT * FROM t WHERE id IN (SELECT id FROM t2)", "IN subquery"},
            {"SELECT * FROM t WHERE id BETWEEN 1 AND 10", "BETWEEN"},
            {"SELECT * FROM t WHERE name LIKE '%test%'", "LIKE"}
        });
        
        // DDL Statements
        add_category("DDL", {
            {"CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT NOT NULL)", "CREATE TABLE"},
            {"CREATE TABLE orders (id INT, user_id INT, FOREIGN KEY (user_id) REFERENCES users(id))", "Foreign Key"},
            {"CREATE INDEX idx_name ON users (name)", "CREATE INDEX"},
            {"CREATE UNIQUE INDEX idx_email ON users (email)", "UNIQUE INDEX"},
            {"CREATE VIEW v AS SELECT * FROM users", "CREATE VIEW"},
            {"ALTER TABLE users ADD COLUMN email TEXT", "ALTER TABLE ADD"},
            {"ALTER TABLE users DROP COLUMN email", "ALTER TABLE DROP"},
            {"ALTER TABLE users RENAME TO customers", "ALTER TABLE RENAME"},
            {"DROP TABLE users", "DROP TABLE"},
            {"DROP INDEX idx_name", "DROP INDEX"}
        });
        
        // DML Statements
        add_category("DML", {
            {"INSERT INTO t VALUES (1, 2, 3)", "INSERT VALUES"},
            {"INSERT INTO t (a, b) VALUES (1, 2)", "INSERT with columns"},
            {"INSERT INTO t SELECT * FROM t2", "INSERT SELECT"},
            {"INSERT INTO t VALUES (1) ON CONFLICT DO NOTHING", "INSERT ON CONFLICT"},
            {"UPDATE t SET col = 1", "UPDATE"},
            {"UPDATE t SET col = 1 WHERE id = 2", "UPDATE WHERE"},
            {"UPDATE t SET col = 1 RETURNING *", "UPDATE RETURNING"},
            {"DELETE FROM t", "DELETE all"},
            {"DELETE FROM t WHERE id = 1", "DELETE WHERE"},
            {"DELETE FROM t USING t2 WHERE t.id = t2.id", "DELETE USING"}
        });
        
        // Transaction Control
        add_category("Transactions", {
            {"BEGIN", "BEGIN"},
            {"BEGIN TRANSACTION", "BEGIN TRANSACTION"},
            {"COMMIT", "COMMIT"},
            {"ROLLBACK", "ROLLBACK"},
            {"SAVEPOINT sp1", "SAVEPOINT"},
            {"RELEASE SAVEPOINT sp1", "RELEASE SAVEPOINT"},
            {"ROLLBACK TO SAVEPOINT sp1", "ROLLBACK TO"}
        });
        
        // Advanced Types (Current State)
        add_category("Advanced Types - DDL", {
            {"CREATE TABLE t (data JSON)", "JSON type"},
            {"CREATE TABLE t (data JSONB)", "JSONB type"},
            {"CREATE TABLE t (tags TEXT[])", "Array type"},
            {"CREATE TABLE t (ids INTEGER[])", "Integer array"},
            {"CREATE TABLE t (matrix INTEGER[][])", "Multi-dim array"},
            {"CREATE TABLE t (duration INTERVAL)", "INTERVAL type"},
            {"CREATE TABLE t (durations INTERVAL[])", "INTERVAL array"}
        });
        
        // Advanced Types in Expressions
        add_category("Advanced Types - Expressions", {
            {"SELECT INTERVAL '1 day'", "INTERVAL literal"},
            {"SELECT data->'key' FROM t", "JSON -> operator"},
            {"SELECT data->>'key' FROM t", "JSON ->> operator"},
            {"SELECT ARRAY[1,2,3]", "ARRAY constructor"},
            {"SELECT ROW(1,2,3)", "ROW constructor"},
            {"SELECT CAST('1 day' AS INTERVAL)", "CAST to INTERVAL"},
            {"SELECT * FROM t WHERE id = ANY(ARRAY[1,2,3])", "ANY with ARRAY"}
        });
        
        // OLAP Features
        add_category("OLAP", {
            {"SELECT a, SUM(b) FROM t GROUP BY GROUPING SETS ((a), ())", "GROUPING SETS"},
            {"SELECT a, b, SUM(c) FROM t GROUP BY CUBE(a, b)", "CUBE"},
            {"SELECT a, b, SUM(c) FROM t GROUP BY ROLLUP(a, b)", "ROLLUP"},
            {"SELECT RANK() OVER (ORDER BY score DESC) FROM t", "RANK"},
            {"SELECT DENSE_RANK() OVER (PARTITION BY dept ORDER BY salary) FROM t", "DENSE_RANK"},
            {"SELECT LEAD(price, 1) OVER (ORDER BY date) FROM t", "LEAD"},
            {"SELECT LAG(price, 1) OVER (ORDER BY date) FROM t", "LAG"}
        });
        
        // Set Operations
        add_category("Set Operations", {
            {"SELECT 1 UNION SELECT 2", "UNION"},
            {"SELECT 1 UNION ALL SELECT 2", "UNION ALL"},
            {"SELECT 1 INTERSECT SELECT 2", "INTERSECT"},
            {"SELECT 1 EXCEPT SELECT 2", "EXCEPT"},
            {"(SELECT * FROM t1) UNION (SELECT * FROM t2) ORDER BY 1", "UNION with ORDER BY"}
        });
        
        // Utility Statements
        add_category("Utility", {
            {"EXPLAIN SELECT * FROM t", "EXPLAIN"},
            {"VACUUM", "VACUUM"},
            {"VACUUM FULL", "VACUUM FULL"},
            {"ANALYZE t", "ANALYZE"},
            {"REINDEX t", "REINDEX"},
            {"TRUNCATE TABLE t", "TRUNCATE"},
            {"VALUES (1,2), (3,4)", "VALUES"},
            {"SET search_path = public", "SET"},
            {"PRAGMA table_info(users)", "PRAGMA"}
        });
    }
    
    void run_tests() {
        std::cout << "\n=== DB25 PARSER COMPREHENSIVE VALIDATION ===" << std::endl;
        std::cout << std::setfill('=') << std::setw(50) << "" << std::endl;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (auto& category : categories) {
            std::cout << "\n📋 " << category.name << ":" << std::endl;
            
            for (const auto& [sql, desc] : category.tests) {
                parser.reset();
                auto result = parser.parse(sql);
                bool passed = result.has_value();
                
                if (passed) {
                    category.passed++;
                    std::cout << "  ✅ " << std::left << std::setw(40) << desc;
                    
                    // Additional validation for specific features
                    auto* ast = result.value();
                    if (desc.find("INTERVAL") != std::string::npos) {
                        if (contains_text(ast, "INTERVAL")) {
                            std::cout << " [INTERVAL recognized]";
                        }
                    } else if (desc.find("JSON") != std::string::npos) {
                        if (contains_text(ast, "JSON")) {
                            std::cout << " [JSON recognized]";
                        }
                    } else if (desc.find("ARRAY") != std::string::npos) {
                        if (contains_text(ast, "ARRAY") || contains_text(ast, "[]")) {
                            std::cout << " [Array recognized]";
                        }
                    }
                } else {
                    std::cout << "  ❌ " << std::left << std::setw(40) << desc;
                }
                std::cout << std::endl;
            }
            
            // Category summary
            double pct = (category.passed * 100.0 / category.total);
            std::cout << "  Category Score: " << category.passed << "/" << category.total 
                     << " (" << std::fixed << std::setprecision(1) << pct << "%)" << std::endl;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        // Overall summary
        int total_passed = 0, total_tests = 0;
        for (const auto& cat : categories) {
            total_passed += cat.passed;
            total_tests += cat.total;
        }
        
        std::cout << "\n" << std::setfill('=') << std::setw(50) << "" << std::endl;
        std::cout << "OVERALL VALIDATION SUMMARY" << std::endl;
        std::cout << std::setfill('=') << std::setw(50) << "" << std::endl;
        
        std::cout << "\n📊 Results by Category:" << std::endl;
        for (const auto& cat : categories) {
            double pct = (cat.passed * 100.0 / cat.total);
            std::cout << std::left << std::setw(25) << cat.name 
                     << std::right << std::setw(5) << cat.passed 
                     << "/" << std::setw(3) << cat.total
                     << " (" << std::setw(5) << std::fixed << std::setprecision(1) << pct << "%)"
                     << std::endl;
        }
        
        std::cout << "\n📈 Overall Score: " << total_passed << "/" << total_tests 
                 << " (" << (total_passed * 100 / total_tests) << "%)" << std::endl;
        std::cout << "⏱️  Test Duration: " << duration.count() << "ms" << std::endl;
        
        // Assessment
        std::cout << "\n🎯 Assessment:" << std::endl;
        if (total_passed >= total_tests * 0.95) {
            std::cout << "  ✅ EXCELLENT - Parser is production-ready!" << std::endl;
        } else if (total_passed >= total_tests * 0.85) {
            std::cout << "  ✅ GOOD - Parser handles most SQL features well" << std::endl;
        } else if (total_passed >= total_tests * 0.75) {
            std::cout << "  ⚠️  ACCEPTABLE - Parser needs some improvements" << std::endl;
        } else {
            std::cout << "  ❌ NEEDS WORK - Significant gaps in SQL support" << std::endl;
        }
        
        // Specific assessments
        for (const auto& cat : categories) {
            if (cat.name == "Advanced Types - Expressions" && 
                cat.passed < cat.total * 0.7) {
                std::cout << "  ⚠️  Advanced type support in expressions is limited" << std::endl;
            }
            if (cat.name == "Core SQL" && cat.passed < cat.total) {
                std::cout << "  ❌ Core SQL has gaps - critical!" << std::endl;
            }
        }
    }
};

int main() {
    ParserValidator validator;
    validator.setup_tests();
    validator.run_tests();
    
    return 0;
}