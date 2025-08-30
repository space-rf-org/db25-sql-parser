#include "db25/parser/parser.hpp"
#include <iostream>
#include <cassert>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

int main() {
    std::cout << "Creating parser..." << std::endl;
    Parser parser;
    
    std::cout << "Parsing simple query..." << std::endl;
    auto result = parser.parse("SELECT 1");
    
    if (result.has_value()) {
        std::cout << "Parse successful" << std::endl;
    } else {
        std::cout << "Parse failed: " << result.error().message << std::endl;
    }
    
    std::cout << "Testing CREATE TABLE..." << std::endl;
    result = parser.parse("CREATE TABLE test (id INTEGER)");
    
    if (result.has_value()) {
        std::cout << "CREATE TABLE parsed" << std::endl;
        auto* ast = result.value();
        std::cout << "Node type: " << static_cast<int>(ast->node_type) << std::endl;
        std::cout << "Child count: " << ast->child_count << std::endl;
        
        if (ast->first_child) {
            std::cout << "First child type: " << static_cast<int>(ast->first_child->node_type) << std::endl;
        }
    } else {
        std::cout << "CREATE TABLE failed: " << result.error().message << std::endl;
    }
    
    return 0;
}
