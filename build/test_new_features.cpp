#include "db25/parser/parser.hpp"
#include <iostream>
#include <cassert>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

void test_transaction_statements() {
    std::cout << "Testing transaction statements..." << std::endl;
    
    Parser parser;
    
    // Test BEGIN TRANSACTION
    auto result = parser.parse("BEGIN TRANSACTION");
    assert(result.has_value());
    assert(result.value()->node_type == NodeType::BeginStmt);
    
    // Test COMMIT
    result = parser.parse("COMMIT");
    assert(result.has_value());
    assert(result.value()->node_type == NodeType::CommitStmt);
    
    // Test ROLLBACK
    result = parser.parse("ROLLBACK");
    assert(result.has_value());
    assert(result.value()->node_type == NodeType::RollbackStmt);
    
    // Test SAVEPOINT
    result = parser.parse("SAVEPOINT sp1");
    assert(result.has_value());
    assert(result.value()->node_type == NodeType::SavepointStmt);
    
    std::cout << "✓ Transaction statements work!" << std::endl;
}

void test_values_statement() {
    std::cout << "Testing VALUES statement..." << std::endl;
    
    Parser parser;
    
    // Test VALUES with single row
    auto result = parser.parse("VALUES (1, 2, 3)");
    assert(result.has_value());
    assert(result.value()->node_type == NodeType::ValuesStmt);
    
    // Test VALUES with multiple rows
    result = parser.parse("VALUES (1, 'a'), (2, 'b'), (3, 'c')");
    assert(result.has_value());
    assert(result.value()->node_type == NodeType::ValuesStmt);
    
    std::cout << "✓ VALUES statement works!" << std::endl;
}

void test_grouping_sets() {
    std::cout << "Testing GROUPING SETS/CUBE/ROLLUP..." << std::endl;
    
    Parser parser;
    
    // Test CUBE
    auto result = parser.parse("SELECT category, product, SUM(sales) FROM products GROUP BY CUBE(category, product)");
    assert(result.has_value());
    
    // Test ROLLUP
    result = parser.parse("SELECT year, month, SUM(revenue) FROM sales GROUP BY ROLLUP(year, month)");
    assert(result.has_value());
    
    // Test GROUPING SETS
    result = parser.parse("SELECT a, b, SUM(c) FROM t GROUP BY GROUPING SETS ((a), (b), (a, b))");
    assert(result.has_value());
    
    std::cout << "✓ GROUPING SETS/CUBE/ROLLUP work!" << std::endl;
}

void test_utility_statements() {
    std::cout << "Testing utility statements..." << std::endl;
    
    Parser parser;
    
    // Test EXPLAIN
    auto result = parser.parse("EXPLAIN SELECT * FROM users");
    assert(result.has_value());
    assert(result.value()->node_type == NodeType::ExplainStmt);
    
    // Test VACUUM
    result = parser.parse("VACUUM");
    assert(result.has_value());
    assert(result.value()->node_type == NodeType::VacuumStmt);
    
    // Test ANALYZE
    result = parser.parse("ANALYZE users");
    assert(result.has_value());
    assert(result.value()->node_type == NodeType::AnalyzeStmt);
    
    std::cout << "✓ Utility statements work!" << std::endl;
}

void test_multi_statement() {
    std::cout << "Testing multi-statement parsing..." << std::endl;
    
    Parser parser;
    
    auto result = parser.parse_script("SELECT 1; SELECT 2; SELECT 3");
    assert(result.has_value());
    assert(result.value().size() == 3);
    
    std::cout << "✓ Multi-statement parsing works!" << std::endl;
}

int main() {
    std::cout << "Testing new parser features..." << std::endl;
    
    try {
        test_transaction_statements();
        test_values_statement();
        test_grouping_sets();
        test_utility_statements();
        test_multi_statement();
        
        std::cout << "\n✅ All tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
