# Missing Intermediate Handlers from Backup

## Overview
These six intermediate handler functions were present in the original parser.cpp.backup but are missing in the refactored version. They provided a three-tier architecture for CREATE statements.

## 1. parse_create_table_stmt() - Line 1482-1486
```cpp
ast::ASTNode* Parser::parse_create_table_stmt() {
    // This is the old entry point, redirect to new implementation
    advance();  // Skip CREATE
    return parse_create_table_impl();
}
```
**Purpose**: Entry point that skips CREATE and delegates to impl

## 2. parse_create_table_impl() - Line 1488-1528
```cpp
ast::ASTNode* Parser::parse_create_table_impl() {
    // We're at TABLE keyword
    advance();  // Skip TABLE
    
    // Create CREATE TABLE statement node
    auto* create_node = arena_.allocate<ast::ASTNode>();
    new (create_node) ast::ASTNode(ast::NodeType::CreateTableStmt);
    create_node->node_id = next_node_id_++;
    
    // Check for IF NOT EXISTS
    if (current_token_ && current_token_->keyword_id == db25::Keyword::IF) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::NOT) {
            advance();
            if (current_token_ && current_token_->keyword_id == db25::Keyword::EXISTS) {
                advance();
                create_node->semantic_flags |= 0x01;  // IF NOT EXISTS flag
            }
        }
    }
    
    // Get table name
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
        create_node->primary_text = current_token_->value;
        advance();
    }
    
    // Simplified implementation - consumes remaining tokens
    int paren_depth = 0;
    while (current_token_ && 
           current_token_->type != tokenizer::TokenType::EndOfFile &&
           !(current_token_->type == tokenizer::TokenType::Delimiter && 
             current_token_->value == ";" && paren_depth == 0)) {
        if (current_token_->value == "(") paren_depth++;
        else if (current_token_->value == ")") paren_depth--;
        advance();
    }
    
    return create_node;
}
```
**Key Features**:
- Handles IF NOT EXISTS
- Sets semantic_flags
- Simplified column/constraint parsing

## 3. parse_create_index_stmt() - Line 1530-1534
```cpp
ast::ASTNode* Parser::parse_create_index_stmt() {
    // Entry point for standalone parse_create_index_stmt calls
    advance();  // Skip CREATE
    return parse_create_index_impl();
}
```
**Purpose**: Entry point that skips CREATE and delegates to impl

## 4. parse_create_index_impl() - Line 1536-1601
```cpp
ast::ASTNode* Parser::parse_create_index_impl() {
    // We're already past CREATE, now at either UNIQUE or INDEX
    bool is_unique = false;
    if (current_token_ && current_token_->keyword_id == db25::Keyword::UNIQUE) {
        is_unique = true;
        advance();
        // Now we should be at INDEX
        if (!current_token_ || (current_token_->value != "INDEX" && current_token_->value != "index")) {
            return nullptr;
        }
    } else if (!current_token_ || (current_token_->value != "INDEX" && current_token_->value != "index")) {
        // If not UNIQUE, must be INDEX
        return nullptr;
    }
    advance();  // Skip INDEX
    
    // Create CREATE INDEX statement node
    auto* create_node = arena_.allocate<ast::ASTNode>();
    new (create_node) ast::ASTNode(ast::NodeType::CreateIndexStmt);
    create_node->node_id = next_node_id_++;
    if (is_unique) {
        create_node->semantic_flags |= 0x02;  // UNIQUE flag
    }
    
    // Check for IF NOT EXISTS
    if (current_token_ && current_token_->keyword_id == db25::Keyword::IF) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::NOT) {
            advance();
            if (current_token_ && current_token_->keyword_id == db25::Keyword::EXISTS) {
                advance();
                create_node->semantic_flags |= 0x01;  // IF NOT EXISTS flag
            }
        }
    }
    
    // Get index name
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
        create_node->primary_text = current_token_->value;
        advance();
    }
    
    // Expect ON keyword
    if (current_token_ && current_token_->keyword_id == db25::Keyword::ON) {
        advance();
        
        // Get table name
        if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
            create_node->schema_name = current_token_->value;  // Use schema_name for table name
            advance();
        }
    }
    
    // Simplified implementation - consumes remaining tokens
    int paren_depth = 0;
    while (current_token_ && 
           current_token_->type != tokenizer::TokenType::EndOfFile &&
           !(current_token_->type == tokenizer::TokenType::Delimiter && 
             current_token_->value == ";" && paren_depth == 0)) {
        if (current_token_->value == "(") paren_depth++;
        else if (current_token_->value == ")") paren_depth--;
        advance();
    }
    
    return create_node;
}
```
**Key Features**:
- Handles UNIQUE flag (semantic_flags |= 0x02)
- Handles IF NOT EXISTS
- Parses ON table_name clause
- Uses schema_name field for table name

