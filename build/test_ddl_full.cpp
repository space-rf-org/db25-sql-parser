#include "db25/parser/parser.hpp"
#include <iostream>
#include <cassert>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

void test_create_table_full() {
    std::cout << "Testing CREATE TABLE with columns and constraints..." << std::endl;
    
    Parser parser;
    
    // Test CREATE TABLE with columns
    auto result = parser.parse(R"(
        CREATE TABLE users (
            id INTEGER PRIMARY KEY,
            name VARCHAR(100) NOT NULL,
            email TEXT UNIQUE,
            age INTEGER CHECK (age >= 18),
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    )");
    
    assert(result.has_value());
    auto* ast = result.value();
    assert(ast->node_type == NodeType::CreateTableStmt);
    assert(ast->child_count > 0);  // Should have column definitions
    
    // Check first column
    auto* first_col = ast->first_child;
    assert(first_col != nullptr);
    assert(first_col->node_type == NodeType::ColumnDefinition);
    std::cout << "  ✓ Column definitions parsed" << std::endl;
    
    // Test with table constraints
    result = parser.parse(R"(
        CREATE TABLE orders (
            id INTEGER,
            user_id INTEGER,
            product_id INTEGER,
            PRIMARY KEY (id),
            FOREIGN KEY (user_id) REFERENCES users(id)
        )
    )");
    
    assert(result.has_value());
    std::cout << "  ✓ Table constraints parsed" << std::endl;
}

void test_alter_table_full() {
    std::cout << "Testing ALTER TABLE operations..." << std::endl;
    
    Parser parser;
    
    // Test ADD COLUMN
    auto result = parser.parse("ALTER TABLE users ADD COLUMN phone VARCHAR(20)");
    assert(result.has_value());
    auto* ast = result.value();
    assert(ast->node_type == NodeType::AlterTableStmt);
    assert(ast->first_child != nullptr);
    assert(ast->first_child->node_type == NodeType::AlterTableAction);
    std::cout << "  ✓ ADD COLUMN parsed" << std::endl;
    
    // Test DROP COLUMN
    result = parser.parse("ALTER TABLE users DROP COLUMN phone CASCADE");
    assert(result.has_value());
    std::cout << "  ✓ DROP COLUMN parsed" << std::endl;
    
    // Test ALTER COLUMN
    result = parser.parse("ALTER TABLE users ALTER COLUMN age SET DEFAULT 0");
    assert(result.has_value());
    std::cout << "  ✓ ALTER COLUMN parsed" << std::endl;
}

void test_create_index_full() {
    std::cout << "Testing CREATE INDEX with options..." << std::endl;
    
    Parser parser;
    
    // Test CREATE INDEX with columns
    auto result = parser.parse("CREATE INDEX idx_email ON users (email)");
    assert(result.has_value());
    auto* ast = result.value();
    assert(ast->node_type == NodeType::CreateIndexStmt);
    assert(ast->child_count > 0);  // Should have index columns
    std::cout << "  ✓ Index columns parsed" << std::endl;
    
    // Test UNIQUE INDEX
    result = parser.parse("CREATE UNIQUE INDEX idx_username ON users (username DESC)");
    assert(result.has_value());
    std::cout << "  ✓ UNIQUE INDEX parsed" << std::endl;
    
    // Test partial index
    result = parser.parse("CREATE INDEX idx_active ON users (id) WHERE status = 'active'");
    assert(result.has_value());
    std::cout << "  ✓ Partial index with WHERE parsed" << std::endl;
}

void test_create_trigger() {
    std::cout << "Testing CREATE TRIGGER..." << std::endl;
    
    Parser parser;
    
    // Test basic trigger
    auto result = parser.parse(R"(
        CREATE TRIGGER update_timestamp
        BEFORE UPDATE ON users
        FOR EACH ROW
        BEGIN
            UPDATE users SET updated_at = CURRENT_TIMESTAMP WHERE id = NEW.id;
        END
    )");
    
    assert(result.has_value());
    auto* ast = result.value();
    assert(ast->node_type == NodeType::CreateTriggerStmt);
    std::cout << "  ✓ CREATE TRIGGER parsed" << std::endl;
    
    // Test AFTER INSERT trigger
    result = parser.parse(R"(
        CREATE TRIGGER log_insert
        AFTER INSERT ON orders
        FOR EACH ROW
        INSERT INTO audit_log VALUES (NEW.id, 'INSERT', CURRENT_TIMESTAMP)
    )");
    
    assert(result.has_value());
    std::cout << "  ✓ AFTER INSERT trigger parsed" << std::endl;
}

void test_create_schema() {
    std::cout << "Testing CREATE SCHEMA..." << std::endl;
    
    Parser parser;
    
    // Test basic schema
    auto result = parser.parse("CREATE SCHEMA myschema");
    assert(result.has_value());
    auto* ast = result.value();
    assert(ast->node_type == NodeType::CreateSchemaStmt);
    std::cout << "  ✓ CREATE SCHEMA parsed" << std::endl;
    
    // Test IF NOT EXISTS
    result = parser.parse("CREATE SCHEMA IF NOT EXISTS analytics");
    assert(result.has_value());
    std::cout << "  ✓ CREATE SCHEMA IF NOT EXISTS parsed" << std::endl;
}

int main() {
    std::cout << "\n=== Testing Full DDL Implementations ===" << std::endl;
    
    try {
        test_create_table_full();
        test_alter_table_full();
        test_create_index_full();
        test_create_trigger();
        test_create_schema();
        
        std::cout << "\n✅ All DDL tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
