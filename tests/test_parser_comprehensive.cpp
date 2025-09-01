/*
 * DB25 Parser - Comprehensive Test Suite
 * Test-Driven Development Approach
 * 
 * This file defines all expected parser behavior BEFORE implementation
 * The parser will be implemented to satisfy these tests
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include <string_view>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class ParserTest : public ::testing::Test {
protected:
    Parser parser;
    
    void SetUp() override {
        parser.reset();
    }
    
    // Helper to check if parse succeeded
    bool parse_succeeds(std::string_view sql) {
        auto result = parser.parse(sql);
        return result.has_value();
    }
    
    // Helper to get AST root
    ASTNode* get_ast(std::string_view sql) {
        auto result = parser.parse(sql);
        return result.has_value() ? result.value() : nullptr;
    }
    
    // Helper to check node type
    bool check_node_type(ASTNode* node, NodeType expected) {
        return node && node->node_type == expected;
    }
};

// ============================================================================
// BASIC PARSING TESTS
// ============================================================================

TEST_F(ParserTest, EmptyInput) {
    auto result = parser.parse("");
    EXPECT_FALSE(result.has_value());
    if (!result.has_value()) {
        EXPECT_EQ(result.error().message, "Empty SQL statement");
    }
}

TEST_F(ParserTest, WhitespaceOnlyInput) {
    auto result = parser.parse("   \n\t  ");
    EXPECT_FALSE(result.has_value());
}

TEST_F(ParserTest, InvalidTokens) {
    auto result = parser.parse("@#$%^&");
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// SELECT STATEMENT TESTS
// ============================================================================

TEST_F(ParserTest, SimpleSelectAll) {
    auto* ast = get_ast("SELECT * FROM users");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::SelectStmt);
    
    // Check select list has wildcard
    // AST uses linked list structure: first_child and next_sibling
    auto* first_child = ast->get_first_child();
    ASSERT_NE(first_child, nullptr);
    
    // Assume first child is select_list
    auto* select_list = first_child;
    EXPECT_EQ(select_list->node_type, NodeType::SelectList);
    
    // Second child should be from_clause
    auto* from_clause = select_list->get_next_sibling();
    ASSERT_NE(from_clause, nullptr);
    EXPECT_EQ(from_clause->node_type, NodeType::FromClause);
}

TEST_F(ParserTest, SelectSpecificColumns) {
    auto* ast = get_ast("SELECT id, name, email FROM users");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::SelectStmt);
    
    auto children = ast->get_children();
    auto* select_list = children[0];
    auto select_items = select_list->get_children();
    EXPECT_EQ(select_items.size(), 3); // Three columns
}

TEST_F(ParserTest, SelectWithAlias) {
    auto* ast = get_ast("SELECT id AS user_id, name AS user_name FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto children = ast->get_children();
    auto* select_list = children[0];
    auto select_items = select_list->get_children();
    
    // First item should have alias
    auto* first_item = select_items[0];
    EXPECT_EQ(first_item->node_type, NodeType::AliasExpr);
}

TEST_F(ParserTest, SelectDistinct) {
    auto* ast = get_ast("SELECT DISTINCT name FROM users");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::SelectStmt);
    // Check for DISTINCT flag in SelectStmt data
}

TEST_F(ParserTest, SelectWithWhere) {
    auto* ast = get_ast("SELECT * FROM users WHERE id = 1");
    ASSERT_NE(ast, nullptr);
    
    auto children = ast->get_children();
    ASSERT_GE(children.size(), 3); // select_list, from_clause, where_clause
    
    auto* where_clause = children[2];
    EXPECT_EQ(where_clause->node_type, NodeType::WhereClause);
    
    auto* condition = where_clause->get_children()[0];
    EXPECT_EQ(condition->node_type, NodeType::BinaryExpr);
}

TEST_F(ParserTest, SelectWithComplexWhere) {
    auto* ast = get_ast("SELECT * FROM users WHERE age > 18 AND status = 'active'");
    ASSERT_NE(ast, nullptr);
    
    auto children = ast->get_children();
    auto* where_clause = children[2];
    auto* condition = where_clause->get_children()[0];
    
    // Should be AND expression at top level
    EXPECT_EQ(condition->node_type, NodeType::BinaryExpr);
    // Check operator type is AND
}

TEST_F(ParserTest, SelectWithOrderBy) {
    auto* ast = get_ast("SELECT * FROM users ORDER BY name ASC, age DESC");
    ASSERT_NE(ast, nullptr);
    
    // Find ORDER BY clause
    auto children = ast->get_children();
    ASTNode* order_clause = nullptr;
    for (auto* child : children) {
        if (child->type == NodeType::OrderByClause) {
            order_clause = child;
            break;
        }
    }
    
    ASSERT_NE(order_clause, nullptr);
    auto order_items = order_clause->get_children();
    EXPECT_EQ(order_items.size(), 2); // Two order items
}

TEST_F(ParserTest, SelectWithLimit) {
    auto* ast = get_ast("SELECT * FROM users LIMIT 10");
    ASSERT_NE(ast, nullptr);
    
    // Find LIMIT clause
    auto children = ast->get_children();
    ASTNode* limit_clause = nullptr;
    for (auto* child : children) {
        if (child->type == NodeType::LimitClause) {
            limit_clause = child;
            break;
        }
    }
    
    ASSERT_NE(limit_clause, nullptr);
}

TEST_F(ParserTest, SelectWithLimitOffset) {
    auto* ast = get_ast("SELECT * FROM users LIMIT 10 OFFSET 20");
    ASSERT_NE(ast, nullptr);
    
    // Check both LIMIT and OFFSET are parsed
}

TEST_F(ParserTest, SelectWithGroupBy) {
    auto* ast = get_ast("SELECT department, COUNT(*) FROM employees GROUP BY department");
    ASSERT_NE(ast, nullptr);
    
    // Find GROUP BY clause
    auto children = ast->get_children();
    ASTNode* group_clause = nullptr;
    for (auto* child : children) {
        if (child->type == NodeType::GroupByClause) {
            group_clause = child;
            break;
        }
    }
    
    ASSERT_NE(group_clause, nullptr);
}

TEST_F(ParserTest, SelectWithHaving) {
    auto* ast = get_ast("SELECT department, COUNT(*) FROM employees GROUP BY department HAVING COUNT(*) > 5");
    ASSERT_NE(ast, nullptr);
    
    // Find HAVING clause
    auto children = ast->get_children();
    ASTNode* having_clause = nullptr;
    for (auto* child : children) {
        if (child->type == NodeType::HavingClause) {
            having_clause = child;
            break;
        }
    }
    
    ASSERT_NE(having_clause, nullptr);
}

// ============================================================================
// JOIN TESTS
// ============================================================================

TEST_F(ParserTest, InnerJoin) {
    auto* ast = get_ast("SELECT * FROM users INNER JOIN orders ON users.id = orders.user_id");
    ASSERT_NE(ast, nullptr);
    
    // Check for JOIN node
    auto children = ast->get_children();
    auto* from_clause = children[1];
    auto from_children = from_clause->get_children();
    
    // Should have join expression
    bool has_join = false;
    for (auto* child : from_children) {
        if (child->type == NodeType::JoinExpr) {
            has_join = true;
            break;
        }
    }
    EXPECT_TRUE(has_join);
}

TEST_F(ParserTest, LeftJoin) {
    auto* ast = get_ast("SELECT * FROM users LEFT JOIN orders ON users.id = orders.user_id");
    ASSERT_NE(ast, nullptr);
}

TEST_F(ParserTest, MultipleJoins) {
    auto* ast = get_ast(
        "SELECT * FROM users "
        "JOIN orders ON users.id = orders.user_id "
        "JOIN products ON orders.product_id = products.id"
    );
    ASSERT_NE(ast, nullptr);
}

// ============================================================================
// INSERT STATEMENT TESTS
// ============================================================================

TEST_F(ParserTest, SimpleInsert) {
    auto* ast = get_ast("INSERT INTO users (name, email) VALUES ('John', 'john@example.com')");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::InsertStmt);
    
    auto children = ast->get_children();
    ASSERT_GE(children.size(), 3); // table, columns, values
}

TEST_F(ParserTest, InsertMultipleRows) {
    auto* ast = get_ast(
        "INSERT INTO users (name, email) VALUES "
        "('John', 'john@example.com'), "
        "('Jane', 'jane@example.com')"
    );
    ASSERT_NE(ast, nullptr);
    
    // Check for multiple value lists
}

TEST_F(ParserTest, InsertFromSelect) {
    auto* ast = get_ast("INSERT INTO users_backup SELECT * FROM users");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::InsertStmt);
    
    // Should have SELECT statement as child
}

// ============================================================================
// UPDATE STATEMENT TESTS
// ============================================================================

TEST_F(ParserTest, SimpleUpdate) {
    auto* ast = get_ast("UPDATE users SET name = 'John' WHERE id = 1");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::UpdateStmt);
    
    auto children = ast->get_children();
    ASSERT_GE(children.size(), 3); // table, set_clause, where_clause
}

TEST_F(ParserTest, UpdateMultipleColumns) {
    auto* ast = get_ast("UPDATE users SET name = 'John', email = 'john@new.com' WHERE id = 1");
    ASSERT_NE(ast, nullptr);
    
    // Check SET clause has multiple assignments
}

// ============================================================================
// DELETE STATEMENT TESTS
// ============================================================================

TEST_F(ParserTest, SimpleDelete) {
    auto* ast = get_ast("DELETE FROM users WHERE id = 1");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::DeleteStmt);
}

TEST_F(ParserTest, DeleteAll) {
    auto* ast = get_ast("DELETE FROM users");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::DeleteStmt);
}

// ============================================================================
// CREATE TABLE TESTS
// ============================================================================

TEST_F(ParserTest, CreateTableBasic) {
    auto* ast = get_ast(
        "CREATE TABLE users ("
        "  id INTEGER PRIMARY KEY,"
        "  name VARCHAR(100) NOT NULL,"
        "  email VARCHAR(255) UNIQUE"
        ")"
    );
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::CreateTableStmt);
    
    // Check columns are parsed
}

TEST_F(ParserTest, CreateTableWithConstraints) {
    auto* ast = get_ast(
        "CREATE TABLE orders ("
        "  id INTEGER PRIMARY KEY,"
        "  user_id INTEGER,"
        "  total DECIMAL(10,2),"
        "  FOREIGN KEY (user_id) REFERENCES users(id)"
        ")"
    );
    ASSERT_NE(ast, nullptr);
}

// ============================================================================
// EXPRESSION TESTS (Pratt Parser)
// ============================================================================

TEST_F(ParserTest, BinaryExpressions) {
    auto* ast = get_ast("SELECT 1 + 2 * 3 FROM dual");
    ASSERT_NE(ast, nullptr);
    
    // Check precedence is correct (multiplication before addition)
    auto children = ast->get_children();
    auto* select_list = children[0];
    auto* expr = select_list->get_children()[0];
    
    // Top level should be addition
    EXPECT_EQ(expr->node_type, NodeType::BinaryExpr);
    // Right child should be multiplication
}

TEST_F(ParserTest, ComparisonExpressions) {
    auto* ast = get_ast("SELECT * FROM users WHERE age >= 18 AND age <= 65");
    ASSERT_NE(ast, nullptr);
}

TEST_F(ParserTest, LogicalExpressions) {
    auto* ast = get_ast("SELECT * FROM users WHERE active = true OR (status = 'pending' AND verified = true)");
    ASSERT_NE(ast, nullptr);
    
    // Check logical operator precedence
}

TEST_F(ParserTest, UnaryExpressions) {
    auto* ast = get_ast("SELECT -price, NOT active FROM products");
    ASSERT_NE(ast, nullptr);
}

TEST_F(ParserTest, FunctionCalls) {
    auto* ast = get_ast("SELECT COUNT(*), MAX(price), CONCAT(first_name, ' ', last_name) FROM users");
    ASSERT_NE(ast, nullptr);
    
    // Check function nodes are created
}

TEST_F(ParserTest, CaseExpression) {
    auto* ast = get_ast(
        "SELECT CASE "
        "  WHEN age < 18 THEN 'minor' "
        "  WHEN age >= 65 THEN 'senior' "
        "  ELSE 'adult' "
        "END FROM users"
    );
    ASSERT_NE(ast, nullptr);
}

// ============================================================================
// SUBQUERY TESTS
// ============================================================================

TEST_F(ParserTest, SubqueryInWhere) {
    auto* ast = get_ast("SELECT * FROM users WHERE id IN (SELECT user_id FROM orders)");
    ASSERT_NE(ast, nullptr);
}

TEST_F(ParserTest, SubqueryInFrom) {
    auto* ast = get_ast("SELECT * FROM (SELECT * FROM users WHERE active = true) AS active_users");
    ASSERT_NE(ast, nullptr);
}

TEST_F(ParserTest, CorrelatedSubquery) {
    auto* ast = get_ast(
        "SELECT * FROM users u WHERE EXISTS "
        "(SELECT 1 FROM orders o WHERE o.user_id = u.id)"
    );
    ASSERT_NE(ast, nullptr);
}

// ============================================================================
// LITERAL TESTS
// ============================================================================

TEST_F(ParserTest, IntegerLiterals) {
    auto* ast = get_ast("SELECT 42, -17, 0 FROM dual");
    ASSERT_NE(ast, nullptr);
}

TEST_F(ParserTest, FloatLiterals) {
    auto* ast = get_ast("SELECT 3.14, -2.5, 1.0e10 FROM dual");
    ASSERT_NE(ast, nullptr);
}

TEST_F(ParserTest, StringLiterals) {
    auto* ast = get_ast("SELECT 'hello', \"world\", 'it''s' FROM dual");
    ASSERT_NE(ast, nullptr);
}

TEST_F(ParserTest, BooleanLiterals) {
    auto* ast = get_ast("SELECT TRUE, FALSE, NULL FROM dual");
    ASSERT_NE(ast, nullptr);
}

// ============================================================================
// ERROR RECOVERY TESTS
// ============================================================================

TEST_F(ParserTest, MissingSemicolon) {
    // Parser should handle missing semicolon
    auto* ast = get_ast("SELECT * FROM users");
    ASSERT_NE(ast, nullptr);
}

TEST_F(ParserTest, ExtraSemicolons) {
    auto* ast = get_ast("SELECT * FROM users;;;");
    ASSERT_NE(ast, nullptr);
}

TEST_F(ParserTest, RecoverFromError) {
    // Test error recovery in multi-statement parsing
    auto result = parser.parse_script(
        "SELECT * FROM users;"
        "INVALID SYNTAX HERE;"
        "SELECT * FROM orders;"
    );
    
    // Should fail but provide meaningful error
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// PERFORMANCE TESTS
// ============================================================================

TEST_F(ParserTest, DeeplyNestedExpressions) {
    // Test parser handles deep nesting without stack overflow
    std::string sql = "SELECT ";
    for (int i = 0; i < 100; ++i) {
        sql += "(";
    }
    sql += "1";
    for (int i = 0; i < 100; ++i) {
        sql += ")";
    }
    sql += " FROM dual";
    
    auto* ast = get_ast(sql);
    ASSERT_NE(ast, nullptr);
}

TEST_F(ParserTest, LargeSelectList) {
    // Test parser handles many columns
    std::string sql = "SELECT col1";
    for (int i = 2; i <= 100; ++i) {
        sql += ", col" + std::to_string(i);
    }
    sql += " FROM table1";
    
    auto* ast = get_ast(sql);
    ASSERT_NE(ast, nullptr);
    
    auto children = ast->get_children();
    auto* select_list = children[0];
    EXPECT_EQ(select_list->get_children().size(), 100);
}

// ============================================================================
// MEMORY TESTS
// ============================================================================

TEST_F(ParserTest, MemoryReuse) {
    // Parse multiple statements and check memory is reused
    size_t first_memory = 0;
    
    for (int i = 0; i < 10; ++i) {
        parser.reset();
        auto* ast = get_ast("SELECT * FROM users WHERE id = 1");
        ASSERT_NE(ast, nullptr);
        
        if (i == 0) {
            first_memory = parser.get_memory_used();
        } else {
            // Memory usage should be similar for same query
            EXPECT_NEAR(parser.get_memory_used(), first_memory, first_memory * 0.1);
        }
    }
}

TEST_F(ParserTest, NodeCount) {
    auto* ast = get_ast("SELECT id, name FROM users WHERE age > 18");
    ASSERT_NE(ast, nullptr);
    
    // Should have reasonable number of nodes
    auto node_count = parser.get_node_count();
    EXPECT_GT(node_count, 5);  // At minimum: select, select_list, 2 columns, from, table, where, comparison
    EXPECT_LT(node_count, 50); // Shouldn't explode
}