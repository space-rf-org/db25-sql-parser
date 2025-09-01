/*
 * DB25 Parser - Comprehensive AST Dumper and Tester
 * For architectural assessment and validation
 */

#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <fstream>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

// Comprehensive node type names
const char* get_node_type_name(NodeType type) {
    switch (type) {
        // Statement Nodes
        case NodeType::SelectStmt: return "SelectStmt";
        case NodeType::InsertStmt: return "InsertStmt";
        case NodeType::UpdateStmt: return "UpdateStmt";
        case NodeType::DeleteStmt: return "DeleteStmt";
        case NodeType::CreateTableStmt: return "CreateTableStmt";
        
        // Expression Nodes
        case NodeType::BinaryExpr: return "BinaryExpr";
        case NodeType::UnaryExpr: return "UnaryExpr";
        case NodeType::FunctionCall: return "FunctionCall";
        case NodeType::CaseExpr: return "CaseExpr";
        case NodeType::InExpr: return "InExpr";
        case NodeType::BetweenExpr: return "BetweenExpr";
        case NodeType::LikeExpr: return "LikeExpr";
        case NodeType::IsNullExpr: return "IsNullExpr";
        
        // Clause Nodes
        case NodeType::SelectList: return "SelectList";
        case NodeType::FromClause: return "FromClause";
        case NodeType::JoinClause: return "JoinClause";
        case NodeType::WhereClause: return "WhereClause";
        case NodeType::GroupByClause: return "GroupByClause";
        case NodeType::HavingClause: return "HavingClause";
        case NodeType::OrderByClause: return "OrderByClause";
        case NodeType::LimitClause: return "LimitClause";
        
        // Reference Nodes
        case NodeType::TableRef: return "TableRef";
        case NodeType::ColumnRef: return "ColumnRef";
        
        // Literal Nodes
        case NodeType::IntegerLiteral: return "IntegerLiteral";
        case NodeType::FloatLiteral: return "FloatLiteral";
        case NodeType::StringLiteral: return "StringLiteral";
        case NodeType::BooleanLiteral: return "BooleanLiteral";
        case NodeType::NullLiteral: return "NullLiteral";
        
        // Other Nodes
        case NodeType::Identifier: return "Identifier";
        case NodeType::Star: return "Star";
        
        // Advanced Features
        case NodeType::Subquery: return "Subquery";
        case NodeType::CTEClause: return "CTEClause";
        case NodeType::CTEDefinition: return "CTEDefinition";
        case NodeType::ColumnList: return "ColumnList";
        case NodeType::UnionStmt: return "UnionStmt";
        case NodeType::IntersectStmt: return "IntersectStmt";
        case NodeType::ExceptStmt: return "ExceptStmt";
        
        default: return "Unknown";
    }
}

// Pretty print AST with detailed information
void print_ast(ASTNode* node, int depth = 0, std::ostream& out = std::cout) {
    if (!node) return;
    
    // Indentation
    for (int i = 0; i < depth; i++) {
        out << "  ";
    }
    
    // Node type
    out << get_node_type_name(node->node_type);
    
    // Node ID
    out << " [id:" << node->node_id << "]";
    
    // Primary text
    if (!node->primary_text.empty()) {
        out << " text:\"" << node->primary_text << "\"";
    }
    
    // Schema/alias name
    if (!node->schema_name.empty()) {
        out << " alias:\"" << node->schema_name << "\"";
    }
    
    // Semantic flags
    if (node->semantic_flags != 0) {
        out << " flags:0x" << std::hex << node->semantic_flags << std::dec;
        // Decode flags
        if (node->semantic_flags & 0x01) out << "[DISTINCT]";
        if (node->semantic_flags & 0x02) out << "[ALL]";
        if (node->semantic_flags & 0x20) out << "[HAS_ALIAS]";
        if (node->semantic_flags & 0x40) out << "[NOT]";
        if (node->semantic_flags & 0x80) out << "[DESC]";
        if (node->semantic_flags & 0x100) out << "[RECURSIVE]";
    }
    
    // Statistics
    if (node->child_count > 0) {
        out << " children:" << node->child_count;
    }
    
    out << "\n";
    
    // Process children
    ASTNode* child = node->first_child;
    while (child) {
        print_ast(child, depth + 1, out);
        child = child->next_sibling;
    }
}

// Test suite of SQL queries
struct TestQuery {
    const char* description;
    const char* sql;
};

