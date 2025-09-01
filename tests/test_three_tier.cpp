#include <iostream>
#include <memory>
#include "db25/parser/parser.hpp"
#include "simd_tokenizer.hpp"

void test_create(const std::string& sql) {
    std::cout << "Testing: " << sql << std::endl;
    
    auto tokenizer = std::make_unique<db25::tokenizer::SIMDTokenizer>();
    tokenizer->tokenize(sql);
    
    db25::parser::Parser parser(tokenizer.get());
    auto ast = parser.parse_script();
    
    if (ast && ast->first_child) {
        auto* create = ast->first_child;
        std::cout << "  Type: " << static_cast<int>(create->type) << std::endl;
        std::cout << "  Flags: 0x" << std::hex << create->semantic_flags << std::dec << std::endl;
        if (create->primary_text) {
            std::cout << "  Name: " << create->primary_text << std::endl;
        }
        if (create->schema_name) {
            std::cout << "  Schema/Table: " << create->schema_name << std::endl;
        }
    } else {
        std::cout << "  FAILED TO PARSE" << std::endl;
    }
    std::cout << std::endl;
}

int main() {
    // Test TEMPORARY TABLE (should set flag 0x08)
    test_create("CREATE TEMPORARY TABLE temp_users (id INT);");
    test_create("CREATE TEMP TABLE temp_data (value TEXT);");
    
    // Test UNIQUE INDEX (should set flag 0x02)
    test_create("CREATE UNIQUE INDEX idx_unique ON users(email);");
    
    // Test IF NOT EXISTS (should set flag 0x01)
    test_create("CREATE TABLE IF NOT EXISTS users (id INT);");
    test_create("CREATE INDEX IF NOT EXISTS idx_name ON users(name);");
    
    // Test OR REPLACE VIEW (should set flag 0x04)
    test_create("CREATE OR REPLACE VIEW user_view AS SELECT * FROM users;");
    
    return 0;
}
