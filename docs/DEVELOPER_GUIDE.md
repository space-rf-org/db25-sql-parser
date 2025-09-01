# DB25 SQL Parser - Developer Guide

## Architecture Overview

The DB25 SQL Parser follows a modular architecture with clear separation of concerns:

```
┌─────────────────────────────────────────────┐
│            User SQL Query                    │
└─────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────┐
│         SIMD Tokenizer (4 variants)         │
│   Scalar | NEON | AVX2 | AVX512            │
└─────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────┐
│       Recursive Descent Parser              │
│   • Statement Router (O(1) dispatch)        │
│   • Expression Parser (precedence climbing) │
│   • Depth Guard (stack protection)          │
└─────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────┐
│      Abstract Syntax Tree (AST)             │
│   • Arena-allocated nodes                   │
│   • Parent-child-sibling structure          │
│   • Zero-copy string views                  │
└─────────────────────────────────────────────┘
```

## Core Components

### 1. Tokenizer (`external/tokenizer/`)
- **PROTECTED**: Do not modify during parser development
- SIMD-optimized with runtime CPU detection
- Processes 16-64 bytes in parallel
- Zero-copy design using string_view

### 2. Parser (`src/parser/`)
- Recursive descent with predictive parsing
- Precedence climbing for expressions
- Context-aware parsing for ambiguous constructs
- DepthGuard for stack overflow protection

### 3. AST Nodes (`include/db25/ast/`)
- Compact node structure (64 bytes typical)
- Type-safe node types enumeration
- Semantic flags for parser hints
- Efficient traversal via sibling pointers

### 4. Memory Management (`src/memory/`)
- **PROTECTED**: Arena allocator is frozen
- Lock-free allocation
- Zero fragmentation
- Bulk deallocation

## Adding New Features - 5 Examples

### Example 1: Adding a New SQL Function

**Goal**: Add support for `COALESCE(expr1, expr2, ...)`

```cpp
// Step 1: Update src/parser/parser.cpp - parse_function()
ast::ASTNode* Parser::parse_function() {
    // ... existing code ...
    
    if (func_name == "COALESCE") {
        // COALESCE takes variable arguments
        func_node->semantic_flags |= 0x100; // Mark as COALESCE
        
        // Parse all arguments
        while (current_token_ && current_token_->value != ")") {
            auto* arg = parse_expression(0);
            if (!arg) return nullptr;
            
            // Add as child
            arg->parent = func_node;
            if (!last_arg) {
                func_node->first_child = arg;
            } else {
                last_arg->next_sibling = arg;
            }
            last_arg = arg;
            func_node->child_count++;
            
            if (current_token_->value == ",") {
                advance();
            }
        }
    }
}

// Step 2: Add test in tests/test_parser_basic.cpp
TEST_F(BasicParserTest, CoalesceFunction) {
    auto result = parser.parse("SELECT COALESCE(name, email, 'N/A') FROM users");
    EXPECT_TRUE(result.has_value());
    // Verify AST structure
}
```

### Example 2: Adding MERGE Statement Support

**Goal**: Support `MERGE INTO target USING source ON condition ...`

```cpp
// Step 1: Add keyword to external/tokenizer/include/keywords.hpp
// Run: cd external/tokenizer && ./tools/extract_keywords

// Step 2: Add node type in include/db25/ast/node_types.hpp
enum class NodeType : uint8_t {
    // ... existing ...
    MergeStmt,      // Add after other statement types
    MergeWhenClause,
    // ...
};

// Step 3: Add parser method in src/parser/parser.cpp
ast::ASTNode* Parser::parse_merge_stmt() {
    DepthGuard guard(this);
    if (!guard.is_valid()) return nullptr;
    
    advance(); // consume MERGE
    
    // Expect INTO
    if (!current_token_ || current_token_->keyword_id != db25::Keyword::INTO) {
        return nullptr;
    }
    advance();
    
    auto* merge_node = arena_.allocate<ast::ASTNode>();
    new (merge_node) ast::ASTNode(ast::NodeType::MergeStmt);
    merge_node->node_id = next_node_id_++;
    
    // Parse target table
    auto* target = parse_table_reference();
    if (!target) return nullptr;
    
    target->parent = merge_node;
    merge_node->first_child = target;
    
    // Continue parsing USING, ON, WHEN clauses...
    // ...
    
    return merge_node;
}

// Step 4: Update statement router
if (keyword_id == db25::Keyword::MERGE) {
    return parse_merge_stmt();
}
```

