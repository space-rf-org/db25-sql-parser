#include "db25/parser/parser.hpp"
#include <iostream>
#include <cassert>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

void print_ast_tree(const ASTNode* node, int depth = 0) {
    if (!node) return;
    
    for (int i = 0; i < depth; i++) std::cout << "  ";
    std::cout << "- " << static_cast<int>(node->node_type) 
              << " (" << node->primary_text << ")"
              << " [children: " << node->child_count << "]" << std::endl;
    
    // Print children
    auto* child = node->first_child;
    while (child) {
        print_ast_tree(child, depth + 1);
        child = child->next_sibling;
    }
}

void test_ddl_ast_construction() {
    std::cout << "\n=== Testing DDL AST Construction ===" << std::endl;
    
    Parser parser;
    
    // Test CREATE TABLE AST
    auto result = parser.parse(R"(
        CREATE TABLE users (
            id INTEGER PRIMARY KEY,
            name VARCHAR(100) NOT NULL,
            data JSON
        )
    )");
    
    if (result.has_value()) {
        std::cout << "\nCREATE TABLE AST Structure:" << std::endl;
        print_ast_tree(result.value());
        
        // Check if we have actual column nodes
        auto* ast = result.value();
        if (ast->first_child && ast->first_child->node_type == NodeType::ColumnDefinition) {
            std::cout << "✅ DDL builds proper AST with column definitions" << std::endl;
        } else {
            std::cout << "❌ DDL still just consuming tokens" << std::endl;
        }
    }
}

void test_advanced_types() {
    std::cout << "\n=== Testing Advanced Type Support ===" << std::endl;
    
    Parser parser;
    
    // Test INTERVAL
    auto result = parser.parse("SELECT INTERVAL '1 day'");
    std::cout << "INTERVAL literal: " << (result.has_value() ? "✅ Parsed" : "❌ Failed") << std::endl;
    
    // Test ARRAY
    result = parser.parse("SELECT ARRAY[1, 2, 3]");
    std::cout << "ARRAY constructor: " << (result.has_value() ? "✅ Parsed" : "❌ Failed") << std::endl;
    
    // Test JSON in CREATE TABLE
    result = parser.parse("CREATE TABLE test (data JSON)");
    if (result.has_value()) {
        auto* ast = result.value();
        if (ast->first_child) {
            auto* col = ast->first_child;
            if (col->first_child) { // data type node
                std::cout << "JSON type in DDL: ✅ Recognized (as " << col->first_child->primary_text << ")" << std::endl;
            }
        }
    } else {
        std::cout << "JSON type: ❌ Failed" << std::endl;
    }
    
    // Test array type
    result = parser.parse("CREATE TABLE test (ids INTEGER[])");
    std::cout << "Array type (INTEGER[]): " << (result.has_value() ? "✅ Parsed" : "❌ Failed") << std::endl;
}

void test_semantic_validation() {
    std::cout << "\n=== Testing Semantic Validation ===" << std::endl;
    
    Parser parser;
    
    // Test if parser validates column references
    auto result = parser.parse("SELECT unknown_column FROM users");
    std::cout << "Unknown column reference: " << (result.has_value() ? "Parsed (no validation)" : "Failed") << std::endl;
    
    // Test type checking
    result = parser.parse("SELECT 'string' + 123");  
    std::cout << "Type mismatch: " << (result.has_value() ? "Parsed (no type checking)" : "Failed") << std::endl;
    
    // Check if parser has validate methods
    if (result.has_value()) {
        // The validate_ast method exists but let's see what it does
        std::cout << "validate_ast method exists in parser" << std::endl;
    }
}

void test_intersect_except() {
    std::cout << "\n=== Testing INTERSECT/EXCEPT ===" << std::endl;
    
    Parser parser;
    
    auto result = parser.parse("SELECT id FROM t1 INTERSECT SELECT id FROM t2");
    std::cout << "INTERSECT: " << (result.has_value() ? "✅ Parsed" : "❌ Failed") << std::endl;
    
    result = parser.parse("SELECT id FROM t1 EXCEPT SELECT id FROM t2");
    std::cout << "EXCEPT: " << (result.has_value() ? "✅ Parsed" : "❌ Failed") << std::endl;
    
    result = parser.parse("SELECT id FROM t1 UNION SELECT id FROM t2");
    std::cout << "UNION (for comparison): " << (result.has_value() ? "✅ Parsed" : "❌ Failed") << std::endl;
}

int main() {
    std::cout << "=== Verifying Current Parser State ===" << std::endl;
    
    test_ddl_ast_construction();
    test_advanced_types();
    test_semantic_validation();
    test_intersect_except();
    
    std::cout << "\n=== Summary ===" << std::endl;
    return 0;
}
