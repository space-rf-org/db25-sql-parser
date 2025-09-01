# DB25 Parser - Final Design Architecture

## Core Design Principles

1. **Hand-written Recursive Descent** - Maximum control and performance
2. **Pratt Parser for Expressions** - Elegant precedence handling
3. **Arena Allocation** - Zero-overhead memory management
4. **Zero Exceptions** - std::expected for error handling
5. **Test-Driven Development** - Tests define behavior

## Parser Architecture

### Three-Layer Design

```
┌─────────────────────────────────────┐
│         Token Stream Layer           │
│    (Tokenizer + Adapter + Filter)    │
└────────────┬────────────────────────┘
             │
┌────────────▼────────────────────────┐
│         Parser Core Layer            │
│  (Recursive Descent + Pratt Parser)  │
└────────────┬────────────────────────┘
             │
┌────────────▼────────────────────────┐
│         AST Generation Layer         │
│    (Arena-Allocated 128B Nodes)      │
└─────────────────────────────────────┘
```

## Implementation Strategy

### 1. Token Stream Management

```cpp
class Parser {
    tokenizer::Tokenizer* tokenizer_;    // SIMD tokenizer from GitHub
    const tokenizer::Token* current_;    // Current token
    const tokenizer::Token* peek_;       // Lookahead token
    
    void advance() {
        tokenizer_->advance();
        current_ = tokenizer_->current();
        peek_ = tokenizer_->peek();
    }
    
    bool match(TokenType type) {
        if (check(type)) {
            advance();
            return true;
        }
        return false;
    }
    
    bool check(TokenType type) const {
        return current_ && current_->type == type;
    }
    
    void consume(TokenType type, const char* msg) {
        if (!match(type)) {
            error(msg);
        }
    }
};
```

### 2. Statement Parser (Recursive Descent)

```cpp
ASTNode* Parser::parse_statement() {
    // Dispatch based on first token
    switch (current_->type) {
        case TOKEN_SELECT:
            return parse_select_stmt();
        case TOKEN_INSERT:
            return parse_insert_stmt();
        case TOKEN_UPDATE:
            return parse_update_stmt();
        case TOKEN_DELETE:
            return parse_delete_stmt();
        case TOKEN_CREATE:
            return parse_create_stmt();
        default:
            error("Expected statement keyword");
    }
}

ASTNode* Parser::parse_select_stmt() {
    consume(TOKEN_SELECT, "Expected SELECT");
    
    // Handle DISTINCT
    bool distinct = match(TOKEN_DISTINCT);
    
    // Parse select list
    auto* select_list = parse_select_list();
    
    // FROM clause
    consume(TOKEN_FROM, "Expected FROM");
    auto* from_clause = parse_from_clause();
    
    // Optional clauses
    auto* where_clause = nullptr;
    if (match(TOKEN_WHERE)) {
        where_clause = parse_where_clause();
    }
    
    auto* group_by = nullptr;
    if (match(TOKEN_GROUP)) {
        consume(TOKEN_BY, "Expected BY after GROUP");
        group_by = parse_group_by_clause();
    }
    
    auto* having = nullptr;
    if (match(TOKEN_HAVING)) {
        having = parse_having_clause();
    }
    
    auto* order_by = nullptr;
    if (match(TOKEN_ORDER)) {
        consume(TOKEN_BY, "Expected BY after ORDER");
        order_by = parse_order_by_clause();
    }
    
    auto* limit = nullptr;
    if (match(TOKEN_LIMIT)) {
        limit = parse_limit_clause();
    }
    
    // Build AST node
    auto* node = make_node(NodeType::SelectStmt);
    node->data.select_stmt.distinct = distinct;
    node->add_child(select_list);
    node->add_child(from_clause);
    if (where_clause) node->add_child(where_clause);
    if (group_by) node->add_child(group_by);
    if (having) node->add_child(having);
    if (order_by) node->add_child(order_by);
    if (limit) node->add_child(limit);
    
    return node;
}
```

### 3. Expression Parser (Pratt Algorithm)

