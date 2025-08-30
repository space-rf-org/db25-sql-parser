#include "db25/parser/parser.hpp"
#include <iostream>

using namespace db25::parser;

int main() {
    std::cout << "Creating parser..." << std::endl;
    Parser p;
    
    std::cout << "Setting SQL..." << std::endl;
    std::string sql = "CREATE TABLE t (name TEXT)";
    
    std::cout << "Calling parse..." << std::endl;
    std::cout.flush();
    
    auto r = p.parse(sql);
    
    std::cout << "Parse returned" << std::endl;
    std::cout << "Result: " << (r.has_value() ? "OK" : "FAIL") << std::endl;
    
    return 0;
}
