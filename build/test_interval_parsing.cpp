#include "db25/parser/parser.hpp"
#include <iostream>

using namespace db25::parser;

int main() {
    Parser p;
    std::string sql = "SELECT SUM(value) OVER (ORDER BY date RANGE BETWEEN INTERVAL '1' DAY PRECEDING AND CURRENT ROW) FROM t";
    
    std::cout << "Parsing: " << sql << std::endl;
    auto result = p.parse(sql);
    
    if (result.has_value()) {
        std::cout << "SUCCESS: Query parsed" << std::endl;
    } else {
        std::cout << "FAILED: Could not parse query" << std::endl;
        
        // Try simpler version
        Parser p2;
        sql = "SELECT SUM(value) OVER (ORDER BY date RANGE BETWEEN 1 PRECEDING AND CURRENT ROW) FROM t";
        std::cout << "\nTrying simpler: " << sql << std::endl;
        result = p2.parse(sql);
        if (result.has_value()) {
            std::cout << "Simple version works" << std::endl;
        } else {
            std::cout << "Simple version also fails" << std::endl;
        }
    }
    
    return 0;
}
