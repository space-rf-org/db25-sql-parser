#include "db25/parser/parser.hpp"
#include <iostream>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

void safe_print_ast(const ASTNode* node, int depth = 0, int max_depth = 10) {
    if (!node || depth > max_depth) return;
    
    for (int i = 0; i < depth; i++) std::cout << "  ";
    
    // Print node info safely
    std::cout << "- Type:" << static_cast<int>(node->node_type);
    
    // Only print primary_text if it's not empty
    if (!node->primary_text.empty()) {
        std::cout << " Text:'" << node->primary_text << "'";
    }
    
    std::cout << " Children:" << node->child_count << std::endl;
    
    // Print first child only for now
    if (node->first_child && depth < max_depth) {
        safe_print_ast(node->first_child, depth + 1, max_depth);
    }
    
    // Print next sibling
    if (node->next_sibling && depth > 0) {
        safe_print_ast(node->next_sibling, depth, max_depth);
    }
}

int main() {
    Parser parser;
    
    std::cout << "Testing CREATE TABLE AST:" << std::endl;
    auto result = parser.parse(R"(
        CREATE TABLE users (
            id INTEGER PRIMARY KEY,
            name VARCHAR(100) NOT NULL
        )
    )");
    
    if (result.has_value()) {
        std::cout << "Parse successful, printing AST:" << std::endl;
        safe_print_ast(result.value());
    } else {
        std::cout << "Parse failed" << std::endl;
    }
    
    return 0;
}
