#define DEBUG_TOKEN_FLOW 1

#include <iostream>
#include <string>
#include <cstdio>
#include "db25/parser/parser.hpp"

void print_ast(db25::ast::ASTNode* node, int depth = 0) {
    if (!node) return;
    
    for (int i = 0; i < depth; ++i) std::cout << "  ";
    std::cout << "Node: " << static_cast<int>(node->node_type);
    if (!node->primary_text.empty()) {
        std::cout << " [" << node->primary_text << "]";
    }
    std::cout << std::endl;
    
    // Traverse children using the linked structure
    db25::ast::ASTNode* child = node->first_child;
    while (child) {
        print_ast(child, depth + 1);
        child = child->next_sibling;
    }
}

int main() {
    // Test the problematic query #5
    std::string query = "SELECT u.name FROM users u";
    
    std::cout << "Testing query: " << query << std::endl;
    std::cout << "=====================================\n" << std::endl;
    
    db25::parser::ParserConfig config;
    config.mode = db25::ast::ParserMode::Debug;
    db25::parser::Parser parser(config);
    
    auto result = parser.parse(query);
    
    if (!result) {
        std::cerr << "Parse error: " << result.error().message << std::endl;
        return 1;
    }
    
    std::cout << "\n=====================================\n";
    std::cout << "Final AST:\n";
    print_ast(result.value());
    
    return 0;
}