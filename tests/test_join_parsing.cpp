/*
 * Test JOIN Parsing - Debug what's happening
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include "db25/ast/node_types.hpp"
#include <iostream>
#include <string>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class JoinParsingTest : public ::testing::Test {
protected:
    std::unique_ptr<Parser> parser;
    
    void SetUp() override {
        parser = std::make_unique<Parser>();
    }
    
    ASTNode* parse(const std::string& sql) {
        auto result = parser->parse(sql);
        return result.has_value() ? result.value() : nullptr;
    }
    
    std::string print_ast(ASTNode* node, int depth = 0) {
        if (!node) return "";
        
        std::string indent(depth * 2, ' ');
        std::string result = indent + node_type_to_string(node->node_type);
        
        if (!node->primary_text.empty()) {
            result += " [" + std::string(node->primary_text) + "]";
        }
        result += "\n";
        
        auto* child = node->first_child;
        while (child) {
            result += print_ast(child, depth + 1);
            child = child->next_sibling;
        }
        
        return result;
    }
    
    const char* node_type_to_string(NodeType type) {
        switch (type) {
            case NodeType::SelectStmt: return "SelectStmt";
            case NodeType::SelectList: return "SelectList";
            case NodeType::FromClause: return "FromClause";
            case NodeType::JoinClause: return "JoinClause";
            case NodeType::WhereClause: return "WhereClause";
            case NodeType::TableRef: return "TableRef";
            case NodeType::Identifier: return "Identifier";
            case NodeType::ColumnRef: return "ColumnRef";
            case NodeType::BinaryExpr: return "BinaryExpr";
            case NodeType::Star: return "Star";
            case NodeType::IntegerLiteral: return "IntegerLiteral";
            default: return "Unknown";
        }
    }
};

TEST_F(JoinParsingTest, CurrentJoinBehavior) {
    std::cout << "\n=== Current JOIN Parsing Behavior ===\n";
    
    // Test 1: Simple INNER JOIN
    {
        std::string sql = "SELECT * FROM users JOIN orders ON users.id = orders.user_id";
        std::cout << "\nSQL: " << sql << "\n";
        
        auto* ast = parse(sql);
        if (ast) {
            std::cout << "AST Structure:\n" << print_ast(ast);
        } else {
            std::cout << "Failed to parse\n";
        }
    }
    
    // Test 2: LEFT JOIN
    {
        std::string sql = "SELECT * FROM users LEFT JOIN orders ON users.id = orders.user_id";
        std::cout << "\nSQL: " << sql << "\n";
        
        auto* ast = parse(sql);
        if (ast) {
            std::cout << "AST Structure:\n" << print_ast(ast);
        } else {
            std::cout << "Failed to parse\n";
        }
    }
    
    // Test 3: Multiple JOINs
    {
        std::string sql = "SELECT * FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id";
        std::cout << "\nSQL: " << sql << "\n";
        
        auto* ast = parse(sql);
        if (ast) {
            std::cout << "AST Structure:\n" << print_ast(ast);
        } else {
            std::cout << "Failed to parse\n";
        }
    }
}