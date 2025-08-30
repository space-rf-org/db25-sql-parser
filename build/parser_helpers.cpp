// Helper functions to reduce code duplication in parser
// These should be added as private methods to Parser class

#include "db25/parser/parser.hpp"

namespace db25::parser {

// Helper function to parse IF NOT EXISTS clause
bool Parser::parse_if_not_exists() {
    if (current_token_ && current_token_->keyword_id == db25::Keyword::IF) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::NOT) {
            advance();
            if (current_token_ && current_token_->keyword_id == db25::Keyword::EXISTS) {
                advance();
                return true;
            }
        }
    }
    return false;
}

// Helper function to parse IF EXISTS clause
bool Parser::parse_if_exists() {
    if (current_token_ && current_token_->keyword_id == db25::Keyword::IF) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::EXISTS) {
            advance();
            return true;
        }
    }
    return false;
}

// Helper function to parse CASCADE/RESTRICT clause
bool Parser::parse_cascade_restrict(uint32_t& flags) {
    if (current_token_ && current_token_->keyword_id == db25::Keyword::CASCADE) {
        flags |= 0x04;  // CASCADE flag
        advance();
        return true;
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::RESTRICT) {
        flags |= 0x08;  // RESTRICT flag
        advance();
        return true;
    }
    return false;
}

// Helper function to skip optional COLUMN keyword
bool Parser::skip_optional_column_keyword() {
    if (current_token_ && current_token_->keyword_id == db25::Keyword::COLUMN) {
        advance();
        return true;
    }
    return false;
}

// Helper function to parse optional AS keyword
bool Parser::parse_optional_as() {
    if (current_token_ && current_token_->keyword_id == db25::Keyword::AS) {
        advance();
        return true;
    }
    return false;
}

// Helper function to check if token is a clause terminator
bool Parser::is_clause_terminator() const {
    if (!current_token_) return true;
    
    if (current_token_->type != tokenizer::TokenType::Keyword) {
        return false;
    }
    
    const auto kw = current_token_->keyword_id;
    return kw == db25::Keyword::FROM ||
           kw == db25::Keyword::WHERE ||
           kw == db25::Keyword::GROUP ||
           kw == db25::Keyword::HAVING ||
           kw == db25::Keyword::ORDER ||
           kw == db25::Keyword::LIMIT ||
           kw == db25::Keyword::UNION ||
           kw == db25::Keyword::INTERSECT ||
           kw == db25::Keyword::EXCEPT ||
           current_token_->value == "UNION" ||    // Handle case variations
           current_token_->value == "INTERSECT" ||
           current_token_->value == "EXCEPT";
}

// Helper function to parse comma-separated list
template<typename ParseFunc>
ast::ASTNode* Parser::parse_comma_list(NodeType list_type, ParseFunc parse_item) {
    auto* list = arena_.allocate<ast::ASTNode>();
    new (list) ast::ASTNode(list_type);
    list->node_id = next_node_id_++;
    
    ast::ASTNode* last_item = nullptr;
    
    while (current_token_ && !is_clause_terminator()) {
        auto* item = parse_item();
        if (!item) break;
        
        item->parent = list;
        if (!list->first_child) {
            list->first_child = item;
        } else if (last_item) {
            last_item->next_sibling = item;
        }
        last_item = item;
        list->child_count++;
        
        // Check for comma
        if (current_token_ && current_token_->type == tokenizer::TokenType::Delimiter &&
            current_token_->value == ",") {
            advance();
        } else {
            break;  // No more items
        }
    }
    
    return list->child_count > 0 ? list : nullptr;
}

} // namespace db25::parser