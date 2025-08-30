#include <iostream>
#include <string>
#include "db25/parser/parser.hpp"

using namespace db25;
using namespace db25::parser;

int main() {
    Parser parser;
    
    // Test EXTRACT function
    std::string sql = "SELECT EXTRACT(YEAR FROM date_col) FROM t";
    std::cout << "Testing: " << sql << std::endl;
    
    auto result = parser.parse(sql);
    if (result.has_value()) {
        std::cout << "✓ Parse successful!" << std::endl;
    } else {
        const auto& error = result.error();
        std::cout << "✗ Parse failed at line " << error.line 
                  << ", column " << error.column 
                  << ": " << error.message << std::endl;
    }
    
    // Test simpler functions
    std::cout << "\nTesting: SELECT CONCAT(a, b), TRIM(name) FROM t" << std::endl;
    auto result2 = parser.parse("SELECT CONCAT(a, b), TRIM(name) FROM t");
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