### Example 3: Adding JSON Path Expressions

**Goal**: Support `column->>'$.path.to.value'`

```cpp
// Step 1: Add operator types in include/db25/ast/node_types.hpp
enum class BinaryOp : uint8_t {
    // ... existing ...
    JsonExtract,      // ->
    JsonExtractText,  // ->>
};

// Step 2: Update expression parser in src/parser/parser.cpp
ast::ASTNode* Parser::parse_postfix_expression() {
    auto* left = parse_primary_expression();
    
    while (current_token_) {
        if (current_token_->value == "->" || 
            current_token_->value == "->>") {
            
            bool is_text = (current_token_->value == "->>");
            advance();
            
            auto* json_expr = arena_.allocate<ast::ASTNode>();
            new (json_expr) ast::ASTNode(ast::NodeType::BinaryExpr);
            json_expr->op_type = is_text ? 
                static_cast<uint8_t>(BinaryOp::JsonExtractText) :
                static_cast<uint8_t>(BinaryOp::JsonExtract);
            
            // Parse path (string literal expected)
            auto* path = parse_primary_expression();
            if (!path) return nullptr;
            
            json_expr->first_child = left;
            left->parent = json_expr;
            left->next_sibling = path;
            path->parent = json_expr;
            json_expr->child_count = 2;
            
            left = json_expr;
        } else {
            break;
        }
    }
    
    return left;
}
```

### Example 4: Adding PIVOT/UNPIVOT Support

**Goal**: Support `PIVOT (AGG(value) FOR column IN (values))`

```cpp
// Step 1: Add node types
enum class NodeType : uint8_t {
    // ... existing ...
    PivotClause,
    UnpivotClause,
    PivotForClause,
    PivotInClause,
};

// Step 2: Add parsing in FROM clause handler
ast::ASTNode* Parser::parse_table_factor() {
    auto* table = parse_table_reference();
    
    // Check for PIVOT/UNPIVOT
    if (current_token_ && current_token_->keyword_id == db25::Keyword::PIVOT) {
        advance();
        
        auto* pivot = arena_.allocate<ast::ASTNode>();
        new (pivot) ast::ASTNode(ast::NodeType::PivotClause);
        
        // Expect (
        if (!expect_token("(")) return nullptr;
        
        // Parse aggregate function
        auto* agg = parse_function();
        if (!agg) return nullptr;
        
        // Parse FOR column
        if (!expect_keyword(db25::Keyword::FOR)) return nullptr;
        auto* for_col = parse_identifier();
        
        // Parse IN (values)
        if (!expect_keyword(db25::Keyword::IN)) return nullptr;
        auto* in_list = parse_expression_list();
        
        // Build AST structure
        pivot->first_child = agg;
        agg->next_sibling = for_col;
        for_col->next_sibling = in_list;
        pivot->child_count = 3;
        
        // Attach to table
        table->next_sibling = pivot;
        
        if (!expect_token(")")) return nullptr;
    }
    
    return table;
}
```

### Example 5: Adding Query Hints

**Goal**: Support `SELECT /*+ INDEX(users idx_name) */ * FROM users`

```cpp
// Step 1: Add hint node type
enum class NodeType : uint8_t {
    // ... existing ...
    QueryHint,
    IndexHint,
};

// Step 2: Parse hints after SELECT keyword
ast::ASTNode* Parser::parse_select_stmt() {
    // ... consume SELECT ...
    
    // Check for hints
    ast::ASTNode* hints = nullptr;
    if (current_token_ && current_token_->type == tokenizer::TokenType::Comment) {
        std::string comment = current_token_->value;
        if (comment.starts_with("/*+") && comment.ends_with("*/")) {
            hints = parse_query_hints(comment);
            advance();
        }
    }
    
    // ... continue parsing ...
    
    if (hints) {
        // Attach hints to SELECT node
        select_node->semantic_flags |= 0x8000; // Has hints
        // Add as special child
    }
}

ast::ASTNode* Parser::parse_query_hints(const std::string& hint_text) {
    // Parse hint syntax
    auto* hint_node = arena_.allocate<ast::ASTNode>();
    new (hint_node) ast::ASTNode(ast::NodeType::QueryHint);
    
    // Extract hint content (remove /*+ and */)
    std::string hints = hint_text.substr(3, hint_text.length() - 5);
    
    // Simple parsing of INDEX(table alias) format
    if (hints.starts_with("INDEX")) {
        auto* index_hint = arena_.allocate<ast::ASTNode>();
        new (index_hint) ast::ASTNode(ast::NodeType::IndexHint);
        index_hint->value = hints;
        
        hint_node->first_child = index_hint;
        hint_node->child_count = 1;
    }
    
    return hint_node;
}
```

