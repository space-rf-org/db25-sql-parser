#include "db25/parser/parser.hpp"
#include <iostream>
#include <vector>
#include <chrono>

using namespace db25;
using namespace db25::parser;

void test_memory_usage() {
    std::cout << "Testing memory usage patterns..." << std::endl;
    
    // Test 1: Parse and reset multiple times
    {
        Parser parser;
        for (int i = 0; i < 100; i++) {
            auto result = parser.parse("SELECT * FROM users WHERE id = 1");
            parser.reset(); // Should free arena memory
        }
        std::cout << "  ✓ Multiple parse/reset cycles completed" << std::endl;
    }
    
    // Test 2: Parse large query
    {
        Parser parser;
        std::string large_query = "SELECT ";
        for (int i = 0; i < 100; i++) {
            if (i > 0) large_query += ", ";
            large_query += "col" + std::to_string(i);
        }
        large_query += " FROM table WHERE ";
        for (int i = 0; i < 50; i++) {
            if (i > 0) large_query += " AND ";
            large_query += "col" + std::to_string(i) + " = " + std::to_string(i);
        }
        
        auto result = parser.parse(large_query);
        if (result.has_value()) {
            std::cout << "  ✓ Large query parsed successfully" << std::endl;
            std::cout << "    Memory used: " << parser.get_memory_used() << " bytes" << std::endl;
            std::cout << "    Node count: " << parser.get_node_count() << " nodes" << std::endl;
        }
    }
    
    // Test 3: Parse script with many statements
    {
        Parser parser;
        std::string script;
        for (int i = 0; i < 50; i++) {
            script += "SELECT " + std::to_string(i) + "; ";
        }
        
        auto result = parser.parse_script(script);
        if (result.has_value()) {
            std::cout << "  ✓ Script with " << result.value().size() << " statements parsed" << std::endl;
            std::cout << "    Memory used: " << parser.get_memory_used() << " bytes" << std::endl;
        }
    }
}

void test_arena_efficiency() {
    std::cout << "\nTesting arena allocation efficiency..." << std::endl;
    
    Parser parser;
    
    // Parse increasingly complex queries
    std::vector<size_t> memory_sizes;
    std::vector<size_t> node_counts;
    
    for (int complexity = 1; complexity <= 5; complexity++) {
        parser.reset();
        
        std::string query = "WITH ";
        for (int i = 0; i < complexity; i++) {
            if (i > 0) query += ", ";
            query += "cte" + std::to_string(i) + " AS (SELECT " + std::to_string(i) + ")";
        }
        query += " SELECT * FROM cte0";
        
        auto result = parser.parse(query);
        if (result.has_value()) {
            memory_sizes.push_back(parser.get_memory_used());
            node_counts.push_back(parser.get_node_count());
        }
    }
    
    std::cout << "  Complexity | Memory (bytes) | Nodes" << std::endl;
    std::cout << "  -----------|----------------|-------" << std::endl;
    for (size_t i = 0; i < memory_sizes.size(); i++) {
        std::cout << "  " << (i + 1) << "          | " 
                  << memory_sizes[i] << "          | " 
                  << node_counts[i] << std::endl;
    }
    
    // Check that memory grows reasonably
    bool efficient = true;
    for (size_t i = 1; i < memory_sizes.size(); i++) {
        double growth = (double)memory_sizes[i] / memory_sizes[i-1];
        if (growth > 3.0) {  // Should not grow more than 3x per complexity level
            efficient = false;
            break;
        }
    }
    
    if (efficient) {
        std::cout << "  ✓ Memory growth is reasonable" << std::endl;
    } else {
        std::cout << "  ⚠ Memory growth may be excessive" << std::endl;
    }
}

int main() {
    std::cout << "\n=== Testing Memory Management ===" << std::endl;
    
    test_memory_usage();
    test_arena_efficiency();
    
    std::cout << "\n✅ Memory tests completed!" << std::endl;
    return 0;
}
