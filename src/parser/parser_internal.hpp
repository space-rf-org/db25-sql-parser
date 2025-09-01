/*
 * Copyright (c) 2024 DB25 Parser Project
 * 
 * Internal header for parser module refactoring
 * Provides shared utilities and friend class access
 */

#pragma once

#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include "db25/parser/tokenizer_adapter.hpp"

namespace db25::parser::internal {

// Forward declarations for parser modules
class DDLParser;
class DMLParser;
class SelectParser;
class ExpressionParser;
class UtilityParser;

// Helper to access Parser internals from friend classes
// This avoids exposing internals in the public header
class ParserAccess {
public:
    // Token stream access
    static void advance(Parser* p) { p->advance(); }
    static bool match(Parser* p, int token_type) { return p->match(token_type); }
    static bool check(Parser* p, int token_type) { return p->check(token_type); }
    static void consume(Parser* p, int token_type, const char* msg) { 
        p->consume(token_type, msg); 
    }
    
    // Node creation
    template<typename... Args>
    static ast::ASTNode* make_node(Parser* p, Args&&... args) {
        return p->make_node(std::forward<Args>(args)...);
    }
    
    // String management
    static std::string_view copy_to_arena(Parser* p, std::string_view str) {
        return p->copy_to_arena(str);
    }
    
    // Error handling
    [[noreturn]] static void error(Parser* p, const std::string& msg) {
        p->error(msg);
    }
    
    // Token access
    static const tokenizer::Token* current_token(Parser* p) {
        return p->current_token_;
    }
    
    static const tokenizer::Token* peek_token(Parser* p) {
        return p->peek_token_;
    }
    
    // Note: DepthGuard access is removed since friend classes can use it directly
    
    // Context management
    static void push_context(Parser* p, Parser::ParseContext ctx) {
        p->push_context(ctx);
    }
    
    static void pop_context(Parser* p) {
        p->pop_context();
    }
    
    static uint8_t get_context_hint(Parser* p) {
        return p->get_context_hint();
    }
};

// Base class for parser modules
class ParserModule {
protected:
    Parser* parser_;
    
    // Convenience accessors
    void advance() { ParserAccess::advance(parser_); }
    bool match(int token_type) { return ParserAccess::match(parser_, token_type); }
    bool check(int token_type) { return ParserAccess::check(parser_, token_type); }
    void consume(int token_type, const char* msg) { 
        ParserAccess::consume(parser_, token_type, msg); 
    }
    
    template<typename... Args>
    ast::ASTNode* make_node(Args&&... args) {
        return ParserAccess::make_node(parser_, std::forward<Args>(args)...);
    }
    
    std::string_view copy_to_arena(std::string_view str) {
        return ParserAccess::copy_to_arena(parser_, str);
    }
    
    [[noreturn]] void error(const std::string& msg) {
        ParserAccess::error(parser_, msg);
    }
    
    const tokenizer::Token* current_token() {
        return ParserAccess::current_token(parser_);
    }
    
    const tokenizer::Token* peek_token() {
        return ParserAccess::peek_token(parser_);
    }
    
    void push_context(Parser::ParseContext ctx) {
        ParserAccess::push_context(parser_, ctx);
    }
    
    void pop_context() {
        ParserAccess::pop_context(parser_);
    }
    
public:
    explicit ParserModule(Parser* parser) : parser_(parser) {}
    virtual ~ParserModule() = default;
};

} // namespace db25::parser::internal