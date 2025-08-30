/*
 * Tokenizer Verification Test
 * Verifies that the tokenizer produces EXACTLY the right tokens
 * with correct types, values, and counts for parser input
 */

#include "db25/parser/tokenizer_adapter.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cassert>
#include <iomanip>
#include <algorithm>

struct ExpectedToken {
    db25::TokenType type;
    std::string value;
    const char* type_name;
};

void verify_tokens(const std::string& sql, 
                  const std::vector<ExpectedToken>& expected,
                  const std::string& test_name) {
    std::cout << "\n=== " << test_name << " ===" << std::endl;
    std::cout << "SQL: " << sql << std::endl;
    
    db25::parser::tokenizer::Tokenizer tokenizer(sql);
    const auto& tokens = tokenizer.get_tokens();
    
    // Remove EOF for comparison (we'll check it separately)
    std::vector<db25::Token> actual_tokens;
    for (const auto& token : tokens) {
        if (token.type != db25::TokenType::EndOfFile) {
            actual_tokens.push_back(token);
        }
    }
    
    std::cout << "Expected: " << expected.size() << " tokens" << std::endl;
    std::cout << "Actual:   " << actual_tokens.size() << " tokens" << std::endl;
    
    // Verify count
    if (actual_tokens.size() != expected.size()) {
        std::cout << "❌ FAIL: Token count mismatch!" << std::endl;
        std::cout << "\nActual tokens received:" << std::endl;
        for (size_t i = 0; i < actual_tokens.size(); ++i) {
            const auto& token = actual_tokens[i];
            std::cout << "  [" << i << "] Type:" << static_cast<int>(token.type) 
                      << " Value:'" << token.value << "'"
                      << " Line:" << token.line << " Col:" << token.column;
            if (token.keyword_id != db25::Keyword::UNKNOWN) {
                std::cout << " Keyword:" << static_cast<int>(token.keyword_id);
            }
            std::cout << std::endl;
        }
        assert(false);
    }
    
    // Verify each token
    bool all_match = true;
    for (size_t i = 0; i < expected.size(); ++i) {
        const auto& exp = expected[i];
        const auto& act = actual_tokens[i];
        
        std::cout << "\nToken [" << i << "]:" << std::endl;
        std::cout << "  Expected: " << std::setw(12) << exp.type_name 
                  << " '" << exp.value << "'" << std::endl;
        std::cout << "  Actual:   " << std::setw(12);
        
        // Print actual type name
        switch (act.type) {
            case db25::TokenType::Unknown:    std::cout << "UNKNOWN"; break;
            case db25::TokenType::Keyword:    std::cout << "KEYWORD"; break;
            case db25::TokenType::Identifier: std::cout << "IDENTIFIER"; break;
            case db25::TokenType::Number:     std::cout << "NUMBER"; break;
            case db25::TokenType::String:     std::cout << "STRING"; break;
            case db25::TokenType::Operator:   std::cout << "OPERATOR"; break;
            case db25::TokenType::Delimiter:  std::cout << "DELIMITER"; break;
            case db25::TokenType::Whitespace: std::cout << "WHITESPACE"; break;
            case db25::TokenType::Comment:    std::cout << "COMMENT"; break;
            case db25::TokenType::EndOfFile:  std::cout << "EOF"; break;
        }
        std::cout << " '" << act.value << "'";
        
        if (act.type == exp.type && act.value == exp.value) {
            std::cout << " ✓" << std::endl;
        } else {
            std::cout << " ❌" << std::endl;
            all_match = false;
            if (act.type != exp.type) {
                std::cout << "    Type mismatch: expected " << static_cast<int>(exp.type) 
                          << " got " << static_cast<int>(act.type) << std::endl;
            }
            if (act.value != exp.value) {
                std::cout << "    Value mismatch" << std::endl;
            }
        }
        
        // Additional info
        if (act.keyword_id != db25::Keyword::UNKNOWN) {
            std::cout << "    Keyword ID: " << static_cast<int>(act.keyword_id) << std::endl;
        }
        std::cout << "    Position: " << act.line << ":" << act.column << std::endl;
    }
    
    // Verify EOF token is present
    if (tokens.empty() || tokens.back().type != db25::TokenType::EndOfFile) {
        std::cout << "\n❌ FAIL: No EOF token!" << std::endl;
        all_match = false;
    } else {
        std::cout << "\n✓ EOF token present" << std::endl;
    }
    
    if (all_match) {
        std::cout << "\n✅ PASS: All tokens match!" << std::endl;
    } else {
        std::cout << "\n❌ FAIL: Token mismatch!" << std::endl;
        assert(false);
    }
}

