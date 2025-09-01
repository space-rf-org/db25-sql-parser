/*
 * Copyright (c) 2024 Space-RF.org
 * Copyright (c) 2024 Chiradip Mandal
 * 
 * DB25 Parser - Tokenizer Adapter
 * Integrates the SIMD tokenizer with the parser
 */

#pragma once

#include "simd_tokenizer.hpp"  // From GitHub dependency
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Forward declaration to match parser.hpp
namespace db25::parser::tokenizer {
    using Token = db25::Token;
    using TokenType = db25::TokenType;
    
    /**
     * Tokenizer adapter for the parser
     * Wraps the SIMD tokenizer and provides a parser-friendly interface
     */
    class Tokenizer {
    public:
        explicit Tokenizer(std::string_view sql);
        ~Tokenizer() = default;
        
        // No copying
        Tokenizer(const Tokenizer&) = delete;
        Tokenizer& operator=(const Tokenizer&) = delete;
        
        // Allow moving
        Tokenizer(Tokenizer&&) = default;
        Tokenizer& operator=(Tokenizer&&) = default;
        
        /**
         * Get all tokens at once
         */
        [[nodiscard]] const std::vector<Token>& get_tokens() const noexcept { 
            return tokens_; 
        }
        
        /**
         * Get current token
         */
        [[nodiscard]] const Token* current() const noexcept {
            if (position_ < tokens_.size()) {
                return &tokens_[position_];
            }
            return nullptr;
        }
        
        /**
         * Get next token (peek ahead)
         */
        [[nodiscard]] const Token* peek() const noexcept {
            if (position_ + 1 < tokens_.size()) {
                // Prefetch the token after next for better cache utilization
                if (position_ + 2 < tokens_.size()) {
                    __builtin_prefetch(&tokens_[position_ + 2], 0, 1);
                }
                return &tokens_[position_ + 1];
            }
            return nullptr;
        }
        
        /**
         * Advance to next token
         */
        void advance() noexcept {
            if (position_ < tokens_.size()) {
                ++position_;
                
                // Prefetch upcoming tokens for linear scanning
                // Prefetch next 2 tokens with different temporal localities
                if (position_ + 1 < tokens_.size()) {
                    __builtin_prefetch(&tokens_[position_ + 1], 0, 3);  // High temporal locality
                }
                if (position_ + 2 < tokens_.size()) {
                    __builtin_prefetch(&tokens_[position_ + 2], 0, 2);  // Medium temporal locality
                }
                if (position_ + 3 < tokens_.size()) {
                    __builtin_prefetch(&tokens_[position_ + 3], 0, 1);  // Low temporal locality
                }
            }
        }
        
        /**
         * Check if at end of tokens
         */
        [[nodiscard]] bool at_end() const noexcept {
            return position_ >= tokens_.size();
        }
        
        /**
         * Reset to beginning
         */
        void reset() noexcept {
            position_ = 0;
        }
        
        /**
         * Get current position
         */
        [[nodiscard]] size_t position() const noexcept {
            return position_;
        }
        
        /**
         * Set position (for error recovery)
         */
        void set_position(size_t pos) noexcept {
            if (pos <= tokens_.size()) {
                position_ = pos;
            }
        }
        
    private:
        std::string sql_copy_;           // Own the SQL text
        std::vector<Token> tokens_;      // All tokens
        std::vector<std::string> token_strings_;  // Storage for token values
        size_t position_ = 0;            // Current position
        
        void tokenize();
    };
} // namespace db25::parser::tokenizer

