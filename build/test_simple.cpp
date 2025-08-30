#include "db25/parser/parser.hpp"
#include <iostream>

using namespace db25::parser;

int main() {
    std::cout << "Test 1: CREATE TABLE without JSON" << std::endl;
    Parser p1;
    auto r1 = p1.parse("CREATE TABLE t (id INT)");
    std::cout << "Result: " << (r1.has_value() ? "OK" : "FAIL") << std::endl;
    
    std::cout << "Test 2: CREATE TABLE with TEXT" << std::endl;
    Parser p2;
    auto r2 = p2.parse("CREATE TABLE t (data TEXT)");
    std::cout << "Result: " << (r2.has_value() ? "OK" : "FAIL") << std::endl;
    
    std::cout << "Test 3: CREATE TABLE with JSON" << std::endl;
    Parser p3;
    auto r3 = p3.parse("CREATE TABLE t (data JSON)");
    std::cout << "Result: " << (r3.has_value() ? "OK" : "FAIL") << std::endl;
    
    return 0;
}
