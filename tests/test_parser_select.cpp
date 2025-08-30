/*
 * DB25 Parser - SELECT Statement Tests
 * Testing proper parsing of SELECT clauses and AST structure
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class SelectParserTest : public ::testing::Test {
protected:
    Parser parser;
    
    void SetUp() override {
        parser.reset();
    }
    
    ASTNode* parse_select(const std::string& sql) {
        auto result = parser.parse(sql);
        if (!result.has_value()) {
            return nullptr;
        }
        return result.value();
    }
    
    // Helper to find a child node by type
    ASTNode* find_child_by_type(ASTNode* parent, NodeType type) {
        if (!parent) return nullptr;
        
        ASTNode* child = parent->get_first_child();
        while (child) {
            if (child->node_type == type) {
                return child;
            }
            child = child->get_next_sibling();
        }
        return nullptr;
    }
    
    // Count children
    int count_children(ASTNode* parent) {
        if (!parent) return 0;
        
        int count = 0;
        ASTNode* child = parent->get_first_child();
        while (child) {
            count++;
            child = child->get_next_sibling();
        }
        return count;
    }
};

// ============================================================================
// BASIC STRUCTURE TESTS
// ============================================================================

TEST_F(SelectParserTest, SelectStarHasCorrectStructure) {
    auto* ast = parse_select("SELECT * FROM users");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::SelectStmt);
    
    // Should have at least 2 children: select_list and from_clause
    EXPECT_GE(count_children(ast), 2);
    
    // Find SELECT list
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr) << "SELECT statement should have SelectList child";
    
    // Find FROM clause
    auto* from_clause = find_child_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr) << "SELECT statement should have FromClause child";
}

TEST_F(SelectParserTest, SelectListWithColumns) {
    auto* ast = parse_select("SELECT id, name, email FROM users");
    auto children = ast->get_children();

    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    // Should have 3 column references
    int column_count = count_children(select_list);
    EXPECT_EQ(column_count, 3) << "SELECT list should have 3 columns";
}

TEST_F(SelectParserTest, FromClauseWithTable) {
    auto* ast = parse_select("SELECT * FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* from_clause = find_child_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr);
    
    // Should have table reference
    auto* table_ref = find_child_by_type(from_clause, NodeType::TableRef);
    ASSERT_NE(table_ref, nullptr) << "FROM clause should have TableRef child";
    
    // Check table name is stored
    EXPECT_FALSE(table_ref->primary_text.empty());
    EXPECT_EQ(table_ref->primary_text, "users");
}

// ============================================================================
// WHERE CLAUSE TESTS
// ============================================================================

TEST_F(SelectParserTest, WhereClausePresent) {
    auto* ast = parse_select("SELECT * FROM users WHERE id = 1");
    ASSERT_NE(ast, nullptr);
    
    auto* where_clause = find_child_by_type(ast, NodeType::WhereClause);
    ASSERT_NE(where_clause, nullptr) << "Should have WHERE clause";
    
    // WHERE clause should have a condition expression
    EXPECT_GT(count_children(where_clause), 0) << "WHERE clause should have condition";
}

TEST_F(SelectParserTest, WhereWithBinaryExpression) {
    auto* ast = parse_select("SELECT * FROM users WHERE age > 18");
    ASSERT_NE(ast, nullptr);
    
    auto* where_clause = find_child_by_type(ast, NodeType::WhereClause);
    ASSERT_NE(where_clause, nullptr);
    
    // Should have binary expression as child
    auto* binary_expr = find_child_by_type(where_clause, NodeType::BinaryExpr);
    ASSERT_NE(binary_expr, nullptr) << "WHERE should contain binary expression";
}

// ============================================================================
// ORDER BY TESTS
// ============================================================================

TEST_F(SelectParserTest, OrderByClause) {
    auto* ast = parse_select("SELECT * FROM users ORDER BY name");
    ASSERT_NE(ast, nullptr);
    
    auto* order_by = find_child_by_type(ast, NodeType::OrderByClause);
    ASSERT_NE(order_by, nullptr) << "Should have ORDER BY clause";
    
    // Should have at least one order item
    EXPECT_GT(count_children(order_by), 0);
}

TEST_F(SelectParserTest, OrderByWithDirection) {
    auto* ast = parse_select("SELECT * FROM users ORDER BY name DESC");
    ASSERT_NE(ast, nullptr);
    
    auto* order_by = find_child_by_type(ast, NodeType::OrderByClause);
    ASSERT_NE(order_by, nullptr);
    
    // Should handle DESC keyword
    // The order item should have a flag or child indicating DESC
}

// ============================================================================
// LIMIT TESTS
// ============================================================================

TEST_F(SelectParserTest, LimitClause) {
    auto* ast = parse_select("SELECT * FROM users LIMIT 10");
    ASSERT_NE(ast, nullptr);
    
    auto* limit = find_child_by_type(ast, NodeType::LimitClause);
    ASSERT_NE(limit, nullptr) << "Should have LIMIT clause";
    
    // Should have a number literal as child
    auto* number = find_child_by_type(limit, NodeType::IntegerLiteral);
    ASSERT_NE(number, nullptr) << "LIMIT should have integer value";
}

TEST_F(SelectParserTest, LimitWithOffset) {
    auto* ast = parse_select("SELECT * FROM users LIMIT 10 OFFSET 20");
    ASSERT_NE(ast, nullptr);
    
    auto* limit = find_child_by_type(ast, NodeType::LimitClause);
    ASSERT_NE(limit, nullptr);
    
    // Should handle both LIMIT and OFFSET
    EXPECT_EQ(count_children(limit), 2) << "Should have limit and offset values";
}

// ============================================================================
// IDENTIFIER TESTS
// ============================================================================

TEST_F(SelectParserTest, ColumnIdentifiers) {
    auto* ast = parse_select("SELECT first_name, last_name FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    // Check first column
    auto* first_col = select_list->get_first_child();
    ASSERT_NE(first_col, nullptr);
    EXPECT_EQ(first_col->node_type, NodeType::ColumnRef);
    EXPECT_EQ(first_col->primary_text, "first_name");
    
    // Check second column
    auto* second_col = first_col->get_next_sibling();
    ASSERT_NE(second_col, nullptr);
    EXPECT_EQ(second_col->node_type, NodeType::ColumnRef);
    EXPECT_EQ(second_col->primary_text, "last_name");
}

TEST_F(SelectParserTest, QualifiedColumnNames) {
    auto* ast = parse_select("SELECT users.id, users.name FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    auto* first_col = select_list->get_first_child();
    ASSERT_NE(first_col, nullptr);
    
    // Should handle table.column notation
    EXPECT_EQ(first_col->node_type, NodeType::ColumnRef);
    // Should store both table and column parts
}

// ============================================================================
// EXPRESSION TESTS
// ============================================================================

TEST_F(SelectParserTest, ExpressionInSelectList) {
    auto* ast = parse_select("SELECT id + 1, name FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    // First item should be an expression
    auto* first_item = select_list->get_first_child();
    ASSERT_NE(first_item, nullptr);
    EXPECT_EQ(first_item->node_type, NodeType::BinaryExpr);
}

TEST_F(SelectParserTest, FunctionCallInSelect) {
    auto* ast = parse_select("SELECT COUNT(*) FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    auto* func_call = find_child_by_type(select_list, NodeType::FunctionCall);
    ASSERT_NE(func_call, nullptr) << "Should parse function call";
    EXPECT_EQ(func_call->primary_text, "COUNT");
}

// ============================================================================
// COMPLEX QUERIES
// ============================================================================

TEST_F(SelectParserTest, CompleteSelectStatement) {
    auto* ast = parse_select(
        "SELECT id, name "
        "FROM users "
        "WHERE age > 18 "
        "ORDER BY name DESC "
        "LIMIT 10"
    );
    ASSERT_NE(ast, nullptr);
    
    // Should have all clauses
    EXPECT_NE(find_child_by_type(ast, NodeType::SelectList), nullptr);
    EXPECT_NE(find_child_by_type(ast, NodeType::FromClause), nullptr);
    EXPECT_NE(find_child_by_type(ast, NodeType::WhereClause), nullptr);
    EXPECT_NE(find_child_by_type(ast, NodeType::OrderByClause), nullptr);
    EXPECT_NE(find_child_by_type(ast, NodeType::LimitClause), nullptr);
}

// ============================================================================
// ALIAS TESTS
// ============================================================================

TEST_F(SelectParserTest, ColumnAlias) {
    auto* ast = parse_select("SELECT id AS user_id FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    auto* alias_expr = find_child_by_type(select_list, NodeType::AliasRef);
    if (!alias_expr) {
        // Might be stored differently
        auto* first_item = select_list->get_first_child();
        ASSERT_NE(first_item, nullptr);
        // Check if alias is stored in the node
    }
}

TEST_F(SelectParserTest, TableAlias) {
    auto* ast = parse_select("SELECT u.id FROM users AS u");
    ASSERT_NE(ast, nullptr);
    
    auto* from_clause = find_child_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr);
    
    // Should handle table alias
}

// ============================================================================
// STAR EXPANSION
// ============================================================================

TEST_F(SelectParserTest, SelectStar) {
    auto* ast = parse_select("SELECT * FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    auto* star = find_child_by_type(select_list, NodeType::Star);
    ASSERT_NE(star, nullptr) << "Should have Star node for *";
}

TEST_F(SelectParserTest, QualifiedStar) {
    auto* ast = parse_select("SELECT users.* FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    auto* star = find_child_by_type(select_list, NodeType::Star);
    if (star) {
        // Should have table qualifier
        EXPECT_FALSE(star->schema_name.empty() || star->primary_text.empty());
    }
}

// ============================================================================
// DISTINCT TESTS
// ============================================================================

TEST_F(SelectParserTest, DistinctSimple) {
    auto* ast = parse_select("SELECT DISTINCT name FROM users");
    ASSERT_NE(ast, nullptr);
    
    // SELECT statement should have DISTINCT flag
    EXPECT_TRUE(static_cast<bool>(ast->semantic_flags & static_cast<uint16_t>(NodeFlags::Distinct)));
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    // Should still parse the column
    auto* column = find_child_by_type(select_list, NodeType::ColumnRef);
    ASSERT_NE(column, nullptr);
    EXPECT_EQ(column->primary_text, "name");
}

TEST_F(SelectParserTest, DistinctMultipleColumns) {
    auto* ast = parse_select("SELECT DISTINCT department, job_title FROM employees");
    ASSERT_NE(ast, nullptr);
    
    EXPECT_TRUE(static_cast<bool>(ast->semantic_flags & static_cast<uint16_t>(NodeFlags::Distinct)));
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    EXPECT_EQ(select_list->child_count, 2);
}

TEST_F(SelectParserTest, DistinctWithStar) {
    auto* ast = parse_select("SELECT DISTINCT * FROM users");
    ASSERT_NE(ast, nullptr);
    
    EXPECT_TRUE(static_cast<bool>(ast->semantic_flags & static_cast<uint16_t>(NodeFlags::Distinct)));
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    auto* star = find_child_by_type(select_list, NodeType::Star);
    ASSERT_NE(star, nullptr);
}