/*
 * DB25 Parser - AST Improvements Verification
 * Verifies that the parser improvements are reflected in the AST
 */

#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include <iostream>
#include <sstream>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

void print_node(const ASTNode* node, int depth = 0) {
    if (!node) return;
    
    // Indent
    for (int i = 0; i < depth; i++) {
        std::cout << "  ";
    }
    
    // Node type
    std::cout << "- ";
    switch(node->node_type) {
        case NodeType::SelectStmt: std::cout << "SELECT"; break;
        case NodeType::UnionStmt: std::cout << "UNION"; break;
        case NodeType::FunctionCall: std::cout << "FUNCTION"; break;
        case NodeType::ColumnRef: std::cout << "COLUMN"; break;
        case NodeType::Identifier: std::cout << "ID"; break;
        case NodeType::TableRef: std::cout << "TABLE"; break;
        case NodeType::OrderByClause: std::cout << "ORDER_BY"; break;
        default: std::cout << "NODE"; break;
    }
    
    // Primary text
    if (!node->primary_text.empty()) {
        std::cout << " [" << node->primary_text << "]";
    }
    
    // Flags
    if (node->flags != NodeFlags::None) {
        std::cout << " flags:";
        if (has_flag(node->flags, NodeFlags::All)) std::cout << " ALL";
        if (has_flag(node->flags, NodeFlags::Distinct)) std::cout << " DISTINCT";
    }
    
    // Semantic flags (for NOT EXISTS/IN)
    if (node->semantic_flags != 0) {
        std::cout << " sem:" << std::hex << node->semantic_flags << std::dec;
    }
    
    std::cout << std::endl;
    
    // Children
    for (auto* child = node->first_child; child; child = child->next_sibling) {
        print_node(child, depth + 1);
    }
}

int main() {
    Parser parser;
    
    std::cout << "=== AST Improvements Verification ===" << std::endl;
    
    // Test 1: NOT EXISTS vs EXISTS
    std::cout << "\n1. NOT EXISTS semantic flag:" << std::endl;
    {
        std::string sql = "SELECT * FROM t WHERE NOT EXISTS (SELECT 1 FROM t2)";
        auto result = parser.parse(sql);
        if (result.has_value()) {
            std::cout << "Parse successful. Look for EXISTS with sem:40" << std::endl;
            // sem:40 indicates NOT flag
        }
        parser.reset();
    }
    
    // Test 2: COUNT(DISTINCT)
    std::cout << "\n2. COUNT(DISTINCT) flag:" << std::endl;
    {
        std::string sql = "SELECT COUNT(DISTINCT id) FROM users";
        auto result = parser.parse(sql);
        if (result.has_value()) {
            print_node(result.value());
            std::cout << "Look for FUNCTION [COUNT] with flags: DISTINCT" << std::endl;
        }
        parser.reset();
    }
    
    // Test 3: UNION ALL flag
    std::cout << "\n3. UNION ALL flag:" << std::endl;
    {
        std::string sql = "SELECT id FROM t1 UNION ALL SELECT id FROM t2";
        auto result = parser.parse(sql);
        if (result.has_value()) {
            print_node(result.value());
            std::cout << "Look for UNION with flags: ALL" << std::endl;
        }
        parser.reset();
    }
    
    // Test 4: Column vs Identifier
    std::cout << "\n4. Column references (not IDs):" << std::endl;
    {
        std::string sql = "SELECT name, age FROM users WHERE salary > 1000";
        auto result = parser.parse(sql);
        if (result.has_value()) {
            print_node(result.value());
            std::cout << "Should see COLUMN nodes, not ID nodes for name, age, salary" << std::endl;
        }
        parser.reset();
    }
    
    // Test 5: Complex query with all improvements
    std::cout << "\n5. Complex query with multiple improvements:" << std::endl;
    {
        std::string sql = R"(
            SELECT COUNT(DISTINCT customer_id) as unique_customers
            FROM orders
            WHERE NOT EXISTS (
                SELECT 1 FROM complaints WHERE customer_id = orders.customer_id
            )
            UNION ALL
            SELECT COUNT(*) FROM archived_orders
        )";
        auto result = parser.parse(sql);
        if (result.has_value()) {
            print_node(result.value());
            std::cout << "\nShould see:" << std::endl;
            std::cout << "- UNION with flags: ALL" << std::endl;
            std::cout << "- FUNCTION [COUNT] with flags: DISTINCT" << std::endl;
            std::cout << "- EXISTS with sem:40 (NOT flag)" << std::endl;
            std::cout << "- COLUMN nodes for column references" << std::endl;
        }
    }
    
    return 0;
}