## 5. parse_create_view_stmt() - Line 1603-1607
```cpp
ast::ASTNode* Parser::parse_create_view_stmt() {
    // Entry point for standalone parse_create_view_stmt calls
    advance();  // Skip CREATE
    return parse_create_view_impl();
}
```
**Purpose**: Entry point that skips CREATE and delegates to impl

## 6. parse_create_view_impl() - Line 1609-1654
```cpp
ast::ASTNode* Parser::parse_create_view_impl() {
    // We might be at VIEW or need to skip OR REPLACE
    // This was already handled in parse_create_stmt
    
    // Expect VIEW keyword
    if (!current_token_ || (current_token_->value != "VIEW" && current_token_->value != "view")) {
        return nullptr;
    }
    advance();  // Skip VIEW
    
    // Create CREATE VIEW statement node
    auto* create_node = arena_.allocate<ast::ASTNode>();
    new (create_node) ast::ASTNode(ast::NodeType::CreateViewStmt);
    create_node->node_id = next_node_id_++;
    
    // Get view name
    if (current_token_ && current_token_->type == tokenizer::TokenType::Identifier) {
        create_node->primary_text = current_token_->value;
        advance();
    }
    
    // Optional column list
    if (current_token_ && current_token_->value == "(") {
        // Skip column list for now
        int paren_depth = 1;
        advance();
        while (current_token_ && paren_depth > 0) {
            if (current_token_->value == "(") paren_depth++;
            else if (current_token_->value == ")") paren_depth--;
            advance();
        }
    }
    
    // Expect AS keyword
    if (current_token_ && current_token_->keyword_id == db25::Keyword::AS) {
        advance();
        
        // Parse SELECT statement
        if (current_token_ && current_token_->keyword_id == db25::Keyword::SELECT) {
            create_node->left = parse_select_stmt();
        }
    }
    
    return create_node;
}
```
**Key Features**:
- Handles OR REPLACE (in parse_create_stmt)
- Parses optional column list
- Parses AS SELECT clause
- Links SELECT statement as left child

## Architectural Pattern

### Three-Tier Architecture (Original):
```
parse_create_stmt()          [Dispatcher - handles all CREATE variants]
    ↓
parse_create_table_stmt()    [Entry point - skips CREATE]
    ↓
parse_create_table_impl()    [Implementation - actual parsing]
```

### Two-Tier Architecture (Refactored):
```
parse_create_stmt()          [Dispatcher - handles all CREATE variants]
    ↓
parse_create_table_full()    [Full implementation - combines stmt+impl]
```

## Semantic Flags Used

- `0x01` - IF NOT EXISTS
- `0x02` - UNIQUE (for indexes)
- `0x04` - OR REPLACE (for views, set in dispatcher)
- `0x08` - TEMPORARY (for tables, set in dispatcher)

## Impact Analysis

### What Was Lost:
1. **Intermediate validation layer** - The _stmt functions could validate before delegating
2. **Entry point flexibility** - Could call parse_create_table_stmt() directly
3. **State management** - Clear separation between token positioning and parsing

### What Needs Verification:
1. Are all semantic flags being set correctly in the refactored version?
2. Is token positioning handled correctly when calling _full functions?
3. Do we need the ability to call CREATE parsers without the CREATE keyword?

## Recommendation

The refactored `parse_create_table_full()`, `parse_create_index_full()`, and `parse_create_view_full()` functions should:

1. Include all semantic flag handling from the _impl versions
2. Support being called both from dispatcher and directly
3. Handle token positioning correctly for both cases
4. Preserve the simplified parsing behavior for quick prototyping