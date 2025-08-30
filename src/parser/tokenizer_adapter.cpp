/*
 * Copyright (c) 2024 Space-RF.org
 * Copyright (c) 2024 Chiradip Mandal
 * 
 * DB25 Parser - Tokenizer Adapter Implementation
 */

#include "db25/parser/tokenizer_adapter.hpp"
#include <algorithm>

namespace db25::parser::tokenizer {

Tokenizer::Tokenizer(std::string_view sql) 
    : sql_copy_(sql)
    , position_(0) {
    tokenize();
}

void Tokenizer::tokenize() {
    // Create SIMD tokenizer
    db25::SimdTokenizer simd_tokenizer(
        reinterpret_cast<const std::byte*>(sql_copy_.data()), 
        sql_copy_.size()
    );
    
    // Tokenize the SQL
    auto raw_tokens = simd_tokenizer.tokenize();
    
    // Store token strings permanently to ensure string_views remain valid
    tokens_.reserve(raw_tokens.size());
    token_strings_.reserve(raw_tokens.size());
    
    for (const auto& raw_token : raw_tokens) {
        // Store the token value string
        token_strings_.emplace_back(raw_token.value);
        
        // Create a new token with string_view pointing to our stored string
        Token token;
        token.type = raw_token.type;
        token.value = token_strings_.back();  // Point to the permanent storage
        token.keyword_id = raw_token.keyword_id;
        token.line = raw_token.line;
        token.column = raw_token.column;
        
        tokens_.push_back(token);
    }
    
    // Filter out whitespace and comments for parser
    // We need to rebuild the tokens vector after filtering to maintain alignment
    std::vector<Token> filtered_tokens;
    std::vector<std::string> filtered_strings;
    
    filtered_tokens.reserve(tokens_.size());
    filtered_strings.reserve(token_strings_.size());
    
    for (size_t i = 0; i < tokens_.size(); ++i) {
        if (tokens_[i].type != TokenType::Whitespace && 
            tokens_[i].type != TokenType::Comment) {
            // Keep this token
            filtered_strings.push_back(token_strings_[i]);
            
            Token token = tokens_[i];
            token.value = filtered_strings.back();  // Update string_view to new location
            filtered_tokens.push_back(token);
        }
    }
    
    tokens_ = std::move(filtered_tokens);
    token_strings_ = std::move(filtered_strings);
    
    // Ensure there's always an EOF token at the end
    if (tokens_.empty() || tokens_.back().type != TokenType::EndOfFile) {
        Token eof_token;
        eof_token.type = TokenType::EndOfFile;
        eof_token.value = "";
        eof_token.line = tokens_.empty() ? 1 : tokens_.back().line;
        eof_token.column = tokens_.empty() ? 1 : tokens_.back().column + 
            static_cast<uint32_t>(tokens_.back().value.length());
        eof_token.keyword_id = Keyword::UNKNOWN;
        
        tokens_.push_back(eof_token);
    }
    
    // Reset position to start
    position_ = 0;
}

} // namespace db25::parser::tokenizer