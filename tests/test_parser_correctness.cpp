/*
 * Parser Correctness Test Suite
 * Comprehensive tests to verify the parser produces correct AST structures
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include "db25/ast/node_types.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <functional>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class ParserCorrectnessTest : public ::testing::Test {
protected:
    std::unique_ptr<Parser> parser;
    
    void SetUp() override {
        parser = std::make_unique<Parser>();
    }
    
    // Helper to parse and get AST
    ASTNode* parse(const std::string& sql) {
        auto result = parser->parse(sql);
        if (result.has_value()) {
            return result.value();
        }
        return nullptr;
    }
    
    // Helper to print AST structure for debugging
    std::string print_ast(ASTNode* node, int depth = 0) {
        if (!node) return "";
        
        std::stringstream ss;
        std::string indent(depth * 2, ' ');
        
        ss << indent << node_type_to_string(node->node_type);
        if (!node->primary_text.empty()) {
            ss << " [" << node->primary_text << "]";
        }
        ss << " (id:" << node->node_id << ", children:" << node->child_count << ")\n";
        
        // Print children
        auto* child = node->first_child;
        while (child) {
            ss << print_ast(child, depth + 1);
            child = child->next_sibling;
        }
        
        return ss.str();
    }
    
    // Helper to convert NodeType to string
    const char* node_type_to_string(NodeType type) {
        switch (type) {
            case NodeType::Unknown: return "Unknown";
            case NodeType::SelectStmt: return "SelectStmt";
            case NodeType::InsertStmt: return "InsertStmt";
            case NodeType::UpdateStmt: return "UpdateStmt";
            case NodeType::DeleteStmt: return "DeleteStmt";
            case NodeType::CreateTableStmt: return "CreateTableStmt";
            case NodeType::SelectList: return "SelectList";
            case NodeType::FromClause: return "FromClause";
            case NodeType::WhereClause: return "WhereClause";
            case NodeType::GroupByClause: return "GroupByClause";
            case NodeType::HavingClause: return "HavingClause";
            case NodeType::OrderByClause: return "OrderByClause";
            case NodeType::LimitClause: return "LimitClause";
            case NodeType::Identifier: return "Identifier";
            case NodeType::ColumnRef: return "ColumnRef";
            case NodeType::TableRef: return "TableRef";
            case NodeType::BinaryExpr: return "BinaryExpr";
            case NodeType::UnaryExpr: return "UnaryExpr";
            case NodeType::FunctionExpr: return "FunctionExpr";
            case NodeType::FunctionCall: return "FunctionCall";
            case NodeType::IntegerLiteral: return "IntegerLiteral";
            case NodeType::StringLiteral: return "StringLiteral";
            case NodeType::Star: return "Star";
            case NodeType::JoinClause: return "JoinClause";
            case NodeType::BetweenExpr: return "BetweenExpr";
            case NodeType::InExpr: return "InExpr";
            case NodeType::LikeExpr: return "LikeExpr";
            case NodeType::IsNullExpr: return "IsNullExpr";
            default: return "???";
        }
    }
    
    // Recursive helper to find node by type
    ASTNode* find_node_by_type(ASTNode* root, NodeType type) {
        if (!root) return nullptr;
        if (root->node_type == type) return root;
        
        auto* child = root->first_child;
        while (child) {
            auto* found = find_node_by_type(child, type);
            if (found) return found;
            child = child->next_sibling;
        }
        return nullptr;
    }
    
    // Count nodes of a specific type
    int count_nodes_of_type(ASTNode* root, NodeType type) {
        if (!root) return 0;
        
        int count = (root->node_type == type) ? 1 : 0;
        
        auto* child = root->first_child;
        while (child) {
            count += count_nodes_of_type(child, type);
            child = child->next_sibling;
        }
        return count;
    }
};

// ============================================================================
// BASIC CORRECTNESS TESTS
// ============================================================================

TEST_F(ParserCorrectnessTest, SimpleSelectStructure) {
    auto* ast = parse("SELECT id, name FROM users");
    ASSERT_NE(ast, nullptr);
    
    // Verify root is SELECT statement
    EXPECT_EQ(ast->node_type, NodeType::SelectStmt);
    EXPECT_EQ(ast->child_count, 2); // SelectList and FromClause
    
    // Verify SelectList
    auto* select_list = ast->first_child;
    ASSERT_NE(select_list, nullptr);
    EXPECT_EQ(select_list->node_type, NodeType::SelectList);
    EXPECT_EQ(select_list->child_count, 2); // id and name
    
    // Verify columns
    auto* col1 = select_list->first_child;
    ASSERT_NE(col1, nullptr);
    EXPECT_EQ(col1->node_type, NodeType::ColumnRef);
    EXPECT_EQ(col1->primary_text, "id");
    
    auto* col2 = col1->next_sibling;
    ASSERT_NE(col2, nullptr);
    EXPECT_EQ(col2->node_type, NodeType::ColumnRef);
    EXPECT_EQ(col2->primary_text, "name");
    
    // Verify FromClause
    auto* from_clause = select_list->next_sibling;
    ASSERT_NE(from_clause, nullptr);
    EXPECT_EQ(from_clause->node_type, NodeType::FromClause);
    
    auto* table = from_clause->first_child;
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->node_type, NodeType::TableRef);
    EXPECT_EQ(table->primary_text, "users");
}

TEST_F(ParserCorrectnessTest, QualifiedColumnCorrectness) {
    auto* ast = parse("SELECT users.id, users.name, email FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_node_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    EXPECT_EQ(select_list->child_count, 3);
    
    // First column should be ColumnRef (qualified)
    auto* col1 = select_list->first_child;
    ASSERT_NE(col1, nullptr);
    EXPECT_EQ(col1->node_type, NodeType::ColumnRef);
    EXPECT_EQ(col1->primary_text, "users.id");
    
    // Second column should be ColumnRef (qualified)
    auto* col2 = col1->next_sibling;
    ASSERT_NE(col2, nullptr);
    EXPECT_EQ(col2->node_type, NodeType::ColumnRef);
    EXPECT_EQ(col2->primary_text, "users.name");
    
    // Third column should be Identifier (unqualified)
    auto* col3 = col2->next_sibling;
    ASSERT_NE(col3, nullptr);
    EXPECT_EQ(col3->node_type, NodeType::ColumnRef);
    EXPECT_EQ(col3->primary_text, "email");
}

TEST_F(ParserCorrectnessTest, WhereClauseStructure) {
    auto* ast = parse("SELECT * FROM users WHERE age > 21 AND status = 'active'");
    ASSERT_NE(ast, nullptr);
    
    auto* where = find_node_by_type(ast, NodeType::WhereClause);
    ASSERT_NE(where, nullptr);
    
    // WHERE should have a binary expression as child
    auto* condition = where->first_child;
    ASSERT_NE(condition, nullptr);
    EXPECT_EQ(condition->node_type, NodeType::BinaryExpr);
    
    // The top-level operator should be AND
    EXPECT_EQ(condition->primary_text, "AND");
    
    // Check left side (age > 21)
    auto* left_expr = condition->first_child;
    ASSERT_NE(left_expr, nullptr);
    EXPECT_EQ(left_expr->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(left_expr->primary_text, ">");
    
    // Check right side (status = 'active')
    auto* right_expr = left_expr->next_sibling;
    ASSERT_NE(right_expr, nullptr);
    EXPECT_EQ(right_expr->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(right_expr->primary_text, "=");
}

TEST_F(ParserCorrectnessTest, FunctionCallStructure) {
    auto* ast = parse("SELECT COUNT(*), MAX(price), MIN(age) FROM products");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_node_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    EXPECT_EQ(select_list->child_count, 3);
    
    // All three should be function calls
    auto* func1 = select_list->first_child;
    ASSERT_NE(func1, nullptr);
    EXPECT_EQ(func1->node_type, NodeType::FunctionCall);
    EXPECT_EQ(func1->primary_text, "COUNT");
    
    auto* func2 = func1->next_sibling;
    ASSERT_NE(func2, nullptr);
    EXPECT_EQ(func2->node_type, NodeType::FunctionCall);
    EXPECT_EQ(func2->primary_text, "MAX");
    
    auto* func3 = func2->next_sibling;
    ASSERT_NE(func3, nullptr);
    EXPECT_EQ(func3->node_type, NodeType::FunctionCall);
    EXPECT_EQ(func3->primary_text, "MIN");
}

TEST_F(ParserCorrectnessTest, OrderByStructure) {
    auto* ast = parse("SELECT * FROM users ORDER BY age DESC, name ASC");
    ASSERT_NE(ast, nullptr);
    
    auto* order_by = find_node_by_type(ast, NodeType::OrderByClause);
    ASSERT_NE(order_by, nullptr);
    
    // Should have 2 order items
    EXPECT_EQ(order_by->child_count, 2);
    
    auto* item1 = order_by->first_child;
    ASSERT_NE(item1, nullptr);
    EXPECT_EQ(item1->node_type, NodeType::ColumnRef);
    EXPECT_EQ(item1->primary_text, "age");
    
    auto* item2 = item1->next_sibling;
    ASSERT_NE(item2, nullptr);
    EXPECT_EQ(item2->node_type, NodeType::ColumnRef);
    EXPECT_EQ(item2->primary_text, "name");
}

TEST_F(ParserCorrectnessTest, ComplexExpressionStructure) {
    auto* ast = parse("SELECT id * 2 + 1, price - discount FROM products");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_node_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    EXPECT_EQ(select_list->child_count, 2);
    
    // First expression: id * 2 + 1
    auto* expr1 = select_list->first_child;
    ASSERT_NE(expr1, nullptr);
    EXPECT_EQ(expr1->node_type, NodeType::BinaryExpr);
    // Due to left-to-right parsing, this will be (id * 2) + 1
    // So top level operator is +
    EXPECT_EQ(expr1->primary_text, "+");
    
    // Second expression: price - discount  
    auto* expr2 = expr1->next_sibling;
    ASSERT_NE(expr2, nullptr);
    EXPECT_EQ(expr2->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(expr2->primary_text, "-");
}

// ============================================================================
// PARENT-CHILD RELATIONSHIP TESTS
// ============================================================================

TEST_F(ParserCorrectnessTest, ParentChildRelationships) {
    auto* ast = parse("SELECT id FROM users WHERE age > 18");
    ASSERT_NE(ast, nullptr);
    
    // Check that all children have correct parent pointers
    auto* select_list = ast->first_child;
    ASSERT_NE(select_list, nullptr);
    EXPECT_EQ(select_list->parent, ast);
    
    auto* from_clause = select_list->next_sibling;
    ASSERT_NE(from_clause, nullptr);
    EXPECT_EQ(from_clause->parent, ast);
    
    auto* where_clause = from_clause->next_sibling;
    ASSERT_NE(where_clause, nullptr);
    EXPECT_EQ(where_clause->parent, ast);
    
    // Check deeper relationships
    auto* id_col = select_list->first_child;
    ASSERT_NE(id_col, nullptr);
    EXPECT_EQ(id_col->parent, select_list);
    
    auto* table_ref = from_clause->first_child;
    ASSERT_NE(table_ref, nullptr);
    EXPECT_EQ(table_ref->parent, from_clause);
    
    auto* condition = where_clause->first_child;
    ASSERT_NE(condition, nullptr);
    EXPECT_EQ(condition->parent, where_clause);
}

TEST_F(ParserCorrectnessTest, SiblingRelationships) {
    auto* ast = parse("SELECT a, b, c FROM t");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_node_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    // Check sibling chain
    auto* col_a = select_list->first_child;
    ASSERT_NE(col_a, nullptr);
    EXPECT_EQ(col_a->primary_text, "a");
    EXPECT_EQ(col_a->parent, select_list);
    
    auto* col_b = col_a->next_sibling;
    ASSERT_NE(col_b, nullptr);
    EXPECT_EQ(col_b->primary_text, "b");
    EXPECT_EQ(col_b->parent, select_list);
    
    auto* col_c = col_b->next_sibling;
    ASSERT_NE(col_c, nullptr);
    EXPECT_EQ(col_c->primary_text, "c");
    EXPECT_EQ(col_c->parent, select_list);
    EXPECT_EQ(col_c->next_sibling, nullptr); // Last sibling
}

// ============================================================================
// LITERAL HANDLING TESTS
// ============================================================================

TEST_F(ParserCorrectnessTest, LiteralTypes) {
    auto* ast = parse("SELECT 42, 'hello', 3.14 FROM dual WHERE id = 100 AND name = 'test'");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_node_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    // First item should be integer literal
    auto* lit1 = select_list->first_child;
    ASSERT_NE(lit1, nullptr);
    EXPECT_EQ(lit1->node_type, NodeType::IntegerLiteral);
    EXPECT_EQ(lit1->primary_text, "42");
    
    // Second item should be string literal
    auto* lit2 = lit1->next_sibling;
    ASSERT_NE(lit2, nullptr);
    EXPECT_EQ(lit2->node_type, NodeType::StringLiteral);
    EXPECT_EQ(lit2->primary_text, "'hello'");
    
    // Third item should be number (parsed as integer for now)
    auto* lit3 = lit2->next_sibling;
    ASSERT_NE(lit3, nullptr);
    EXPECT_EQ(lit3->node_type, NodeType::IntegerLiteral);
    EXPECT_EQ(lit3->primary_text, "3.14");
}

// ============================================================================
// EDGE CASES AND ERROR HANDLING
// ============================================================================

TEST_F(ParserCorrectnessTest, EmptySelectList) {
    auto* ast = parse("SELECT FROM users");
    // Should either fail or handle gracefully
    // Current implementation might create empty select list
    if (ast) {
        auto* select_list = find_node_by_type(ast, NodeType::SelectList);
        if (select_list) {
            EXPECT_EQ(select_list->child_count, 0);
        }
    }
}

TEST_F(ParserCorrectnessTest, MissingFromClause) {
    auto* ast = parse("SELECT id, name");
    ASSERT_NE(ast, nullptr);
    
    // Should have only SelectList, no FromClause
    EXPECT_EQ(ast->child_count, 1);
    
    auto* select_list = ast->first_child;
    ASSERT_NE(select_list, nullptr);
    EXPECT_EQ(select_list->node_type, NodeType::SelectList);
    EXPECT_EQ(select_list->next_sibling, nullptr);
}

TEST_F(ParserCorrectnessTest, MultipleWhereConditions) {
    auto* ast = parse("SELECT * FROM users WHERE age > 18 AND age < 65 AND active = true");
    ASSERT_NE(ast, nullptr);
    
    auto* where = find_node_by_type(ast, NodeType::WhereClause);
    ASSERT_NE(where, nullptr);
    
    // Count binary expressions (should be at least 3 for the ANDs and comparisons)
    int binary_count = count_nodes_of_type(where, NodeType::BinaryExpr);
    EXPECT_GE(binary_count, 3);
}

// ============================================================================
// STATEMENT TYPE TESTS
// ============================================================================

TEST_F(ParserCorrectnessTest, InsertStatementStructure) {
    auto* ast = parse("INSERT INTO users VALUES (1, 'John')");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::InsertStmt);
}

TEST_F(ParserCorrectnessTest, UpdateStatementStructure) {
    auto* ast = parse("UPDATE users SET name = 'Jane' WHERE id = 1");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::UpdateStmt);
}

TEST_F(ParserCorrectnessTest, DeleteStatementStructure) {
    auto* ast = parse("DELETE FROM users WHERE id = 1");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::DeleteStmt);
}

// ============================================================================
// NODE ID UNIQUENESS TEST
// ============================================================================

TEST_F(ParserCorrectnessTest, NodeIDsAreUnique) {
    auto* ast = parse("SELECT a, b, c FROM t1, t2 WHERE x = 1 AND y = 2 ORDER BY z");
    ASSERT_NE(ast, nullptr);
    
    std::vector<uint32_t> node_ids;
    std::function<void(ASTNode*)> collect_ids = [&](ASTNode* node) {
        if (!node) return;
        node_ids.push_back(node->node_id);
        
        auto* child = node->first_child;
        while (child) {
            collect_ids(child);
            child = child->next_sibling;
        }
    };
    
    collect_ids(ast);
    
    // Check all IDs are unique
    std::sort(node_ids.begin(), node_ids.end());
    for (size_t i = 1; i < node_ids.size(); i++) {
        EXPECT_NE(node_ids[i], node_ids[i-1]) << "Duplicate node ID found: " << node_ids[i];
    }
}

// ============================================================================
// PRINT AST FOR DEBUGGING
// ============================================================================

TEST_F(ParserCorrectnessTest, DebugPrintComplexQuery) {
    auto* ast = parse("SELECT u.id, u.name, COUNT(*) FROM users u WHERE u.age > 21 GROUP BY u.id, u.name ORDER BY COUNT(*) DESC LIMIT 10");
    
    if (ast) {
        std::cout << "\n=== AST Structure ===\n";
        std::cout << print_ast(ast);
        std::cout << "===================\n";
    }
    
    // This test just prints for manual inspection
    EXPECT_NE(ast, nullptr);
}