/*
 * DB25 Parser - Basic Tests for Initial Implementation
 * Simplified tests to get started with TDD
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class BasicParserTest : public ::testing::Test {
protected:
    Parser parser;
    
    void SetUp() override {
        parser.reset();
    }
};

// Test 1: Empty input should return error
TEST_F(BasicParserTest, EmptyInput) {
    auto result = parser.parse("");
    EXPECT_FALSE(result.has_value());
    if (!result.has_value()) {
        EXPECT_NE(result.error().message.find("Empty"), std::string::npos);
    }
}

// Test 2: Simple SELECT should parse
TEST_F(BasicParserTest, SimpleSelect) {
    auto result = parser.parse("SELECT * FROM users");
    EXPECT_TRUE(result.has_value()) << "Failed to parse simple SELECT";
    
    if (result.has_value()) {
        auto* ast = result.value();
        ASSERT_NE(ast, nullptr);
        EXPECT_EQ(ast->node_type, NodeType::SelectStmt);
    }
}

// Test 3: Invalid SQL should fail
TEST_F(BasicParserTest, InvalidSQL) {
    auto result = parser.parse("INVALID SQL");
    EXPECT_FALSE(result.has_value());
}

// Test 4: SELECT with columns
TEST_F(BasicParserTest, SelectWithColumns) {
    auto result = parser.parse("SELECT id, name FROM users");
    EXPECT_TRUE(result.has_value());
    
    if (result.has_value()) {
        auto* ast = result.value();
        ASSERT_NE(ast, nullptr);
        EXPECT_EQ(ast->node_type, NodeType::SelectStmt);
    }
}

// Test 5: SELECT with WHERE
TEST_F(BasicParserTest, SelectWithWhere) {
    auto result = parser.parse("SELECT * FROM users WHERE id = 1");
    EXPECT_TRUE(result.has_value());
    
    if (result.has_value()) {
        auto* ast = result.value();
        ASSERT_NE(ast, nullptr);
        EXPECT_EQ(ast->node_type, NodeType::SelectStmt);
    }
}

// Test 6: INSERT statement
TEST_F(BasicParserTest, SimpleInsert) {
    auto result = parser.parse("INSERT INTO users (name) VALUES ('John')");
    EXPECT_TRUE(result.has_value());
    
    if (result.has_value()) {
        auto* ast = result.value();
        ASSERT_NE(ast, nullptr);
        EXPECT_EQ(ast->node_type, NodeType::InsertStmt);
    }
}

// Test 7: UPDATE statement
TEST_F(BasicParserTest, SimpleUpdate) {
    auto result = parser.parse("UPDATE users SET name = 'John' WHERE id = 1");
    EXPECT_TRUE(result.has_value());
    
    if (result.has_value()) {
        auto* ast = result.value();
        ASSERT_NE(ast, nullptr);
        EXPECT_EQ(ast->node_type, NodeType::UpdateStmt);
    }
}

// Test 8: DELETE statement  
TEST_F(BasicParserTest, SimpleDelete) {
    auto result = parser.parse("DELETE FROM users WHERE id = 1");
    EXPECT_TRUE(result.has_value());
    
    if (result.has_value()) {
        auto* ast = result.value();
        ASSERT_NE(ast, nullptr);
        EXPECT_EQ(ast->node_type, NodeType::DeleteStmt);
    }
}

// Test 9: Memory is managed properly
TEST_F(BasicParserTest, MemoryManagement) {
    auto result = parser.parse("SELECT * FROM users");
    EXPECT_TRUE(result.has_value());
    
    size_t memory_used = parser.get_memory_used();
    EXPECT_GT(memory_used, 0);
    
    // Parse another query after reset
    parser.reset();
    result = parser.parse("SELECT id FROM products");
    EXPECT_TRUE(result.has_value());
    
    // Memory should be reused
    size_t new_memory_used = parser.get_memory_used();
    EXPECT_GT(new_memory_used, 0);
}

// Test 10: Node count tracking
TEST_F(BasicParserTest, NodeCount) {
    auto result = parser.parse("SELECT * FROM users");
    EXPECT_TRUE(result.has_value());
    
    size_t node_count = parser.get_node_count();
    EXPECT_GT(node_count, 0) << "Should have created at least one node";
    EXPECT_LT(node_count, 100) << "Simple query shouldn't create too many nodes";
}