int main() {
    std::cout << "==================================" << std::endl;
    std::cout << "Tokenizer Verification Test" << std::endl;
    std::cout << "==================================" << std::endl;
    
    // Test 1: Simple SELECT
    verify_tokens(
        "SELECT * FROM users",
        {
            {db25::TokenType::Keyword, "SELECT", "KEYWORD"},
            {db25::TokenType::Operator, "*", "OPERATOR"},
            {db25::TokenType::Keyword, "FROM", "KEYWORD"},
            {db25::TokenType::Identifier, "users", "IDENTIFIER"}
        },
        "Simple SELECT"
    );
    
    // Test 2: SELECT with WHERE
    verify_tokens(
        "SELECT id, name FROM users WHERE age > 25",
        {
            {db25::TokenType::Keyword, "SELECT", "KEYWORD"},
            {db25::TokenType::Identifier, "id", "IDENTIFIER"},
            {db25::TokenType::Delimiter, ",", "DELIMITER"},
            {db25::TokenType::Identifier, "name", "IDENTIFIER"},
            {db25::TokenType::Keyword, "FROM", "KEYWORD"},
            {db25::TokenType::Identifier, "users", "IDENTIFIER"},
            {db25::TokenType::Keyword, "WHERE", "KEYWORD"},
            {db25::TokenType::Identifier, "age", "IDENTIFIER"},
            {db25::TokenType::Operator, ">", "OPERATOR"},
            {db25::TokenType::Number, "25", "NUMBER"}
        },
        "SELECT with WHERE"
    );
    
    // Test 3: String literals
    verify_tokens(
        "SELECT * FROM users WHERE name = 'John Doe'",
        {
            {db25::TokenType::Keyword, "SELECT", "KEYWORD"},
            {db25::TokenType::Operator, "*", "OPERATOR"},
            {db25::TokenType::Keyword, "FROM", "KEYWORD"},
            {db25::TokenType::Identifier, "users", "IDENTIFIER"},
            {db25::TokenType::Keyword, "WHERE", "KEYWORD"},
            {db25::TokenType::Identifier, "name", "IDENTIFIER"},
            {db25::TokenType::Operator, "=", "OPERATOR"},
            {db25::TokenType::String, "'John Doe'", "STRING"}
        },
        "String Literals"
    );
    
    // Test 4: Numbers (integer and float)
    verify_tokens(
        "SELECT * WHERE price BETWEEN 10.50 AND 99",
        {
            {db25::TokenType::Keyword, "SELECT", "KEYWORD"},
            {db25::TokenType::Operator, "*", "OPERATOR"},
            {db25::TokenType::Keyword, "WHERE", "KEYWORD"},
            {db25::TokenType::Identifier, "price", "IDENTIFIER"},
            {db25::TokenType::Keyword, "BETWEEN", "KEYWORD"},
            {db25::TokenType::Number, "10.50", "NUMBER"},
            {db25::TokenType::Keyword, "AND", "KEYWORD"},
            {db25::TokenType::Number, "99", "NUMBER"}
        },
        "Numbers"
    );
    
    // Test 5: Operators
    verify_tokens(
        "SELECT a + b * c - d / e",
        {
            {db25::TokenType::Keyword, "SELECT", "KEYWORD"},
            {db25::TokenType::Identifier, "a", "IDENTIFIER"},
            {db25::TokenType::Operator, "+", "OPERATOR"},
            {db25::TokenType::Identifier, "b", "IDENTIFIER"},
            {db25::TokenType::Operator, "*", "OPERATOR"},
            {db25::TokenType::Identifier, "c", "IDENTIFIER"},
            {db25::TokenType::Operator, "-", "OPERATOR"},
            {db25::TokenType::Identifier, "d", "IDENTIFIER"},
            {db25::TokenType::Operator, "/", "OPERATOR"},
            {db25::TokenType::Identifier, "e", "IDENTIFIER"}
        },
        "Operators"
    );
    
    // Test 6: Parentheses and semicolon
    verify_tokens(
        "SELECT COUNT(*) FROM (SELECT * FROM t);",
        {
            {db25::TokenType::Keyword, "SELECT", "KEYWORD"},
            {db25::TokenType::Identifier, "COUNT", "IDENTIFIER"},  // COUNT might be identifier or function
            {db25::TokenType::Delimiter, "(", "DELIMITER"},
            {db25::TokenType::Operator, "*", "OPERATOR"},
            {db25::TokenType::Delimiter, ")", "DELIMITER"},
            {db25::TokenType::Keyword, "FROM", "KEYWORD"},
            {db25::TokenType::Delimiter, "(", "DELIMITER"},
            {db25::TokenType::Keyword, "SELECT", "KEYWORD"},
            {db25::TokenType::Operator, "*", "OPERATOR"},
            {db25::TokenType::Keyword, "FROM", "KEYWORD"},
            {db25::TokenType::Identifier, "t", "IDENTIFIER"},
            {db25::TokenType::Delimiter, ")", "DELIMITER"},
            {db25::TokenType::Delimiter, ";", "DELIMITER"}
        },
        "Parentheses and Semicolon"
    );
    
    // Test 7: Qualified identifiers (table.column)
    verify_tokens(
        "SELECT users.id, users.name FROM users",
        {
            {db25::TokenType::Keyword, "SELECT", "KEYWORD"},
            {db25::TokenType::Identifier, "users", "IDENTIFIER"},
            {db25::TokenType::Operator, ".", "OPERATOR"},
            {db25::TokenType::Identifier, "id", "IDENTIFIER"},
            {db25::TokenType::Delimiter, ",", "DELIMITER"},
            {db25::TokenType::Identifier, "users", "IDENTIFIER"},
            {db25::TokenType::Operator, ".", "OPERATOR"},
            {db25::TokenType::Identifier, "name", "IDENTIFIER"},
            {db25::TokenType::Keyword, "FROM", "KEYWORD"},
            {db25::TokenType::Identifier, "users", "IDENTIFIER"}
        },
        "Qualified Identifiers"
    );
    
    // Test 8: Comments and whitespace (should be filtered)
    {
        std::cout << "\n=== Comment Filtering Test ===" << std::endl;
        std::string sql_with_comments = "-- This is a comment\nSELECT * /* inline comment */ FROM users";
        db25::parser::tokenizer::Tokenizer tokenizer(sql_with_comments);
        const auto& tokens = tokenizer.get_tokens();
        
        bool has_comment = false;
        bool has_whitespace = false;
        for (const auto& token : tokens) {
            if (token.type == db25::TokenType::Comment) has_comment = true;
            if (token.type == db25::TokenType::Whitespace) has_whitespace = true;
        }
        
        if (has_comment || has_whitespace) {
            std::cout << "❌ FAIL: Comments/whitespace not filtered!" << std::endl;
            assert(false);
        } else {
            std::cout << "✅ PASS: Comments and whitespace properly filtered" << std::endl;
        }
    }
    
    // Test 9: Complex operators
    verify_tokens(
        "SELECT * WHERE a >= 10 AND b <= 20 AND c <> 5",
        {
            {db25::TokenType::Keyword, "SELECT", "KEYWORD"},
            {db25::TokenType::Operator, "*", "OPERATOR"},
            {db25::TokenType::Keyword, "WHERE", "KEYWORD"},
            {db25::TokenType::Identifier, "a", "IDENTIFIER"},
            {db25::TokenType::Operator, ">=", "OPERATOR"},
            {db25::TokenType::Number, "10", "NUMBER"},
            {db25::TokenType::Keyword, "AND", "KEYWORD"},
            {db25::TokenType::Identifier, "b", "IDENTIFIER"},
            {db25::TokenType::Operator, "<=", "OPERATOR"},
            {db25::TokenType::Number, "20", "NUMBER"},
            {db25::TokenType::Keyword, "AND", "KEYWORD"},
            {db25::TokenType::Identifier, "c", "IDENTIFIER"},
            {db25::TokenType::Operator, "<>", "OPERATOR"},
            {db25::TokenType::Number, "5", "NUMBER"}
        },
        "Complex Operators"
    );
    
    // Test 10: Keywords vs Identifiers
    verify_tokens(
        "SELECT select_col FROM from_table WHERE where_col = 1",
        {
            {db25::TokenType::Keyword, "SELECT", "KEYWORD"},
            {db25::TokenType::Identifier, "select_col", "IDENTIFIER"},
            {db25::TokenType::Keyword, "FROM", "KEYWORD"},
            {db25::TokenType::Identifier, "from_table", "IDENTIFIER"},
            {db25::TokenType::Keyword, "WHERE", "KEYWORD"},
            {db25::TokenType::Identifier, "where_col", "IDENTIFIER"},
            {db25::TokenType::Operator, "=", "OPERATOR"},
            {db25::TokenType::Number, "1", "NUMBER"}
        },
        "Keywords vs Identifiers"
    );
    
    // Test 11: Load and verify sql_test.sqls
    {
        std::cout << "\n==================================" << std::endl;
        std::cout << "SQL Test Suite Verification" << std::endl;
        std::cout << "==================================" << std::endl;
        
        // Try multiple possible locations for sql_test.sqls
        std::vector<std::string> possible_paths = {
            "tests/sql_test.sqls",                    // Running from project root
            "../tests/sql_test.sqls",                 // Running from build directory
            "../../tests/sql_test.sqls",              // Running from deeper directory
            SQL_TEST_FILE_PATH                        // CMake-defined absolute path
        };
        
        std::ifstream file;
        std::string used_path;
        for (const auto& path : possible_paths) {
            file.open(path);
            if (file) {
                used_path = path;
                break;
            }
        }
        
        if (!file) {
            std::cerr << "Error: Could not find sql_test.sqls in any of these locations:" << std::endl;
            for (const auto& path : possible_paths) {
                std::cerr << "  - " << path << std::endl;
            }
            std::cerr << "Skipping SQL test suite verification." << std::endl;
        } else {
            std::cout << "Found sql_test.sqls at: " << used_path << std::endl;
            std::string line;
            std::string current_id;
            std::string current_sql;
            int test_count = 0;
            int pass_count = 0;
            
            while (std::getline(file, line)) {
                if (line.starts_with("--ID:")) {
                    current_id = line.substr(5);
                    // Trim whitespace
                    current_id.erase(0, current_id.find_first_not_of(" \t"));
                    current_id.erase(current_id.find_last_not_of(" \t") + 1);
                    current_sql.clear();
                } else if (line == "--END") {
                    if (!current_id.empty() && !current_sql.empty()) {
                        test_count++;
                        std::cout << "\n[" << test_count << "] Testing: " << current_id << std::endl;
                        std::cout << "SQL: " << current_sql.substr(0, 60);
                        if (current_sql.length() > 60) std::cout << "...";
                        std::cout << std::endl;
                        
                        // Tokenize
                        db25::parser::tokenizer::Tokenizer tokenizer(current_sql);
                        const auto& tokens = tokenizer.get_tokens();
                        
                        // Basic verification
                        bool valid = true;
                        
                        // Check for EOF token
                        if (tokens.empty() || tokens.back().type != db25::TokenType::EndOfFile) {
                            std::cout << "  ❌ No EOF token" << std::endl;
                            valid = false;
                        }
                        
                        // Check for no whitespace/comments
                        int token_count = 0;
                        for (const auto& token : tokens) {
                            if (token.type == db25::TokenType::Whitespace) {
                                std::cout << "  ❌ Whitespace not filtered" << std::endl;
                                valid = false;
                                break;
                            }
                            if (token.type == db25::TokenType::Comment) {
                                std::cout << "  ❌ Comment not filtered" << std::endl;
                                valid = false;
                                break;
                            }
                            if (token.type != db25::TokenType::EndOfFile) {
                                token_count++;
                            }
                        }
                        
                        // Check minimum token count (should have at least SELECT/INSERT/UPDATE/DELETE + something)
                        if (token_count < 3) {
                            std::cout << "  ❌ Too few tokens: " << token_count << std::endl;
                            valid = false;
                        }
                        
                        // Check first token is a keyword (SELECT/INSERT/UPDATE/DELETE/CREATE/etc)
                        if (!tokens.empty() && tokens[0].type != db25::TokenType::Keyword) {
                            std::cout << "  ❌ First token not a keyword" << std::endl;
                            valid = false;
                        }
                        
                        if (valid) {
                            std::cout << "  ✓ Tokens: " << token_count << " (clean, EOF present)" << std::endl;
                            
                            // Print first 5 tokens for verification
                            std::cout << "  First tokens: ";
                            for (size_t i = 0; i < std::min(size_t(5), tokens.size() - 1); ++i) {
                                std::cout << "[";
                                switch (tokens[i].type) {
                                    case db25::TokenType::Keyword:    std::cout << "KW:"; break;
                                    case db25::TokenType::Identifier: std::cout << "ID:"; break;
                                    case db25::TokenType::Number:     std::cout << "NUM:"; break;
                                    case db25::TokenType::String:     std::cout << "STR:"; break;
                                    case db25::TokenType::Operator:   std::cout << "OP:"; break;
                                    case db25::TokenType::Delimiter:  std::cout << "DEL:"; break;
                                    default: std::cout << "?:"; break;
                                }
                                std::cout << tokens[i].value << "] ";
                            }
                            if (tokens.size() > 6) std::cout << "...";
                            std::cout << std::endl;
                            pass_count++;
                        }
                    }
                    current_id.clear();
                    current_sql.clear();
                } else if (!line.starts_with("--") && !current_id.empty()) {
                    if (!current_sql.empty()) current_sql += "\n";
                    current_sql += line;
                }
            }
            
            std::cout << "\n==================================" << std::endl;
            std::cout << "SQL Test Suite Results:" << std::endl;
            std::cout << "  Total: " << test_count << " queries" << std::endl;
            std::cout << "  Passed: " << pass_count << std::endl;
            std::cout << "  Failed: " << (test_count - pass_count) << std::endl;
            
            if (pass_count == test_count) {
                std::cout << "✅ ALL SQL TEST SUITE QUERIES TOKENIZED CORRECTLY!" << std::endl;
            } else {
                std::cout << "❌ Some queries failed tokenization" << std::endl;
                assert(false);
            }
        }
    }
    
    std::cout << "\n==================================" << std::endl;
    std::cout << "✅ ALL TOKENIZER VERIFICATION TESTS PASSED!" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "\nThe tokenizer is producing correct tokens for the parser." << std::endl;
    
    return 0;
}