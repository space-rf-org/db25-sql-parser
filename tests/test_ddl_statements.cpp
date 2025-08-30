/*
 * DB25 Parser - DDL Statement Tests
 * Testing CREATE INDEX/VIEW, DROP, ALTER, TRUNCATE statements
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class DDLTest : public ::testing::Test {
protected:
    Parser parser;
    
    void SetUp() override {
        parser.reset();
    }
    
    ASTNode* parse(const std::string& sql) {
        auto result = parser.parse(sql);
        if (!result.has_value()) {
            return nullptr;
        }
        return result.value();
    }
};

// ============================================================================
// CREATE INDEX
// ============================================================================

TEST_F(DDLTest, CreateIndex) {
    auto* ast = parse("CREATE INDEX idx_users_email ON users (email)");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::CreateIndexStmt);
    EXPECT_EQ(ast->primary_text, "idx_users_email");
    EXPECT_EQ(ast->schema_name, "users");  // Table name stored in schema_name
}

TEST_F(DDLTest, CreateUniqueIndex) {
    auto* ast = parse("CREATE UNIQUE INDEX idx_users_email ON users (email)");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::CreateIndexStmt);
    EXPECT_EQ(ast->primary_text, "idx_users_email");
    EXPECT_EQ(ast->schema_name, "users");  // Table name stored in schema_name
    EXPECT_TRUE(ast->semantic_flags & 0x02);  // UNIQUE flag
}

TEST_F(DDLTest, CreateIndexIfNotExists) {
    auto* ast = parse("CREATE INDEX IF NOT EXISTS idx_users_email ON users (email)");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::CreateIndexStmt);
    EXPECT_TRUE(ast->semantic_flags & 0x01);  // IF NOT EXISTS flag
}

TEST_F(DDLTest, CreateIndexWithWhere) {
    auto* ast = parse("CREATE INDEX idx_active_users ON users (email) WHERE active = true");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::CreateIndexStmt);
    EXPECT_EQ(ast->primary_text, "idx_active_users");
}

// ============================================================================
// CREATE VIEW
// ============================================================================

TEST_F(DDLTest, CreateView) {
    auto* ast = parse("CREATE VIEW active_users AS SELECT * FROM users WHERE active = true");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::CreateViewStmt);
    EXPECT_EQ(ast->primary_text, "active_users");
    
    // Should have SELECT as child
    ASSERT_NE(ast->first_child, nullptr);
    EXPECT_EQ(ast->first_child->node_type, NodeType::SelectStmt);
}

TEST_F(DDLTest, CreateOrReplaceView) {
    auto* ast = parse("CREATE OR REPLACE VIEW active_users AS SELECT * FROM users");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::CreateViewStmt);
    EXPECT_EQ(ast->primary_text, "active_users");
}

TEST_F(DDLTest, CreateViewWithColumns) {
    auto* ast = parse("CREATE VIEW user_summary (id, name, total) AS SELECT id, name, COUNT(*) FROM users GROUP BY id, name");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::CreateViewStmt);
    EXPECT_EQ(ast->primary_text, "user_summary");
}

// ============================================================================
// DROP STATEMENTS
// ============================================================================

TEST_F(DDLTest, DropTable) {
    auto* ast = parse("DROP TABLE users");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::DropStmt);
    EXPECT_EQ(ast->primary_text, "users");
    EXPECT_TRUE(ast->semantic_flags & 0x10);  // DROP TABLE flag
}

TEST_F(DDLTest, DropTableIfExists) {
    auto* ast = parse("DROP TABLE IF EXISTS users");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::DropStmt);
    EXPECT_EQ(ast->primary_text, "users");
    EXPECT_TRUE(ast->semantic_flags & 0x01);  // IF EXISTS flag
    EXPECT_TRUE(ast->semantic_flags & 0x10);  // DROP TABLE flag
}

TEST_F(DDLTest, DropTableCascade) {
    auto* ast = parse("DROP TABLE users CASCADE");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::DropStmt);
    EXPECT_TRUE(ast->semantic_flags & 0x04);  // CASCADE flag
}

TEST_F(DDLTest, DropIndex) {
    auto* ast = parse("DROP INDEX idx_users_email");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::DropStmt);
    EXPECT_EQ(ast->primary_text, "idx_users_email");
    EXPECT_TRUE(ast->semantic_flags & 0x20);  // DROP INDEX flag
}

TEST_F(DDLTest, DropView) {
    auto* ast = parse("DROP VIEW active_users");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::DropStmt);
    EXPECT_EQ(ast->primary_text, "active_users");
    EXPECT_TRUE(ast->semantic_flags & 0x30);  // DROP VIEW flag
}

// ============================================================================
// ALTER TABLE
// ============================================================================

TEST_F(DDLTest, AlterTableAddColumn) {
    auto* ast = parse("ALTER TABLE users ADD COLUMN age INT");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::AlterTableStmt);
    EXPECT_EQ(ast->primary_text, "users");
    // Check AlterTableAction child node
    ASSERT_NE(ast->first_child, nullptr);
    EXPECT_EQ(ast->first_child->node_type, NodeType::AlterTableAction);
    EXPECT_EQ(std::string(ast->first_child->primary_text), "ADD");
}

TEST_F(DDLTest, AlterTableDropColumn) {
    auto* ast = parse("ALTER TABLE users DROP COLUMN age");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::AlterTableStmt);
    EXPECT_EQ(ast->primary_text, "users");
    // Check AlterTableAction child node
    ASSERT_NE(ast->first_child, nullptr);
    EXPECT_EQ(ast->first_child->node_type, NodeType::AlterTableAction);
    EXPECT_EQ(std::string(ast->first_child->primary_text), "DROP");
}

TEST_F(DDLTest, AlterTableRename) {
    auto* ast = parse("ALTER TABLE users RENAME TO customers");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::AlterTableStmt);
    EXPECT_EQ(ast->primary_text, "users");
    // Check AlterTableAction child node
    ASSERT_NE(ast->first_child, nullptr);
    EXPECT_EQ(ast->first_child->node_type, NodeType::AlterTableAction);
    EXPECT_EQ(std::string(ast->first_child->primary_text), "RENAME");
}

TEST_F(DDLTest, AlterTableRenameColumn) {
    auto* ast = parse("ALTER TABLE users RENAME COLUMN email TO email_address");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::AlterTableStmt);
    EXPECT_EQ(ast->primary_text, "users");
    // Check AlterTableAction child node
    ASSERT_NE(ast->first_child, nullptr);
    EXPECT_EQ(ast->first_child->node_type, NodeType::AlterTableAction);
    EXPECT_EQ(std::string(ast->first_child->primary_text), "RENAME");
}

TEST_F(DDLTest, AlterTableAlterColumn) {
    auto* ast = parse("ALTER TABLE users ALTER COLUMN age SET DEFAULT 0");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::AlterTableStmt);
    EXPECT_EQ(ast->primary_text, "users");
    // Check AlterTableAction child node
    ASSERT_NE(ast->first_child, nullptr);
    EXPECT_EQ(ast->first_child->node_type, NodeType::AlterTableAction);
    EXPECT_EQ(std::string(ast->first_child->primary_text), "ALTER");
}

// ============================================================================
// TRUNCATE
// ============================================================================

TEST_F(DDLTest, TruncateTable) {
    auto* ast = parse("TRUNCATE TABLE users");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::TruncateStmt);
    EXPECT_EQ(ast->primary_text, "users");
}

TEST_F(DDLTest, TruncateTableNoKeyword) {
    auto* ast = parse("TRUNCATE users");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::TruncateStmt);
    EXPECT_EQ(ast->primary_text, "users");
}

TEST_F(DDLTest, TruncateTableCascade) {
    auto* ast = parse("TRUNCATE TABLE users CASCADE");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::TruncateStmt);
    EXPECT_TRUE(ast->semantic_flags & 0x04);  // CASCADE flag
}

// ============================================================================
// CREATE TABLE (Existing, verify not broken)
// ============================================================================

TEST_F(DDLTest, CreateTableStillWorks) {
    auto* ast = parse("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(255))");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::CreateTableStmt);
    EXPECT_EQ(ast->primary_text, "users");
}

TEST_F(DDLTest, CreateTableIfNotExists) {
    auto* ast = parse("CREATE TABLE IF NOT EXISTS users (id INT)");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::CreateTableStmt);
    EXPECT_EQ(ast->primary_text, "users");
    EXPECT_TRUE(ast->semantic_flags & 0x01);  // IF NOT EXISTS flag
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}