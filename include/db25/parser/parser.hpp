/*
 * Copyright (c) 2024 Space-RF.org
 * Copyright (c) 2024 Chiradip Mandal
 * 
 * DB25 SQL Parser - Main Parser Class
 * Version 1.0
 * 
 * Design:
 * - Recursive descent for statements
 * - Pratt parser for expressions
 * - Arena-allocated AST nodes
 * - Integration with DB25 tokenizer
 */

#pragma once

#include "db25/ast/ast_node.hpp"
#include "db25/memory/arena.hpp"
#include <memory>
#include <expected>
#include <string_view>
#include <vector>
#include <span>

// Include tokenizer adapter
#include "db25/parser/tokenizer_adapter.hpp"

namespace db25::parser {

/**
 * Error information for parse failures
 */
struct ParseError {
    uint32_t line;
    uint32_t column;
    uint32_t position;
    std::string message;
    std::string_view context;  // Surrounding text for error display
    
    ParseError(uint32_t l, uint32_t c, uint32_t p, 
               std::string msg, std::string_view ctx = {})
        : line(l), column(c), position(p)
        , message(std::move(msg)), context(ctx) {}
};

/**
 * Parse result - either AST root or error
 */
using ParseResult = std::expected<ast::ASTNode*, ParseError>;

/**
 * Parser configuration options
 */
struct ParserConfig {
    ast::ParserMode mode = ast::ParserMode::Production;
    size_t max_depth = 1000;        // Maximum recursion depth
    bool size_compromise = true;    // Allow size/semantics compromise
    bool strict_ansi = false;       // Strict ANSI SQL compliance
    bool allow_extensions = true;   // Allow DB25 extensions
};

/**
 * Main SQL Parser class
 * 
 * Usage:
 *   Parser parser(config);
 *   auto result = parser.parse(sql_text);
 *   if (result) {
 *       auto* root = result.value();
 *       // Use AST...
 *   } else {
 *       auto error = result.error();
 *       // Handle error...
 *   }
 */
class Parser {
public:
    // ========== Construction ==========
    
    explicit Parser(const ParserConfig& config = {});
    ~Parser();
    
    // No copying
    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;
    
    // Allow moving
    Parser(Parser&&) = default;
    Parser& operator=(Parser&&) = default;
    
    // ========== Main Interface ==========
    
    /**
     * Parse SQL text and return AST
     * @param sql SQL text to parse
     * @return Root AST node or parse error
     */
    [[nodiscard]] ParseResult parse(std::string_view sql);
    
    /**
     * Parse multiple statements (script)
     * @param sql SQL script with multiple statements
     * @return Vector of AST roots or first error
     */
    [[nodiscard]] std::expected<std::vector<ast::ASTNode*>, ParseError> 
    parse_script(std::string_view sql);
    
    // ========== Memory Management ==========
    
    /**
     * Reset parser state and arena
     * Call between parses to reuse parser instance
     */
    void reset();
    
    /**
     * Get memory statistics
     */
    [[nodiscard]] size_t get_memory_used() const noexcept;
    [[nodiscard]] size_t get_node_count() const noexcept;
    
    // ========== Configuration ==========
    
    [[nodiscard]] const ParserConfig& config() const noexcept { return config_; }
    void set_config(const ParserConfig& config) { config_ = config; }
    
private:
    // ========== Internal State ==========
    
    ParserConfig config_;
    db25::Arena arena_;              // Memory arena for AST nodes
    std::string_view input_;        // Current input being parsed
    size_t current_depth_;          // Current recursion depth
    uint32_t next_node_id_;         // Next node ID to assign
    
    // ========== Depth Guard ==========
    
    /**
     * RAII guard for recursion depth protection
     * Prevents stack overflow attacks and infinite recursion
     */
    class DepthGuard {
    public:
        DepthGuard(Parser* parser) : parser_(parser), valid_(true) {
            if (parser_->current_depth_ >= parser_->config_.max_depth) {
                parser_->depth_exceeded_ = true;
                valid_ = false;
            } else {
                parser_->current_depth_++;
            }
        }
        
        ~DepthGuard() {
            if (parser_ && valid_) {
                parser_->current_depth_--;
            }
        }
        
