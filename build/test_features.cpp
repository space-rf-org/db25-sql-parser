#include "db25/parser/parser.hpp"
#include <iostream>

using namespace db25;
using namespace db25::parser;

int main() {
    Parser parser;
    
    std::cout << "\n=== DDL AST Construction ===" << std::endl;
    auto result = parser.parse("CREATE TABLE t (id INT, data JSON, tags TEXT[])");
    std::cout << "CREATE TABLE with types: " << (result.has_value() ? "✅ Builds AST" : "❌ Failed") << std::endl;
    if (result.has_value() && result.value()->child_count > 0) {
        std::cout << "  - Has " << result.value()->child_count << " column definitions" << std::endl;
    }
    
    std::cout << "\n=== Advanced Types ===" << std::endl;
    
    // INTERVAL literal
    result = parser.parse("SELECT INTERVAL '1 day'");
    std::cout << "INTERVAL literal: " << (result.has_value() ? "❓ Parses but may not recognize" : "❌ Failed") << std::endl;
    
    // ARRAY constructor  
    result = parser.parse("SELECT ARRAY[1, 2, 3]");
    std::cout << "ARRAY constructor: " << (result.has_value() ? "❓ Parses but may not recognize" : "❌ Failed") << std::endl;
    
    // JSON in DDL
    result = parser.parse("CREATE TABLE j (data JSON)");
    bool json_recognized = false;
    if (result.has_value() && result.value()->first_child) {
        auto* col = result.value()->first_child;
        if (col->first_child && col->first_child->primary_text.find("JSON") != std::string::npos) {
            json_recognized = true;
        }
    }
    std::cout << "JSON type in DDL: " << (json_recognized ? "✅ Recognized" : "❌ Not recognized as JSON") << std::endl;
    
    std::cout << "\n=== Set Operations ===" << std::endl;
    
    result = parser.parse("SELECT 1 UNION SELECT 2");
    std::cout << "UNION: " << (result.has_value() ? "✅ Works" : "❌ Failed") << std::endl;
    
    result = parser.parse("SELECT 1 INTERSECT SELECT 2");
    std::cout << "INTERSECT: " << (result.has_value() ? "✅ Works" : "❌ Failed") << std::endl;
    
    result = parser.parse("SELECT 1 EXCEPT SELECT 2");
    std::cout << "EXCEPT: " << (result.has_value() ? "✅ Works" : "❌ Failed") << std::endl;
    
    std::cout << "\n=== Semantic Validation ===" << std::endl;
    
    // Test unknown columns - parser should still parse (validation is separate)
    result = parser.parse("SELECT unknown_col FROM t");
    std::cout << "Unknown column: " << (result.has_value() ? "✅ Parses (no validation)" : "❌ Failed") << std::endl;
    
    // Test type mismatch - parser should parse
    result = parser.parse("SELECT 'text' + 123");
    std::cout << "Type mismatch: " << (result.has_value() ? "✅ Parses (no type checking)" : "❌ Failed") << std::endl;
    
    std::cout << "\n=== Transaction Modes ===" << std::endl;
    
    result = parser.parse("BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED");
    std::cout << "ISOLATION LEVEL: " << (result.has_value() ? "❓ May parse partially" : "❌ Failed") << std::endl;
    
    result = parser.parse("START TRANSACTION READ ONLY");
    std::cout << "READ ONLY mode: " << (result.has_value() ? "❓ May parse partially" : "❌ Failed") << std::endl;
    
    return 0;
}