## Testing Guidelines

### 1. Unit Tests
Always add tests when implementing new features:

```cpp
// In appropriate test file (e.g., tests/test_parser_basic.cpp)
TEST_F(ParserTest, YourNewFeature) {
    // Test successful parse
    auto result = parser.parse("YOUR SQL HERE");
    ASSERT_TRUE(result.has_value());
    
    // Verify AST structure
    auto* ast = result.value();
    EXPECT_EQ(ast->type, ast::NodeType::ExpectedType);
    EXPECT_EQ(ast->child_count, expected_count);
    
    // Test error cases
    auto error_result = parser.parse("INVALID SQL");
    EXPECT_FALSE(error_result.has_value());
}
```

### 2. Integration Tests
Add queries to `tests/showcase_queries.sqls`:

```sql
# Your Feature Category
# ============================================

# Basic usage
YOUR SQL QUERY HERE
---

# Complex usage
MORE COMPLEX QUERY
---
```

### 3. Performance Tests
For performance-critical features:

```cpp
TEST_F(PerformanceTest, YourFeature) {
    const int iterations = 10000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        auto result = parser.parse("YOUR SQL");
        ASSERT_TRUE(result.has_value());
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Average parse time: " 
              << duration.count() / iterations << " μs\n";
}
```

## Code Style Guidelines

1. **Use modern C++23 features**: concepts, ranges, std::expected
2. **Follow RAII**: Use arena allocation, no manual delete
3. **Error handling**: Use Result<T, Error> pattern
4. **Performance**: Profile before optimizing
5. **Documentation**: Document complex algorithms
6. **Testing**: Write tests before implementation (TDD)

## Debugging Tips

### 1. Enable Parser Tracing
```cpp
// In parser.cpp, add trace output
#ifdef DEBUG
    std::cerr << "Parsing: " << current_token_->value << "\n";
#endif
```

### 2. Use AST Viewer
```bash
echo "YOUR SQL" | ./ast_viewer --stats
```

### 3. Dump Tokenizer Output
```cpp
// Debug tokenizer issues
tokenizer::Tokenizer tok;
tok.tokenize(sql);
for (const auto& token : tok.tokens()) {
    std::cout << "Token: " << token.value 
              << " Type: " << static_cast<int>(token.type) << "\n";
}
```

## Common Pitfalls

1. **Forgetting DepthGuard**: Always use in recursive functions
2. **Not handling nullptr**: Check parse results
3. **Breaking tokenizer**: It's PROTECTED - don't modify
4. **Manual memory management**: Use arena only
5. **Ignoring precedence**: Use precedence climbing for expressions

## Performance Optimization

### 1. Use SIMD Where Possible
```cpp
// Check for SIMD support
#ifdef DB25_HAS_AVX2
    // AVX2 optimized path
#else
    // Scalar fallback
#endif
```

### 2. Minimize Allocations
```cpp
// Good: Reuse arena
auto* node = arena_.allocate<ast::ASTNode>();

// Bad: Dynamic allocation
auto* node = new ast::ASTNode(); // Never do this!
```

### 3. Use String Views
```cpp
// Good: Zero-copy
node->value = std::string_view(token->value);

// Bad: Copy
node->value = std::string(token->value);
```

## Contributing

1. Fork the repository
2. Create feature branch (`git checkout -b feature/amazing-feature`)
3. Write tests first (TDD)
4. Implement feature
5. Ensure all tests pass (`make test`)
6. Run benchmarks if performance-critical
7. Update documentation
8. Submit pull request

## Resources

- [SQL Standard](https://www.iso.org/standard/76583.html)
- [PostgreSQL Parser](https://github.com/postgres/postgres/tree/master/src/backend/parser)
- [DuckDB Parser](https://github.com/duckdb/duckdb/tree/master/src/parser)
- [ANTLR Grammars](https://github.com/antlr/grammars-v4)

## Questions?

Consult the architecture documents in `docs/` or open an issue on GitHub.