```cpp
// Precedence table
enum Precedence {
    PREC_NONE = 0,
    PREC_OR = 1,        // OR
    PREC_AND = 2,       // AND
    PREC_NOT = 3,       // NOT
    PREC_COMP = 4,      // =, <>, <, >, <=, >=
    PREC_TERM = 5,      // +, -
    PREC_FACTOR = 6,    // *, /, %
    PREC_UNARY = 7,     // -, +, NOT
    PREC_CALL = 8,      // Function calls
    PREC_PRIMARY = 9    // Literals, identifiers
};

int Parser::get_precedence(TokenType type) const {
    switch (type) {
        case TOKEN_OR: return PREC_OR;
        case TOKEN_AND: return PREC_AND;
        case TOKEN_NOT: return PREC_NOT;
        case TOKEN_EQUAL:
        case TOKEN_NOT_EQUAL:
        case TOKEN_LESS:
        case TOKEN_GREATER:
        case TOKEN_LESS_EQUAL:
        case TOKEN_GREATER_EQUAL: return PREC_COMP;
        case TOKEN_PLUS:
        case TOKEN_MINUS: return PREC_TERM;
        case TOKEN_STAR:
        case TOKEN_SLASH:
        case TOKEN_PERCENT: return PREC_FACTOR;
        default: return PREC_NONE;
    }
}

ASTNode* Parser::parse_expression(int min_precedence) {
    // Parse prefix/primary expression
    auto* left = parse_primary();
    
    // Parse infix expressions
    while (true) {
        int precedence = get_precedence(current_->type);
        if (precedence < min_precedence) {
            break;
        }
        
        TokenType op = current_->type;
        advance();
        
        // Right-associative operators would use precedence instead of precedence+1
        auto* right = parse_expression(precedence + 1);
        
        // Create binary expression node
        auto* node = make_node(NodeType::BinaryExpr);
        node->data.binary_expr.op = token_to_binary_op(op);
        node->add_child(left);
        node->add_child(right);
        
        left = node;
    }
    
    return left;
}

ASTNode* Parser::parse_primary() {
    // Handle literals
    if (check(TOKEN_NUMBER)) {
        auto* node = make_node(NodeType::IntegerLiteral);
        node->data.integer_literal.value = std::stoll(current_->value);
        advance();
        return node;
    }
    
    if (check(TOKEN_STRING)) {
        auto* node = make_node(NodeType::StringLiteral);
        // Store string in arena
        size_t len = current_->value.length();
        char* str = arena_.allocate<char>(len + 1);
        std::memcpy(str, current_->value.data(), len);
        str[len] = '\0';
        node->data.string_literal.value = str;
        node->data.string_literal.length = len;
        advance();
        return node;
    }
    
    if (check(TOKEN_IDENTIFIER)) {
        std::string_view name = current_->value;
        advance();
        
        // Check for function call
        if (match(TOKEN_LPAREN)) {
            auto* node = make_node(NodeType::FunctionCall);
            // Store function name in arena
            char* fname = arena_.allocate<char>(name.length() + 1);
            std::memcpy(fname, name.data(), name.length());
            fname[name.length()] = '\0';
            node->data.function_call.name = fname;
            
            // Parse arguments
            if (!check(TOKEN_RPAREN)) {
                do {
                    node->add_child(parse_expression());
                } while (match(TOKEN_COMMA));
            }
            consume(TOKEN_RPAREN, "Expected )");
            return node;
        }
        
        // Regular identifier
        auto* node = make_node(NodeType::Identifier);
        char* id = arena_.allocate<char>(name.length() + 1);
        std::memcpy(id, name.data(), name.length());
        id[name.length()] = '\0';
        node->data.identifier.name = id;
        return node;
    }
    
    // Parenthesized expression
    if (match(TOKEN_LPAREN)) {
        auto* expr = parse_expression();
        consume(TOKEN_RPAREN, "Expected )");
        return expr;
    }
    
    // Unary expressions
    if (match(TOKEN_MINUS)) {
        auto* node = make_node(NodeType::UnaryExpr);
        node->data.unary_expr.op = UnaryOp::Minus;
        node->add_child(parse_primary());
        return node;
    }
    
    if (match(TOKEN_NOT)) {
        auto* node = make_node(NodeType::UnaryExpr);
        node->data.unary_expr.op = UnaryOp::Not;
        node->add_child(parse_expression(PREC_NOT));
        return node;
    }
    
    error("Expected expression");
}
```

