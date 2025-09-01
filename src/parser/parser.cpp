/*
 * Copyright (c) 2024 Space-RF.org
 * Copyright (c) 2024 Chiradip Mandal
 * 
 * DB25 SQL Parser - Implementation
 */

#include "db25/parser/parser.hpp"
#include <cstdlib>  // for std::abort
#include <cstring>  // for std::memcpy
#include <iostream> // for std::cerr (debug output)
#include <stdexcept> // for std::runtime_error

namespace db25::parser {

// ========== Construction ==========

Parser::Parser(const ParserConfig& config)
    : config_(config)
    , arena_(64 * 1024)  // 64KB initial size
    // input_ is default-initialized (empty string_view)
    , current_depth_(0)
    , next_node_id_(1)
    , tokenizer_(nullptr)
    , current_token_(nullptr)
    , peek_token_(nullptr)
    , parenthesis_depth_(0)  // Initialize parenthesis tracking
    , strict_mode_(true) {   // Enable strict validation by default
}

Parser::~Parser() {
    delete tokenizer_;  // Safe even if nullptr
}

// ========== Helper Methods ==========

// Get context hint for current parsing position
uint8_t Parser::get_context_hint() const {
    // Convert current context to a hint value that can be stored in AST node
    // This helps semantic analyzer understand where the identifier appeared
    return static_cast<uint8_t>(current_context());
}

// Helper to copy a string to the arena and return a string_view
// This eliminates repetitive string copying code throughout the parser
std::string_view Parser::copy_to_arena(std::string_view source) {
    if (source.empty()) {
        return {};
    }
    const size_t length = source.length();
    // Note: arena_memory is NOT a local variable - it's a pointer to heap memory
    // managed by the arena allocator. This memory persists until arena.reset()
    char* const arena_memory = static_cast<char*>(arena_.allocate(length + 1));
    std::memcpy(arena_memory, source.data(), length);
    arena_memory[length] = '\0';
    // Returning a view of arena-managed memory is safe - the memory lifetime
    // is tied to the Parser object, not this function scope
    return {arena_memory, length};
}

// ========== Main Interface ==========

ParseResult Parser::parse(std::string_view sql) {
    // Reset state
    reset();
    input_ = sql;
    
    // Initialize tokenizer
    tokenizer_ = new tokenizer::Tokenizer(sql);
    current_token_ = tokenizer_->current();
    peek_token_ = tokenizer_->peek();
    
    // Check for empty input
    if (!current_token_ || current_token_->type == tokenizer::TokenType::EndOfFile) {
        delete tokenizer_;
        tokenizer_ = nullptr;
        return std::unexpected(ParseError{
            1, 1, 0,
            "Empty SQL statement",
            sql.substr(0, 50)
        });
    }
    
    // Parse the statement
    ast::ASTNode* root = parse_statement();
    
    // Check if depth was exceeded
    if (depth_exceeded_) {
        uint32_t line = current_token_ ? current_token_->line : 1;
        uint32_t col = current_token_ ? current_token_->column : 1;
        uint32_t pos = tokenizer_ ? tokenizer_->position() : 0;
        delete tokenizer_;
        tokenizer_ = nullptr;
        return std::unexpected(ParseError{
            line, col, pos,
            "Maximum recursion depth exceeded",
            sql.substr(0, 50)
        });
    }
    
    if (!root) {
        uint32_t line = current_token_ ? current_token_->line : 1;
        uint32_t col = current_token_ ? current_token_->column : 1;
        uint32_t pos = tokenizer_ ? tokenizer_->position() : 0;  // Get position BEFORE delete
        delete tokenizer_;
        tokenizer_ = nullptr;
        return std::unexpected(ParseError{
            line, col,
            pos,
            "Failed to parse statement",
            sql.substr(0, 50)
        });
    }
    
    // Check for unbalanced parentheses
    if (parenthesis_depth_ != 0) {
        uint32_t line = current_token_ ? current_token_->line : 1;
        uint32_t col = current_token_ ? current_token_->column : 1;
        uint32_t pos = tokenizer_ ? tokenizer_->position() : 0;
        delete tokenizer_;
        tokenizer_ = nullptr;
        return std::unexpected(ParseError{
            line, col, pos,
            parenthesis_depth_ > 0 ? "Unclosed parenthesis" : "Unexpected closing parenthesis",
            sql.substr(0, 50)
        });
    }
    
    // Validate AST if in strict mode
    if (strict_mode_ && !validate_ast(root)) {
        uint32_t line = current_token_ ? current_token_->line : 1;
        uint32_t col = current_token_ ? current_token_->column : 1;
        uint32_t pos = tokenizer_ ? tokenizer_->position() : 0;
        delete tokenizer_;
        tokenizer_ = nullptr;
        return std::unexpected(ParseError{
            line, col, pos,
            "Invalid SQL: validation failed",
            sql.substr(0, 50)
        });
    }
    
    // Don't delete tokenizer on success - the AST nodes have string_views
    // pointing to the tokenizer's memory. The tokenizer will be deleted
    // when the parser is destroyed or reset() is called.
    // delete tokenizer_;  // KEEP ALIVE for AST string_views
    // tokenizer_ = nullptr;
    return root;
}

std::expected<std::vector<ast::ASTNode*>, ParseError>
Parser::parse_script(std::string_view sql) {
    std::vector<ast::ASTNode*> statements;
    
    // Initialize parser state
    input_ = sql;
    current_depth_ = 0;
    next_node_id_ = 1;
    parenthesis_depth_ = 0;
    depth_exceeded_ = false;
    
    // Create tokenizer with the new input
    delete tokenizer_;  // Clean up any previous tokenizer
    tokenizer_ = new tokenizer::Tokenizer(input_);
    
    // Get initial tokens
    current_token_ = tokenizer_->current();
    peek_token_ = tokenizer_->peek();
    
    // Parse statements until end of input
    while (current_token_ && current_token_->type != tokenizer::TokenType::EndOfFile) {
        // Skip semicolons between statements
        if (current_token_->type == tokenizer::TokenType::Delimiter && 
            current_token_->value == ";") {
            advance();
            continue;
        }
        
        // Parse a statement
        auto* stmt = parse_statement();
        if (!stmt) {
            // If we couldn't parse a statement and we're not at EOF, it's an error
            if (current_token_ && current_token_->type != tokenizer::TokenType::EndOfFile) {
                uint32_t line = current_token_ ? current_token_->line : 1;
                uint32_t col = current_token_ ? current_token_->column : 1;
                uint32_t pos = current_token_ ? static_cast<uint32_t>(current_token_->column) : 0;
                return std::unexpected(ParseError{
                    line, col, pos,
                    "Failed to parse statement at position " + std::to_string(pos),
                    sql.substr(pos, 50)
                });
            }
            break;
        }
        
        statements.push_back(stmt);
        
        // Consume optional semicolon after statement
        if (current_token_ && current_token_->type == tokenizer::TokenType::Delimiter && 
            current_token_->value == ";") {
            advance();
        }
    }
    
    // Keep tokenizer alive - AST nodes have string_views pointing to it
    return statements;
}

// ========== Memory Management ==========

void Parser::reset() {
    arena_.reset();
    current_depth_ = 0;
    next_node_id_ = 1;
    parenthesis_depth_ = 0;  // Reset parenthesis tracking
    depth_exceeded_ = false;  // Reset depth exceeded flag
    delete tokenizer_;  // Safe even if nullptr
    tokenizer_ = nullptr;
    current_token_ = nullptr;
    peek_token_ = nullptr;
}

size_t Parser::get_memory_used() const noexcept {
    return arena_.bytes_used();
}

size_t Parser::get_node_count() const noexcept {
    return next_node_id_ - 1;
}

// ========== Token Stream ==========

void Parser::advance() {
    if (tokenizer_ && !tokenizer_->at_end()) {
        tokenizer_->advance();
        current_token_ = tokenizer_->current();
        peek_token_ = tokenizer_->peek();
    }
}

bool Parser::match(int token_type) {
    if (check(token_type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::check(int token_type) const {
    return current_token_ && 
           static_cast<int>(current_token_->type) == token_type;
}

void Parser::consume(int token_type, const char* message) {
    if (!match(token_type)) {
        error(message);
    }
}

// ========== Recursive Descent Parsers ==========

ast::ASTNode* Parser::parse_statement() {
    if (!current_token_) {
        return nullptr;
    }
    
    // Prefetch tokens for statement dispatch
    // Statement parsing needs to look ahead for statement type determination
    if (tokenizer_) {
        const auto& tokens = tokenizer_->get_tokens();
        size_t pos = tokenizer_->position();
        
        // Prefetch next 3 tokens for compound statement detection (CREATE TABLE, DROP INDEX, etc.)
        if (pos + 1 < tokens.size()) __builtin_prefetch(&tokens[pos + 1], 0, 3);
        if (pos + 2 < tokens.size()) __builtin_prefetch(&tokens[pos + 2], 0, 2);
        if (pos + 3 < tokens.size()) __builtin_prefetch(&tokens[pos + 3], 0, 1);
    }
    
    // Check if it's a keyword or identifier (some DDL keywords like TRUNCATE aren't in the keyword list)
    if (current_token_->type != tokenizer::TokenType::Keyword && 
        current_token_->type != tokenizer::TokenType::Identifier) {
        return nullptr;
    }
    
    // Dispatch based on keyword/identifier value
    // Use keyword ID for case-insensitive matching
    auto keyword_id = current_token_->keyword_id;
    
#ifdef DEBUG_MIXED_CASE
    std::cerr << "DEBUG parse_statement: keyword_id = " << static_cast<int>(keyword_id) 
              << " (SELECT=" << static_cast<int>(db25::Keyword::SELECT) << ")" << std::endl;
#endif
    
    // Dispatch based on keyword ID
    if (keyword_id == db25::Keyword::WITH) {
        // Parse CTE and the following statement
        return parse_with_statement();
    } else if (keyword_id == db25::Keyword::SELECT) {
        return parse_select_stmt();
    } else if (keyword_id == db25::Keyword::INSERT) {
        return parse_insert_stmt();
    } else if (keyword_id == db25::Keyword::UPDATE) {
        return parse_update_stmt();
    } else if (keyword_id == db25::Keyword::DELETE) {
        return parse_delete_stmt();
    } else if (keyword_id == db25::Keyword::CREATE) {
        // Dispatch to appropriate CREATE handler
        return parse_create_stmt();
    } else if (keyword_id == db25::Keyword::DROP) {
        return parse_drop_stmt();
    } else if (keyword_id == db25::Keyword::ALTER) {
        return parse_alter_table_full();  // Use full implementation
    } else if (current_token_ && (current_token_->value == "TRUNCATE" || current_token_->value == "truncate")) {  // TODO: Add to keywords
        // TRUNCATE might not be in keyword list, use string comparison
        return parse_truncate_stmt();
    } else if (keyword_id == db25::Keyword::BEGIN || keyword_id == db25::Keyword::START) {
        return parse_transaction_stmt();
    } else if (keyword_id == db25::Keyword::COMMIT) {
        return parse_transaction_stmt();
    } else if (keyword_id == db25::Keyword::ROLLBACK) {
        return parse_transaction_stmt();
    } else if (keyword_id == db25::Keyword::SAVEPOINT) {
        return parse_transaction_stmt();
    } else if (keyword_id == db25::Keyword::RELEASE) {
        return parse_transaction_stmt();
    } else if (keyword_id == db25::Keyword::EXPLAIN) {
        return parse_explain_stmt();
    } else if (keyword_id == db25::Keyword::VALUES) {
        return parse_values_stmt();
    } else if (keyword_id == db25::Keyword::SET) {
        return parse_set_stmt();
    } else if (current_token_ && (current_token_->value == "VACUUM" || current_token_->value == "vacuum")) {  // TODO: Add VACUUM to keywords
        return parse_vacuum_stmt();
    } else if (current_token_ && (current_token_->value == "ANALYZE" || current_token_->value == "analyze")) {  // TODO: Add ANALYZE to keywords
        return parse_analyze_stmt();
    } else if (keyword_id == db25::Keyword::ATTACH) {
        return parse_attach_stmt();
    } else if (keyword_id == db25::Keyword::DETACH) {
        return parse_detach_stmt();
    } else if (current_token_ && (current_token_->value == "REINDEX" || current_token_->value == "reindex")) {  // TODO: Add REINDEX to keywords
        return parse_reindex_stmt();
    } else if (current_token_ && (current_token_->value == "PRAGMA" || current_token_->value == "pragma")) {  // TODO: Add PRAGMA to keywords
        return parse_pragma_stmt();
    }
    
    return nullptr;
}

} // namespace db25::parser