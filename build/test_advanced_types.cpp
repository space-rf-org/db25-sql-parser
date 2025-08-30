#include "db25/parser/parser.hpp"
#include <iostream>
#include <vector>
#include <string>

using namespace db25::parser;
using namespace db25::ast;

struct TypeTest {
    std::string name;
    std::string sql;
    std::string expected_behavior;
};

int main() {
    std::cout << "\n=== ADVANCED TYPE SUPPORT ANALYSIS ===\n" << std::endl;
    
    std::vector<TypeTest> tests = {
        // INTERVAL in DDL
        {"INTERVAL column type", "CREATE TABLE t (duration INTERVAL)", "DDL context"},
        {"INTERVAL array type", "CREATE TABLE t (durations INTERVAL[])", "DDL context"},
        
        // INTERVAL literals
        {"INTERVAL literal basic", "SELECT INTERVAL '1 day'", "Expression context"},
        {"INTERVAL in expression", "SELECT date + INTERVAL '1 month'", "Expression context"},
        {"INTERVAL in WHERE", "SELECT * FROM t WHERE created > NOW() - INTERVAL '7 days'", "Expression context"},
        {"INTERVAL in window frame", "SELECT SUM(x) OVER (ORDER BY date RANGE BETWEEN INTERVAL '1' DAY PRECEDING AND CURRENT ROW)", "Window context"},
        
        // ARRAY in DDL
        {"ARRAY column type", "CREATE TABLE t (tags TEXT[])", "DDL context"},
        {"Integer array type", "CREATE TABLE t (ids INTEGER[])", "DDL context"},
        {"Multi-dim array", "CREATE TABLE t (matrix INTEGER[][])", "DDL context"},
        
        // ARRAY constructors
        {"ARRAY constructor", "SELECT ARRAY[1,2,3]", "Expression context"},
        {"ARRAY in WHERE", "SELECT * FROM t WHERE id = ANY(ARRAY[1,2,3])", "Expression context"},
        {"ARRAY subquery", "SELECT ARRAY(SELECT id FROM users)", "Expression context"},
        
        // ROW constructors
        {"ROW constructor", "SELECT ROW(1, 'text', true)", "Expression context"},
        {"ROW comparison", "SELECT * FROM t WHERE (a,b,c) = ROW(1,2,3)", "Expression context"},
        
        // JSON operators
        {"JSON arrow operator", "SELECT data->'key' FROM t", "Expression context"},
        {"JSON double arrow", "SELECT data->>'key' FROM t", "Expression context"},
        {"JSON path", "SELECT data#>'{users,0,name}' FROM t", "Expression context"},
        {"JSON contains", "SELECT * FROM t WHERE data @> '{\"key\": \"value\"}'", "Expression context"},
        
        // Composite/Complex
        {"CAST to INTERVAL", "SELECT CAST('1 day' AS INTERVAL)", "Expression context"},
        {"ARRAY of INTERVAL", "CREATE TABLE t (schedules INTERVAL[])", "DDL context"},
    };
    
    Parser parser;
    int ddl_supported = 0;
    int expr_supported = 0;
    int total = 0;
    
    for (const auto& test : tests) {
        parser.reset();
        auto result = parser.parse(test.sql);
        bool parsed = result.has_value();
        
        total++;
        std::cout << (parsed ? "✅" : "❌") << " " << test.name << std::endl;
        std::cout << "   SQL: " << test.sql << std::endl;
        
        if (parsed) {
            // Check if it's really parsed as the intended type
            auto* ast = result.value();
            
            // Try to find type-specific nodes
            bool has_interval_node = false;
            bool has_array_node = false;
            bool has_row_node = false;
            bool has_json_op = false;
            
            std::function<void(ASTNode*)> check_nodes = [&](ASTNode* node) {
                if (!node) return;
                
                // Check for INTERVAL
                if (node->primary_text.find("INTERVAL") != std::string::npos) {
                    has_interval_node = true;
                }
                
                // Check for array brackets
                if (node->primary_text.find("[]") != std::string::npos ||
                    node->primary_text.find("ARRAY") != std::string::npos) {
                    has_array_node = true;
                }
                
                // Check for ROW
                if (node->primary_text.find("ROW") != std::string::npos) {
                    has_row_node = true;
                }
                
                // Check for JSON operators
                if (node->primary_text == "->" || node->primary_text == "->>" ||
                    node->primary_text == "#>" || node->primary_text == "@>") {
                    has_json_op = true;
                }
                
                for (auto* child = node->first_child; child; child = child->next_sibling) {
                    check_nodes(child);
                }
            };
            
            check_nodes(ast);
            
            if (test.expected_behavior == "DDL context") {
                ddl_supported++;
                std::cout << "   ✓ Parsed in DDL context";
                if (has_interval_node) std::cout << " (INTERVAL recognized)";
                if (has_array_node) std::cout << " (Array notation recognized)";
            } else {
                expr_supported++;
                std::cout << "   ✓ Parsed as expression";
                if (has_interval_node) std::cout << " (INTERVAL found in AST)";
                if (has_array_node) std::cout << " (ARRAY found in AST)";
                if (has_row_node) std::cout << " (ROW found in AST)";
                if (has_json_op) std::cout << " (JSON operator found)";
            }
            std::cout << std::endl;
        } else {
            std::cout << "   ✗ Failed to parse" << std::endl;
        }
        std::cout << std::endl;
    }
    
    std::cout << "=== SUMMARY ===" << std::endl;
    std::cout << "Total tests: " << total << std::endl;
    std::cout << "DDL context supported: " << ddl_supported << std::endl;
    std::cout << "Expression context supported: " << expr_supported << std::endl;
    std::cout << "Overall support: " << (ddl_supported + expr_supported) << "/" << total;
    std::cout << " (" << ((ddl_supported + expr_supported) * 100 / total) << "%)" << std::endl;
    
    return 0;
}