        [[nodiscard]] bool is_valid() const { return valid_; }
        
        // Non-copyable, non-movable
        DepthGuard(const DepthGuard&) = delete;
        DepthGuard& operator=(const DepthGuard&) = delete;
        DepthGuard(DepthGuard&&) = delete;
        DepthGuard& operator=(DepthGuard&&) = delete;
        
    private:
        Parser* parser_;
        bool valid_;
    };
    
    // Depth exceeded flag
    bool depth_exceeded_ = false;
    
    // Token stream management
    // Note: tokenizer is owned externally to avoid incomplete type issues
    tokenizer::Tokenizer* tokenizer_;
    const tokenizer::Token* current_token_;
    const tokenizer::Token* peek_token_;
    
    // ========== Node Creation ==========
    
    /**
     * Create a new AST node in the arena
     */
    template<typename... Args>
    [[nodiscard]] ast::ASTNode* make_node(Args&&... args) {
        auto* node = arena_.allocate<ast::ASTNode>();
        std::construct_at(node, std::forward<Args>(args)...);
        node->node_id = next_node_id_++;
        return node;
    }
    
    // ========== Token Stream ==========
    
    void advance();
    [[nodiscard]] bool match(int token_type);
    [[nodiscard]] bool check(int token_type) const;
    void consume(int token_type, const char* message);
    
    // ========== Recursive Descent Parsers ==========
    
    [[nodiscard]] ast::ASTNode* parse_statement();
    [[nodiscard]] ast::ASTNode* parse_with_statement();
    [[nodiscard]] ast::ASTNode* parse_select_stmt();
    [[nodiscard]] ast::ASTNode* parse_insert_stmt();
    [[nodiscard]] ast::ASTNode* parse_update_stmt();
    [[nodiscard]] ast::ASTNode* parse_delete_stmt();
    [[nodiscard]] ast::ASTNode* parse_create_stmt();  // Dispatches to specific CREATE
    [[nodiscard]] ast::ASTNode* parse_create_table_stmt();
    [[nodiscard]] ast::ASTNode* parse_create_table_impl();  // Implementation helper
    [[nodiscard]] ast::ASTNode* parse_create_table_full();  // Full implementation with columns
    [[nodiscard]] ast::ASTNode* parse_create_index_stmt();
    [[nodiscard]] ast::ASTNode* parse_create_index_impl();  // Implementation helper
    [[nodiscard]] ast::ASTNode* parse_create_index_full();  // Full implementation with options
    [[nodiscard]] ast::ASTNode* parse_create_view_stmt();
    [[nodiscard]] ast::ASTNode* parse_create_view_impl();   // Implementation helper
    [[nodiscard]] ast::ASTNode* parse_create_trigger();     // CREATE TRIGGER
    [[nodiscard]] ast::ASTNode* parse_create_schema();      // CREATE SCHEMA
    [[nodiscard]] ast::ASTNode* parse_drop_stmt();
    [[nodiscard]] ast::ASTNode* parse_alter_table_stmt();
    [[nodiscard]] ast::ASTNode* parse_alter_table_full();   // Full implementation
    [[nodiscard]] ast::ASTNode* parse_truncate_stmt();
    
    // DDL helper parsers
    [[nodiscard]] ast::ASTNode* parse_data_type();
    [[nodiscard]] ast::ASTNode* parse_column_definition();
    [[nodiscard]] ast::ASTNode* parse_column_constraint();
    [[nodiscard]] ast::ASTNode* parse_table_constraint();
    
    // Transaction control statements
    [[nodiscard]] ast::ASTNode* parse_transaction_stmt();
    
    // Utility statements
    [[nodiscard]] ast::ASTNode* parse_explain_stmt();
    [[nodiscard]] ast::ASTNode* parse_values_stmt();
    [[nodiscard]] ast::ASTNode* parse_set_stmt();
    [[nodiscard]] ast::ASTNode* parse_vacuum_stmt();
    [[nodiscard]] ast::ASTNode* parse_analyze_stmt();
    [[nodiscard]] ast::ASTNode* parse_attach_stmt();
    [[nodiscard]] ast::ASTNode* parse_detach_stmt();
    [[nodiscard]] ast::ASTNode* parse_reindex_stmt();
    [[nodiscard]] ast::ASTNode* parse_pragma_stmt();
    
