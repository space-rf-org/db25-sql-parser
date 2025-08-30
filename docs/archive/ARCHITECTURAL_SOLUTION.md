# Architectural Solution for SQL Special Function Syntax

## Problem Statement

The EXTRACT function has non-standard syntax that breaks typical function parsing:
```sql
EXTRACT(temporal_part FROM temporal_expression)
```

The keyword `FROM` inside parentheses conflicts with SQL's FROM clause, causing the expression parser to terminate prematurely.

## Architectural Principles Applied

### 1. **Separation of Concerns**
Instead of modifying the general expression parser with special cases, we created a dedicated parser for EXTRACT:
- `parse_extract_expression()` - Handles EXTRACT-specific syntax
- Keeps general expression parsing clean and maintainable
- No pollution of core parsing logic with edge cases

### 2. **Early Detection Pattern**
Special functions are detected early in `parse_primary_expression()`:
```cpp
// Handle CAST expressions
if (current_token_->value == "CAST") {
    return parse_cast_expression();
}

// Handle EXTRACT expressions  
if (current_token_->value == "EXTRACT") {
    return parse_extract_expression();
}
```

### 3. **Extensibility by Design**
The pattern is easily extensible for other special SQL functions:
- POSITION(substring IN string)
- OVERLAY(string PLACING substring FROM start)
- SUBSTRING(string FROM start FOR length)

Each can have its own dedicated parser without affecting others.

### 4. **Clean State Management**
The EXTRACT parser manages its own parsing context:
- Consumes the EXTRACT keyword
- Handles the opening parenthesis
- Parses temporal part (YEAR, MONTH, etc.)
- Explicitly consumes FROM keyword
- Parses the temporal expression
- Validates closing parenthesis

### 5. **Type Safety & AST Consistency**
EXTRACT creates a FunctionCall node with proper structure:
```
FunctionCall("EXTRACT")
├── Identifier("YEAR")     // temporal part
└── Identifier("date_col")  // temporal expression
```

## Implementation Details

### Parser Function Structure
```cpp
ast::ASTNode* Parser::parse_extract_expression() {
    // 1. Consume EXTRACT and opening parenthesis
    advance(); // EXTRACT
    advance(); // (
    
    // 2. Create FunctionCall node
    auto* extract_node = new ASTNode(NodeType::FunctionCall);
    extract_node->primary_text = "EXTRACT";
    
    // 3. Parse temporal part (YEAR, MONTH, etc.)
    auto* temporal_part = parse_identifier();
    
    // 4. Consume FROM keyword explicitly
    advance(); // FROM
    
    // 5. Parse temporal expression
    auto* temporal_expr = parse_identifier_or_column();
    
    // 6. Validate and consume closing parenthesis
    advance(); // )
    
    // 7. Build AST structure
    return extract_node;
}
```

### Design Benefits

1. **Maintainability**: Each special function is isolated in its own parser
2. **Performance**: No runtime overhead for checking special cases in hot paths
3. **Correctness**: FROM keyword doesn't interfere with expression parsing
4. **Extensibility**: New special functions can be added without regression
5. **Clarity**: Code explicitly shows which functions have special syntax

## Alternative Approaches Considered

### 1. Context Flags (Rejected)
```cpp
bool in_extract_function = false;
// Would require threading context through all expression parsing
```
**Rejected because**: Adds complexity to all expression parsing paths

### 2. Token Lookahead (Rejected)
```cpp
if (function_name == "EXTRACT") {
    // Special handling with complex lookahead
}
```
**Rejected because**: Increases coupling between tokenizer and parser

### 3. Grammar Modification (Rejected)
Modifying the expression grammar to handle FROM differently in function contexts.
**Rejected because**: Would complicate the grammar significantly

## Future Extensions

This architectural pattern can handle other SQL special functions:

### POSITION
```cpp
ast::ASTNode* Parser::parse_position_expression() {
    // POSITION(substring IN string)
}
```

### OVERLAY
```cpp
ast::ASTNode* Parser::parse_overlay_expression() {
    // OVERLAY(string PLACING substring FROM start)
}
```

### SUBSTRING
```cpp
ast::ASTNode* Parser::parse_substring_expression() {
    // SUBSTRING(string FROM start FOR length)
}
```

## Conclusion

By applying the **Single Responsibility Principle** and **Open/Closed Principle**, we've created a clean, extensible solution that:
- Solves the immediate EXTRACT problem
- Provides a pattern for future special functions
- Maintains parser performance and clarity
- Avoids polluting core parsing logic

This is proper architectural thinking: solving not just the immediate problem, but creating a sustainable pattern for future similar problems.