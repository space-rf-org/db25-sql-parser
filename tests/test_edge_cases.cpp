/*
 * DB25 Parser - Edge Cases and Stress Tests
 */

#include <iostream>
#include <string>
#include <vector>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

struct EdgeCase {
    const char* description;
    const char* sql;
    bool should_pass;
};

std::vector<EdgeCase> edge_cases = {
    // Valid edge cases
    {"Empty SELECT list", "SELECT FROM users", false},
    {"Multiple wildcards", "SELECT *, * FROM users", true},
    {"Nested parentheses", "SELECT ((1 + 2) * 3) FROM dual", true},
    {"Deep nesting", "SELECT * FROM (SELECT * FROM (SELECT * FROM (SELECT * FROM users)))", true},
    {"Multiple JOINs", 
     "SELECT * FROM t1 JOIN t2 ON t1.id = t2.id JOIN t3 ON t2.id = t3.id JOIN t4 ON t3.id = t4.id", true},
    {"Complex WHERE", 
     "SELECT * FROM users WHERE (age > 18 AND age < 65) OR (status = 'special' AND NOT deleted)", true},
    {"Multiple GROUP BY", 
     "SELECT dept, team, COUNT(*) FROM employees GROUP BY dept, team", true},
    {"Chained set operations",
     "SELECT id FROM t1 UNION SELECT id FROM t2 INTERSECT SELECT id FROM t3", true},
    {"Mixed case keywords",
     "SeLeCt * FrOm UsErS wHeRe AcTiVe = TrUe", true},
    {"Quoted identifiers",
     "SELECT \"user name\", 'string literal' FROM users", true},
    {"Numeric expressions",
     "SELECT 1 + 2 * 3 - 4 / 2, 10 % 3 FROM dual", true},
    {"Boolean expressions",
     "SELECT * FROM users WHERE active = true AND verified = false", true},
    {"NULL handling",
     "SELECT * FROM users WHERE email IS NULL OR email IS NOT NULL", true},
    {"Complex CASE",
     "SELECT CASE WHEN a > b THEN c WHEN d < e THEN f ELSE g END FROM table", true},
    {"Nested functions",
     "SELECT UPPER(TRIM(LOWER(name))) FROM users", true},
    {"Subquery in SELECT",
     "SELECT id, (SELECT MAX(price) FROM products) as max_price FROM categories", true},
    {"Correlated subquery",
     "SELECT * FROM t1 WHERE EXISTS (SELECT 1 FROM t2 WHERE t2.id = t1.id)", true},
    {"ALL/ANY operators",
     "SELECT * FROM products WHERE price > ALL (SELECT price FROM discounted)", true},
    
    // Should fail
    {"Missing FROM", "SELECT * WHERE id = 1", false},
    {"Invalid JOIN", "SELECT * FROM t1 JOIN ON t1.id = t2.id", false},
    {"Unclosed parenthesis", "SELECT * FROM (SELECT * FROM users", false},
    {"Invalid operator", "SELECT * FROM users WHERE id === 1", false},
};

void print_summary(int passed, int failed, int false_positives, int false_negatives) {
    std::cout << "\n================================================================================\n";
    std::cout << "                                EDGE CASES SUMMARY                             \n";
    std::cout << "================================================================================\n";
    std::cout << "Total Tests: " << (passed + failed) << "\n";
    std::cout << "Correctly Handled: " << passed << " (" 
              << (passed * 100.0 / (passed + failed)) << "%)\n";
    std::cout << "Incorrectly Handled: " << failed << "\n";
    if (false_positives > 0) {
        std::cout << "  - False Positives (should fail but passed): " << false_positives << "\n";
    }
    if (false_negatives > 0) {
        std::cout << "  - False Negatives (should pass but failed): " << false_negatives << "\n";
    }
}

int main() {
    std::cout << "================================================================================\n";
    std::cout << "                    DB25 SQL Parser - Edge Cases Test                          \n";
    std::cout << "================================================================================\n\n";
    
    Parser parser;
    int passed = 0;
    int failed = 0;
    int false_positives = 0;
    int false_negatives = 0;
    
    for (const auto& test : edge_cases) {
        std::cout << "Test: " << test.description << "\n";
        std::cout << "SQL: " << test.sql << "\n";
        std::cout << "Expected: " << (test.should_pass ? "PASS" : "FAIL") << "\n";
        
        parser.reset();
        auto result = parser.parse(test.sql);
        bool did_pass = result.has_value();
        
        std::cout << "Result: " << (did_pass ? "PASSED" : "FAILED");
        
        if (did_pass == test.should_pass) {
            std::cout << " ✓ (Correct)\n";
            passed++;
        } else {
            std::cout << " ✗ (Incorrect)\n";
            failed++;
            if (did_pass && !test.should_pass) {
                false_positives++;
                std::cout << "  ERROR: False positive - should have failed\n";
            } else if (!did_pass && test.should_pass) {
                false_negatives++;
                std::cout << "  ERROR: False negative - should have passed\n";
                std::cout << "  Parse error: " << result.error().message << "\n";
            }
        }
        
        std::cout << std::string(80, '-') << "\n";
    }
    
    print_summary(passed, failed, false_positives, false_negatives);
    
    // Additional stress test
    std::cout << "\n================================================================================\n";
    std::cout << "                               STRESS TEST                                     \n";
    std::cout << "================================================================================\n";
    
    // Generate a very long SELECT list
    std::string long_select = "SELECT ";
    for (int i = 0; i < 100; i++) {
        if (i > 0) long_select += ", ";
        long_select += "col" + std::to_string(i);
    }
    long_select += " FROM table";
    
    std::cout << "Testing with 100 columns in SELECT list...\n";
    parser.reset();
    auto result = parser.parse(long_select);
    if (result.has_value()) {
        std::cout << "✓ Successfully parsed query with 100 columns\n";
        std::cout << "  Memory used: " << parser.get_memory_used() << " bytes\n";
        std::cout << "  Nodes created: " << parser.get_node_count() << "\n";
    } else {
        std::cout << "✗ Failed to parse long query\n";
    }
    
    return failed > 0 ? 1 : 0;
}