    // DML clause parsers
    [[nodiscard]] ast::ASTNode* parse_returning_clause();
    [[nodiscard]] ast::ASTNode* parse_on_conflict_clause();
    [[nodiscard]] ast::ASTNode* parse_using_clause();
    
    // SELECT clause parsers
    [[nodiscard]] ast::ASTNode* parse_select_list();
    [[nodiscard]] ast::ASTNode* parse_select_item();
    [[nodiscard]] ast::ASTNode* parse_from_clause();
    [[nodiscard]] ast::ASTNode* parse_join_clause();
    [[nodiscard]] ast::ASTNode* parse_where_clause();
    [[nodiscard]] ast::ASTNode* parse_group_by_clause();
    [[nodiscard]] ast::ASTNode* parse_having_clause();
    [[nodiscard]] ast::ASTNode* parse_order_by_clause();
    [[nodiscard]] ast::ASTNode* parse_limit_clause();
    [[nodiscard]] ast::ASTNode* parse_identifier();
    [[nodiscard]] ast::ASTNode* parse_column_ref();
    [[nodiscard]] ast::ASTNode* parse_function_call();
    [[nodiscard]] ast::ASTNode* parse_table_reference();
    [[nodiscard]] ast::ASTNode* parse_case_expression();
    [[nodiscard]] ast::ASTNode* parse_cast_expression();
    [[nodiscard]] ast::ASTNode* parse_extract_expression();
    
    // ========== Pratt Parser for Expressions ==========
    
    [[nodiscard]] ast::ASTNode* parse_expression(int min_precedence = 0);
    [[nodiscard]] ast::ASTNode* parse_primary_expression();
    [[nodiscard]] ast::ASTNode* parse_primary();
    [[nodiscard]] int get_precedence() const;
    [[nodiscard]] ast::ASTNode* parse_window_spec();  // Window function OVER clause
    
    // ========== Helper Methods ==========
    
    // Helper to copy a string to the arena
    std::string_view copy_to_arena(std::string_view source);
    
    // ========== Validation Methods ==========
    
    // Validate parsed AST for semantic correctness
    [[nodiscard]] bool validate_ast(ast::ASTNode* root);
    [[nodiscard]] bool validate_select_stmt(ast::ASTNode* select_stmt);
    [[nodiscard]] bool validate_clause_dependencies(ast::ASTNode* select_stmt);
    [[nodiscard]] bool validate_join_clause(ast::ASTNode* join_clause);
    
    // ========== Error Handling ==========
    
    [[noreturn]] void error(const std::string& message);
    void synchronize();  // Error recovery
    
    // ========== Validation State ==========
    
    int parenthesis_depth_ = 0;  // Track parenthesis balance
    bool strict_mode_ = true;    // Enable strict validation
    
    // ========== Context Tracking ==========
    
    /**
     * Context hints for unresolved identifiers
     * Helps semantic analyzer understand where identifier appeared
     */
    enum class ParseContext : uint8_t {
        UNKNOWN = 0,
        SELECT_LIST,      // In SELECT clause
        FROM_CLAUSE,      // In FROM clause (table names)
        WHERE_CLAUSE,     // In WHERE condition
        GROUP_BY_CLAUSE,  // In GROUP BY
        HAVING_CLAUSE,    // In HAVING condition
        ORDER_BY_CLAUSE,  // In ORDER BY
        JOIN_CONDITION,   // In ON clause of JOIN
        CASE_EXPRESSION,  // In CASE expression
        FUNCTION_ARG,     // As function argument
        SUBQUERY          // Inside subquery
    };
    
    // Current parsing context stack
    std::vector<ParseContext> context_stack_;
    
    // Helper methods for context management
    void push_context(ParseContext ctx) { context_stack_.push_back(ctx); }
    void pop_context() { if (!context_stack_.empty()) context_stack_.pop_back(); }
    ParseContext current_context() const { 
        return context_stack_.empty() ? ParseContext::UNKNOWN : context_stack_.back(); 
    }
    
    // Get context hint for current position
    uint8_t get_context_hint() const;
};

} // namespace db25::parser