### 4. Error Handling

```cpp
void Parser::error(const std::string& message) {
    // Since exceptions are disabled, we need another strategy
    // Option 1: Set error state and return nullptr
    // Option 2: Use setjmp/longjmp for non-local exit
    // Option 3: Return error nodes that propagate up
    
    // For now, use std::abort() in development
    std::cerr << "Parse error at " << current_->line << ":" << current_->column 
              << ": " << message << std::endl;
    std::abort();
}

// Better approach: Return Result types
template<typename T>
using Result = std::expected<T, ParseError>;

Result<ASTNode*> Parser::parse_select_stmt_safe() {
    if (!match(TOKEN_SELECT)) {
        return std::unexpected(ParseError{
            current_->line, current_->column,
            "Expected SELECT keyword"
        });
    }
    // ... rest of parsing
}
```

### 5. Memory Management

```cpp
class Parser {
    Arena arena_{64 * 1024};  // 64KB initial size
    
    template<typename... Args>
    ASTNode* make_node(Args&&... args) {
        // Allocate in arena
        void* mem = arena_.allocate(sizeof(ASTNode));
        
        // Construct in-place
        auto* node = new(mem) ASTNode(std::forward<Args>(args)...);
        
        // Assign unique ID
        node->node_id = next_node_id_++;
        
        // Track location
        node->location.line = current_->line;
        node->location.column = current_->column;
        node->location.offset = current_->offset;
        
        return node;
    }
    
    void reset() {
        arena_.reset();  // O(1) deallocation
        next_node_id_ = 1;
        // Reset tokenizer
    }
};
```

## Optimization Strategies

### 1. Token Caching
- Cache frequently checked tokens
- Avoid redundant tokenizer calls
- Prefetch next tokens

### 2. Inline Hot Paths
```cpp
[[gnu::always_inline]] 
inline bool match(TokenType type) {
    // Inline for performance
}
```

### 3. Branch Prediction Hints
```cpp
if [[likely]] (check(TOKEN_IDENTIFIER)) {
    // Common case
} else [[unlikely]] {
    // Error case
}
```

### 4. Memory Pooling
- Pre-allocate common node sizes
- Reuse nodes when possible
- Align nodes to cache lines

### 5. String Interning
```cpp
class StringPool {
    std::unordered_map<std::string_view, const char*> interned_;
    Arena& arena_;
    
    const char* intern(std::string_view str) {
        if (auto it = interned_.find(str); it != interned_.end()) {
            return it->second;  // Already interned
        }
        // Allocate and intern new string
        char* s = arena_.allocate<char>(str.length() + 1);
        std::memcpy(s, str.data(), str.length());
        s[str.length()] = '\0';
        interned_[std::string_view(s, str.length())] = s;
        return s;
    }
};
```

## Testing Strategy

1. **Unit Tests** - Each parser method tested individually
2. **Integration Tests** - Full SQL statements
3. **Fuzz Testing** - Random SQL generation
4. **Performance Tests** - Benchmark against targets
5. **Memory Tests** - Valgrind, ASAN, leak detection

## Performance Targets

- **Simple SELECT**: < 100μs
- **Complex JOIN**: < 1ms  
- **1000 column SELECT**: < 5ms
- **Deep nesting (100 levels)**: < 10ms
- **Memory per query**: < 10KB typical, < 1MB extreme

## Next Implementation Steps

1. **Week 1**: Core SELECT implementation
   - parse_statement() dispatcher
   - parse_select_stmt() 
   - parse_expression() with basic operators
   - parse_primary() for literals and identifiers

2. **Week 2**: Extended SELECT features
   - WHERE clause
   - ORDER BY, GROUP BY, HAVING
   - JOIN support
   - Subqueries

3. **Week 3**: DML statements
   - INSERT, UPDATE, DELETE
   - Multi-row operations
   - INSERT FROM SELECT

4. **Week 4**: DDL and optimization
   - CREATE TABLE
   - Performance tuning
   - Error recovery
   - Documentation

## Success Metrics

- ✅ All tests passing (100+ test cases)
- ✅ < 1ms parse time for typical queries
- ✅ < 10KB memory for typical queries
- ✅ Zero memory leaks
- ✅ Clean, maintainable code
- ✅ Comprehensive documentation