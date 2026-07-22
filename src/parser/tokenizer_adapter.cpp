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
    : sql_copy_(std::make_unique<std::string>(sql))
    , position_(0) {
    tokenize();
}

void Tokenizer::tokenize() {
    // Create SIMD tokenizer over the owned SQL buffer.
    db25::SimdTokenizer simd_tokenizer(
        reinterpret_cast<const std::byte*>(sql_copy_->data()),
        sql_copy_->size()
    );

    // Tokenize the SQL
    auto raw_tokens = simd_tokenizer.tokenize();

    // Each raw Token::value is already a string_view into sql_copy_, which this
    // adapter owns for its whole lifetime - so no per-token string copies are
    // needed. Keep the views and drop whitespace/comments in a single pass (the
    // previous code copied every token value into an owned std::string twice: once
    // building the full list, then again while filtering).
    //
    // NOTE: these token values are views into the middle of the SQL buffer and are
    // therefore NOT NUL-terminated - value.data()[value.size()] is the next SQL
    // byte, not '\0'. Never pass token.value.data() to a C-string API (strlen,
    // atoi, printf("%s"), ...); retained text must go through copy_to_arena (which
    // appends a terminator). The old code happened to NUL-terminate because each
    // value aliased an owned std::string; nothing relied on that.
    tokens_.clear();
    tokens_.reserve(raw_tokens.size() + 1);

    for (const auto& raw_token : raw_tokens) {
        if (raw_token.type == TokenType::Whitespace ||
            raw_token.type == TokenType::Comment) {
            continue;
        }
        Token token;
        token.type = raw_token.type;
        token.value = raw_token.value;   // view into *sql_copy_
        token.keyword_id = raw_token.keyword_id;
        token.line = raw_token.line;
        token.column = raw_token.column;
        tokens_.push_back(token);
    }

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