/*
 * DB25 Parser - SQL Completion Test
 * Tests parser against comprehensive SQL test suite and reports completion by category
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <iomanip>
#include <sstream>
#include "db25/parser/parser.hpp"

using namespace db25::parser;

struct TestCategory {
    std::string name;
    int total = 0;
    int passed = 0;
    std::vector<std::string> failed_queries;
};

class SQLCompletionTester {
private:
    std::map<std::string, TestCategory> categories;
    std::string current_category;
    int total_queries = 0;
    int total_passed = 0;
    
public:
    void run(const std::string& test_file) {
        std::ifstream file(test_file);
        if (!file.is_open()) {
            std::cerr << "Failed to open test file: " << test_file << std::endl;
            return;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            processLine(line);
        }
        
        printReport();
    }
    
private:
    void processLine(const std::string& line) {
        // Skip empty lines
        if (line.empty()) return;
        
        // Check for category headers
        if (line.find("-- ============") == 0) {
            // Next line should have the category name
            return;
        }
        
        // Check for category names
        if (line.find("-- SELECT FUNDAMENTALS") != std::string::npos) {
            current_category = "SELECT FUNDAMENTALS";
        } else if (line.find("-- SELECT WITH CLAUSES") != std::string::npos) {
            current_category = "SELECT WITH CLAUSES";
        } else if (line.find("-- JOINS") != std::string::npos) {
            current_category = "JOINS";
        } else if (line.find("-- AGGREGATE FUNCTIONS") != std::string::npos) {
            current_category = "AGGREGATE FUNCTIONS";
        } else if (line.find("-- SUBQUERIES") != std::string::npos) {
            current_category = "SUBQUERIES";
        } else if (line.find("-- SET OPERATIONS") != std::string::npos) {
            current_category = "SET OPERATIONS";
        } else if (line.find("-- COMMON TABLE EXPRESSIONS") != std::string::npos) {
            current_category = "CTEs";
        } else if (line.find("-- CASE EXPRESSIONS") != std::string::npos) {
            current_category = "CASE EXPRESSIONS";
        } else if (line.find("-- SELECT ADVANCED") != std::string::npos) {
            current_category = "SELECT ADVANCED";
        } else if (line.find("-- SELECT ULTRA COMPLEX") != std::string::npos) {
            current_category = "SELECT ULTRA COMPLEX";
        } else if (line.find("-- SELECT EDGE CASES") != std::string::npos) {
            current_category = "SELECT EDGE CASES";
        } else if (line.find("-- DDL - CREATE TABLE") != std::string::npos) {
            current_category = "DDL - CREATE TABLE";
        } else if (line.find("-- DML - INSERT") != std::string::npos) {
            current_category = "DML - INSERT";
        } else if (line.find("-- DML - UPDATE") != std::string::npos) {
            current_category = "DML - UPDATE";
        } else if (line.find("-- DML - DELETE") != std::string::npos) {
            current_category = "DML - DELETE";
        }
        
        // Skip comments and section headers
        if (line[0] == '-' && line[1] == '-') return;
        
        // Test the SQL query
        if (!current_category.empty()) {
            testQuery(line);
        }
    }
    
    void testQuery(const std::string& sql) {
        // Skip if it's just whitespace
        if (sql.find_first_not_of(" \t\r\n") == std::string::npos) return;
        
        Parser parser;
        auto result = parser.parse(sql);
        
        total_queries++;
        categories[current_category].total++;
        
        if (result.has_value()) {
            total_passed++;
            categories[current_category].passed++;
        } else {
            // Store failed query (truncate if too long)
            std::string failed = sql;
            if (failed.length() > 80) {
                failed = failed.substr(0, 77) + "...";
            }
            categories[current_category].failed_queries.push_back(failed);
        }
    }
    
    void printReport() {
        std::cout << "\n";
        std::cout << "================================================================================\n";
        std::cout << "                    DB25 SQL Parser - Completion Report                        \n";
        std::cout << "================================================================================\n\n";
        
        // Overall summary
        double overall_percentage = (total_queries > 0) ? 
            (static_cast<double>(total_passed) / total_queries * 100) : 0;
        
        std::cout << "OVERALL: " << total_passed << "/" << total_queries 
                  << " (" << std::fixed << std::setprecision(1) 
                  << overall_percentage << "%)\n";
        std::cout << std::string(80, '-') << "\n\n";
        
        // Category breakdown
        std::cout << std::left << std::setw(30) << "Category" 
                  << std::setw(15) << "Passed/Total"
                  << std::setw(10) << "Percentage"
                  << "Status\n";
        std::cout << std::string(80, '-') << "\n";
        
        for (const auto& [name, cat] : categories) {
            if (cat.total == 0) continue;
            
            double percentage = (static_cast<double>(cat.passed) / cat.total * 100);
            
            std::cout << std::left << std::setw(30) << name
                      << std::setw(15) << (std::to_string(cat.passed) + "/" + std::to_string(cat.total))
                      << std::setw(10) << (std::to_string(static_cast<int>(percentage)) + "%");
            
            // Status indicator
            if (percentage == 100) {
                std::cout << "✓ COMPLETE";
            } else if (percentage >= 90) {
                std::cout << "◐ NEARLY COMPLETE";
            } else if (percentage >= 70) {
                std::cout << "◔ GOOD PROGRESS";
            } else if (percentage >= 50) {
                std::cout << "◑ PARTIAL";
            } else if (percentage >= 25) {
                std::cout << "◕ LIMITED";
            } else {
                std::cout << "○ MINIMAL";
            }
            
            std::cout << "\n";
        }
        
        std::cout << std::string(80, '-') << "\n\n";
        
        // Show failed queries for categories with < 100% completion
        std::cout << "FAILED QUERIES BY CATEGORY:\n";
        std::cout << std::string(80, '-') << "\n";
        
        for (const auto& [name, cat] : categories) {
            if (cat.failed_queries.empty()) continue;
            
            std::cout << "\n" << name << " (" << cat.failed_queries.size() << " failed):\n";
            for (size_t i = 0; i < std::min(size_t(5), cat.failed_queries.size()); ++i) {
                std::cout << "  • " << cat.failed_queries[i] << "\n";
            }
            if (cat.failed_queries.size() > 5) {
                std::cout << "  ... and " << (cat.failed_queries.size() - 5) << " more\n";
            }
        }
        
        std::cout << "\n";
        std::cout << "================================================================================\n";
        
        // Completion level assessment
        std::cout << "\nCOMPLETION LEVEL ASSESSMENT:\n";
        std::cout << std::string(80, '-') << "\n";
        
        if (overall_percentage >= 95) {
            std::cout << "★★★★★ PRODUCTION READY - Parser is highly complete\n";
        } else if (overall_percentage >= 85) {
            std::cout << "★★★★☆ NEARLY COMPLETE - Most SQL features supported\n";
        } else if (overall_percentage >= 70) {
            std::cout << "★★★☆☆ GOOD - Core features work, some advanced features missing\n";
        } else if (overall_percentage >= 50) {
            std::cout << "★★☆☆☆ MODERATE - Basic SQL works, many features incomplete\n";
        } else if (overall_percentage >= 30) {
            std::cout << "★☆☆☆☆ LIMITED - Only basic queries supported\n";
        } else {
            std::cout << "☆☆☆☆☆ MINIMAL - Parser needs significant work\n";
        }
        
        std::cout << "\n";
    }
};

int main(int argc, char** argv) {
    std::string test_file = "tests/comprehensive_sql_tests.sql";
    
    if (argc > 1) {
        test_file = argv[1];
    }
    
    SQLCompletionTester tester;
    tester.run(test_file);
    
    return 0;
}