std::vector<TestQuery> test_queries = {
    // Basic SELECT
    {"Simple SELECT", "SELECT * FROM users"},
    {"SELECT with columns", "SELECT id, name, email FROM users"},
    {"SELECT with WHERE", "SELECT * FROM users WHERE active = true"},
    {"SELECT with JOIN", "SELECT u.*, o.* FROM users u JOIN orders o ON u.id = o.user_id"},
    
    // Complex queries
    {"GROUP BY with HAVING", 
     "SELECT department, COUNT(*) as count FROM employees GROUP BY department HAVING COUNT(*) > 10"},
    
    {"Complex JOIN", 
     "SELECT * FROM orders o "
     "LEFT JOIN customers c ON o.customer_id = c.id "
     "RIGHT JOIN products p ON o.product_id = p.id"},
    
    // Expressions
    {"Arithmetic expressions", 
     "SELECT price * quantity as total, price + tax as final_price FROM items"},
    
    {"CASE expression", 
     "SELECT name, CASE WHEN age < 18 THEN 'Minor' WHEN age >= 65 THEN 'Senior' ELSE 'Adult' END as category FROM persons"},
    
    // SQL operators
    {"BETWEEN operator", "SELECT * FROM products WHERE price BETWEEN 10 AND 100"},
    {"IN operator", "SELECT * FROM orders WHERE status IN ('pending', 'processing', 'shipped')"},
    {"LIKE operator", "SELECT * FROM users WHERE email LIKE '%@gmail.com'"},
    {"IS NULL", "SELECT * FROM employees WHERE manager_id IS NULL"},
    {"EXISTS", "SELECT * FROM customers WHERE EXISTS (SELECT 1 FROM orders WHERE orders.customer_id = customers.id)"},
    
    // Subqueries
    {"Scalar subquery", 
     "SELECT name, (SELECT COUNT(*) FROM orders WHERE customer_id = customers.id) as order_count FROM customers"},
    
    {"IN subquery", 
     "SELECT * FROM products WHERE category_id IN (SELECT id FROM categories WHERE active = true)"},
    
    {"Correlated subquery",
     "SELECT * FROM employees e1 WHERE salary > (SELECT AVG(salary) FROM employees e2 WHERE e2.department = e1.department)"},
    
    // Set operations
    {"UNION", "SELECT id, name FROM customers UNION SELECT id, name FROM suppliers"},
    {"UNION ALL", "SELECT city FROM customers UNION ALL SELECT city FROM suppliers"},
    {"INTERSECT", "SELECT product_id FROM orders INTERSECT SELECT id FROM products WHERE in_stock = true"},
    {"EXCEPT", "SELECT id FROM products EXCEPT SELECT product_id FROM discontinued_items"},
    
    // CTEs
    {"Simple CTE", 
     "WITH active_users AS (SELECT * FROM users WHERE active = true) SELECT * FROM active_users"},
    
    {"CTE with columns",
     "WITH order_summary (customer_id, total_orders) AS ("
     "  SELECT customer_id, COUNT(*) FROM orders GROUP BY customer_id"
     ") SELECT * FROM order_summary WHERE total_orders > 5"},
    
    {"Multiple CTEs",
     "WITH "
     "high_value AS (SELECT * FROM orders WHERE amount > 1000), "
     "recent AS (SELECT * FROM orders WHERE order_date > '2024-01-01') "
     "SELECT * FROM high_value INTERSECT SELECT * FROM recent"},
    
    // Advanced features
    {"ORDER BY with LIMIT", 
     "SELECT * FROM products ORDER BY price DESC, name ASC LIMIT 10"},
    
    {"DISTINCT with aggregates",
     "SELECT DISTINCT department, COUNT(DISTINCT employee_id) FROM assignments GROUP BY department"},
    
    {"Complex nested",
     "SELECT * FROM (SELECT id, name FROM users WHERE active = true) AS active_users WHERE id IN (SELECT user_id FROM premium_subscriptions)"}
};

int main() {
    Parser parser;
    int passed = 0;
    int failed = 0;
    
    std::ofstream ast_file("ast_dump.txt");
    
    std::cout << "================================================================================\n";
    std::cout << "                    DB25 SQL Parser - Comprehensive Test Suite                 \n";
    std::cout << "================================================================================\n\n";
    
    ast_file << "DB25 SQL Parser - AST Dump\n";
    ast_file << "===========================\n\n";
    
    for (const auto& test : test_queries) {
        std::cout << "Test: " << test.description << "\n";
        std::cout << "SQL:  " << test.sql << "\n";
        
        ast_file << "Test: " << test.description << "\n";
        ast_file << "SQL:  " << test.sql << "\n";
        ast_file << "AST:\n";
        
        parser.reset();
        auto result = parser.parse(test.sql);
        
        if (result.has_value()) {
            std::cout << "Status: ✓ PASSED\n";
            passed++;
            
            // Dump AST to file
            print_ast(result.value(), 0, ast_file);
            
            // Also print to console for important tests
            if (std::string(test.description).find("CTE") != std::string::npos ||
                std::string(test.description).find("subquery") != std::string::npos ||
                std::string(test.description).find("Complex") != std::string::npos) {
                std::cout << "AST Preview:\n";
                print_ast(result.value(), 0, std::cout);
            }
        } else {
            std::cout << "Status: ✗ FAILED\n";
            std::cout << "  Error: " << result.error().message << "\n";
            std::cout << "  Location: line " << result.error().line 
                      << ", column " << result.error().column << "\n";
            failed++;
            
            ast_file << "  PARSE FAILED: " << result.error().message << "\n";
        }
        
        std::cout << std::string(80, '-') << "\n";
        ast_file << "\n" << std::string(80, '-') << "\n\n";
    }
    
    // Summary
    std::cout << "\n================================================================================\n";
    std::cout << "                                   SUMMARY                                     \n";
    std::cout << "================================================================================\n";
    std::cout << "Total Tests: " << (passed + failed) << "\n";
    std::cout << "Passed: " << passed << " (" << (passed * 100.0 / (passed + failed)) << "%)\n";
    std::cout << "Failed: " << failed << "\n";
    
    // Memory statistics
    std::cout << "\nMemory Statistics:\n";
    std::cout << "  Arena memory used: " << parser.get_memory_used() << " bytes\n";
    std::cout << "  Total nodes created: " << parser.get_node_count() << "\n";
    
    ast_file << "\n\nSummary:\n";
    ast_file << "Total Tests: " << (passed + failed) << "\n";
    ast_file << "Passed: " << passed << "\n";
    ast_file << "Failed: " << failed << "\n";
    
    ast_file.close();
    std::cout << "\nFull AST dump written to: ast_dump.txt\n";
    
    return failed > 0 ? 1 : 0;
}