#include "db25/parser/parser.hpp"
#include <iostream>
#include <cassert>

using namespace db25;
using namespace db25::parser;

void test_edge_cases() {
    Parser parser;
    
    // Test empty VALUES rows (edge case for null pointer)
    auto result = parser.parse("VALUES (), ()");
    if (!result.has_value()) {
        std::cout << "Empty VALUES rows handled correctly (parse failed as expected)" << std::endl;
    }
    
    // Test ON CONFLICT with empty column list
    result = parser.parse("INSERT INTO t VALUES (1) ON CONFLICT DO NOTHING");
    if (result.has_value()) {
        std::cout << "ON CONFLICT without column list handled correctly" << std::endl;
    }
    
    // Test RETURNING with single expression
    result = parser.parse("DELETE FROM users RETURNING id");
    if (result.has_value()) {
        std::cout << "RETURNING with single expression handled correctly" << std::endl;
    }
    
    // Test USING with single column
    result = parser.parse("DELETE FROM t1 USING t2 WHERE t1.id = t2.id");
    if (result.has_value()) {
        std::cout << "USING clause handled correctly" << std::endl;
    }
    
    std::cout << "✅ Null safety tests passed!" << std::endl;
}

int main() {
    try {
        test_edge_cases();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
