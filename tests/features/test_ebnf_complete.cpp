/*
 * DB25 Parser - EBNF Complete Test
 * Tests parser against comprehensive SQL covering entire grammar
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <set>
#include <map>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

// Colors for output
const char* GREEN = "\033[32m";
const char* RED = "\033[31m";
const char* YELLOW = "\033[33m";
const char* BLUE = "\033[34m";
const char* CYAN = "\033[36m";
const char* RESET = "\033[0m";
const char* BOLD = "\033[1m";

// Track node types encountered
std::set<NodeType> encountered_node_types;
std::map<NodeType, int> node_type_counts;
std::map<std::string, int> keyword_usage;

// Recursively analyze AST
void analyze_ast(const ASTNode* node, int depth = 0) {
    if (!node) return;
    
    // Track node types
    encountered_node_types.insert(node->node_type);
    node_type_counts[node->node_type]++;
    
    // Track keywords in expressions
    if (!node->primary_text.empty() && depth > 0) {
        std::string text(node->primary_text);
        // Convert to uppercase for consistency
        for (auto& c : text) c = std::toupper(c);
        if (text.size() > 2) { // Skip short operators
            keyword_usage[text]++;
        }
    }
    
    // Recurse through children
    ASTNode* child = node->first_child;
    while (child) {
        analyze_ast(child, depth + 1);
        child = child->next_sibling;
    }
}

// Split SQL file into individual statements
std::vector<std::string> split_sql_statements(const std::string& sql) {
    std::vector<std::string> statements;
    std::string current;
    bool in_string = false;
    char string_char = '\0';
    
    for (size_t i = 0; i < sql.size(); ++i) {
        char c = sql[i];
        
        // Handle string literals
        if (!in_string && (c == '\'' || c == '"')) {
            in_string = true;
            string_char = c;
            current += c;
        } else if (in_string && c == string_char) {
            // Check for escaped quote
            if (i + 1 < sql.size() && sql[i + 1] == string_char) {
                current += c;
                current += sql[++i];
            } else {
                in_string = false;
                current += c;
            }
        } else if (!in_string && c == ';') {
            current += c;
            // Skip whitespace
            while (!current.empty() && std::isspace(current.front())) {
                current.erase(0, 1);
            }
            while (!current.empty() && std::isspace(current.back())) {
                current.pop_back();
            }
            if (!current.empty() && current != ";") {
                statements.push_back(current);
            }
            current.clear();
        } else {
            current += c;
        }
    }
    
    // Add last statement if no trailing semicolon
    while (!current.empty() && std::isspace(current.front())) {
        current.erase(0, 1);
    }
    while (!current.empty() && std::isspace(current.back())) {
        current.pop_back();
    }
    if (!current.empty()) {
        statements.push_back(current);
    }
    
    return statements;
}

int main(int argc, char* argv[]) {
    std::string filename = "ebnf_complete.sql";
    if (argc > 1) {
        filename = argv[1];
    }
    
    // Read SQL file
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << RED << "Error: Could not open file " << filename << RESET << std::endl;
        return 1;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string sql_content = buffer.str();
    file.close();
    
    std::cout << BOLD << "========================================" << RESET << std::endl;
    std::cout << BOLD << "DB25 Parser - EBNF Complete Test" << RESET << std::endl;
    std::cout << BOLD << "========================================" << RESET << std::endl;
    std::cout << "Testing file: " << CYAN << filename << RESET << std::endl;
    std::cout << "File size: " << CYAN << sql_content.size() << " bytes" << RESET << std::endl;
    
    // Split into statements
    auto statements = split_sql_statements(sql_content);
    std::cout << "Statements found: " << CYAN << statements.size() << RESET << std::endl;
    std::cout << std::endl;
    
    Parser parser;
    int passed = 0;
    int failed = 0;
    std::vector<std::string> failed_statements;
    
    auto total_start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < statements.size(); ++i) {
        const auto& stmt = statements[i];
        
        // Get first 50 chars for display
        std::string preview = stmt.substr(0, 50);
        if (stmt.size() > 50) preview += "...";
        // Remove newlines for display
        for (auto& c : preview) {
            if (c == '\n' || c == '\r') c = ' ';
        }
        
        std::cout << "Statement " << (i + 1) << ": " << preview << std::endl;
        
        auto start = std::chrono::high_resolution_clock::now();
        auto result = parser.parse(stmt);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        if (result.has_value()) {
            std::cout << GREEN << "  ✓ PASSED" << RESET;
            std::cout << " (" << duration.count() << " μs)";
            
            ASTNode* root = result.value();
            analyze_ast(root);
            
            std::cout << " - Root: " << static_cast<int>(root->node_type);
            std::cout << ", Nodes: " << root->child_count << std::endl;
            
            passed++;
        } else {
            std::cout << RED << "  ✗ FAILED" << RESET;
            const auto& error = result.error();
            std::cout << " - Line " << error.line << ", Col " << error.column;
            std::cout << ": " << error.message << std::endl;
            
            failed_statements.push_back(preview);
            failed++;
        }
    }
    
    auto total_end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start);
    
    std::cout << std::endl;
    std::cout << BOLD << "========================================" << RESET << std::endl;
    std::cout << BOLD << "Test Results Summary" << RESET << std::endl;
    std::cout << BOLD << "========================================" << RESET << std::endl;
    
    std::cout << "Statements passed: " << GREEN << passed << RESET << std::endl;
    std::cout << "Statements failed: " << (failed > 0 ? RED : GREEN) << failed << RESET << std::endl;
    std::cout << "Success rate: " << (failed == 0 ? GREEN : YELLOW) 
              << (passed * 100.0 / (passed + failed)) << "%" << RESET << std::endl;
    std::cout << "Total parse time: " << CYAN << total_duration.count() << " ms" << RESET << std::endl;
    std::cout << std::endl;
    
    // Node type coverage
    std::cout << BOLD << "Node Type Coverage (" << encountered_node_types.size() << " types)" << RESET << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    
    // Sort by count
    std::vector<std::pair<NodeType, int>> sorted_types(node_type_counts.begin(), node_type_counts.end());
    std::sort(sorted_types.begin(), sorted_types.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    int shown = 0;
    for (const auto& [type, count] : sorted_types) {
        if (shown++ < 20) { // Show top 20
            std::cout << "  " << std::setw(20) << static_cast<int>(type) 
                      << ": " << CYAN << count << RESET << " occurrences" << std::endl;
        }
    }
    if (shown < sorted_types.size()) {
        std::cout << "  ... and " << (sorted_types.size() - shown) << " more types" << std::endl;
    }
    std::cout << std::endl;
    
    // Keyword usage
    std::cout << BOLD << "Top Keywords/Functions Used" << RESET << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    
    std::vector<std::pair<std::string, int>> sorted_keywords(keyword_usage.begin(), keyword_usage.end());
    std::sort(sorted_keywords.begin(), sorted_keywords.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    shown = 0;
    for (const auto& [keyword, count] : sorted_keywords) {
        if (shown++ < 15) { // Show top 15
            std::cout << "  " << std::setw(20) << keyword 
                      << ": " << CYAN << count << RESET << " times" << std::endl;
        }
    }
    std::cout << std::endl;
    
    // Failed statements
    if (!failed_statements.empty()) {
        std::cout << BOLD << RED << "Failed Statements:" << RESET << std::endl;
        std::cout << "----------------------------------------" << std::endl;
        for (const auto& stmt : failed_statements) {
            std::cout << "  • " << stmt << std::endl;
        }
        std::cout << std::endl;
    }
    
    // Final verdict
    if (failed == 0) {
        std::cout << GREEN << BOLD << "✓ ALL TESTS PASSED! Parser successfully handles comprehensive EBNF grammar." << RESET << std::endl;
        return 0;
    } else {
        std::cout << YELLOW << BOLD << "⚠ Some statements failed. Parser covers " 
                  << (passed * 100.0 / (passed + failed)) 
                  << "% of EBNF grammar tests." << RESET << std::endl;
        return 1;
    }
}