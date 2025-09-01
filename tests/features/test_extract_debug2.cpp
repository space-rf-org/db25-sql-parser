#include <iostream>
#include <string>
#include "db25/parser/parser.hpp"

using namespace db25;
using namespace db25::parser;

int main() {
    Parser parser;
    
    // Test very simple EXTRACT
    std::string sql1 = "SELECT EXTRACT(YEAR FROM d)";
    std::cout << "Testing: " << sql1 << std::endl;
    
    auto result1 = parser.parse(sql1);
    if (result1.has_value()) {
        std::cout << "✓ Parse successful!" << std::endl;
    } else {
        const auto& error = result1.error();
        std::cout << "✗ Parse failed at line " << error.line 
                  << ", column " << error.column 
                  << ": " << error.message << std::endl;
    }
    
    // Test EXTRACT with FROM clause
    std::string sql2 = "SELECT EXTRACT(YEAR FROM date_col) FROM t";
    std::cout << "\nTesting: " << sql2 << std::endl;
    
    auto result2 = parser.parse(sql2);
    if (result2.has_value()) {
        std::cout << "✓ Parse successful!" << std::endl;
    } else {
        const auto& error = result2.error();
        std::cout << "✗ Parse failed at line " << error.line 
                  << ", column " << error.column 
                  << ": " << error.message << std::endl;
    }
    
    return 0;
}