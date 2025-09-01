# DB25 Parser Architecture

## Overview

DB25 Parser is a high-performance, SIMD-optimized SQL parser built with modern C++23. It emphasizes performance through hardware acceleration, security through depth protection, and clean architecture through separation of concerns.

## Core Architecture Principles

### 1. **Layered Architecture**
```
┌─────────────────────────────────────┐
│         Application Layer           │
│         (User Interface)            │
├─────────────────────────────────────┤
│          Parser Layer               │
│    (Syntactic Analysis, AST)       │
├─────────────────────────────────────┤
│        Tokenizer Layer              │
│   (Lexical Analysis, SIMD)         │  ← Protected/Frozen
├─────────────────────────────────────┤
│      Memory Management              │
│      (Arena Allocator)              │  ← Protected/Frozen
└─────────────────────────────────────┘
```

### 2. **Component Separation**

#### Protected Components (DO NOT MODIFY)
- **SIMD Tokenizer** (`DB25-sql-tokenizer2`)
  - Handles all lexical analysis
  - Keyword detection and classification
  - SIMD-optimized character processing
  - Produces standardized token stream

- **Arena Allocator** (`include/db25/memory/arena.hpp`)
  - Lock-free memory allocation
  - Zero fragmentation
  - Bulk deallocation

#### Parser Components
- **Recursive Descent Parser** (`src/parser/parser.cpp`)
  - Statement dispatch with O(1) routing
  - Pratt expression parsing with precedence
  - Special function handlers (CAST, EXTRACT)
  - DepthGuard protection

- **AST Nodes** (`include/db25/ast/ast_node.hpp`)
  - 128-byte cache-aligned nodes
  - Efficient parent-child-sibling structure
  - Zero-copy string views

## Performance Optimizations

### 1. **SIMD Tokenization** (4.5x speedup)
- Processes 16-64 bytes in parallel
- Automatic CPU detection (SSE4.2, AVX2, AVX-512, ARM NEON)
- Zero-copy tokenization using string_views

### 2. **Hardware Prefetching** (10-20% improvement)
```cpp
// Prefetch upcoming tokens during parsing
__builtin_prefetch(&tokens[pos + 1], 0, 3);  // High temporal locality
__builtin_prefetch(&tokens[pos + 2], 0, 2);  // Medium temporal locality
__builtin_prefetch(&tokens[pos + 3], 0, 1);  // Low temporal locality
```

### 3. **Arena Memory Allocation** (1.8x speedup)
- No per-node malloc overhead
- Improved cache locality
- Bulk deallocation

### 4. **Expression Parsing Optimization**
- Pratt parsing with precedence climbing
- Operator precedence table
- Early termination for clause keywords

## Security Features

### DepthGuard Implementation
Prevents stack overflow attacks and infinite recursion:

```cpp
class DepthGuard {
    Parser* parser_;
    bool valid_;
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
};
```

- Default limit: 1000 levels
- RAII-based automatic management
- Works with `-fno-exceptions`

## Parser Design Patterns

### 1. **Early Detection Pattern**
Special SQL functions are detected early and delegated to specialized parsers:

```cpp
if (current_token_->value == "CAST") {
    return parse_cast_expression();
}
if (current_token_->value == "EXTRACT") {
    return parse_extract_expression();
}
```

### 2. **Token Lifetime Management**
The tokenizer is kept alive after successful parsing to maintain string_view validity:

```cpp
auto result = parser.parse(sql);
if (result.has_value()) {
    // Tokenizer kept alive, string_views remain valid
    return result.value();
}
```

### 3. **Clause Termination**
Expression parsing correctly terminates at SQL clause keywords:

```cpp
bool is_clause_keyword(const Token* token) {
    return token->value == "FROM" || 
           token->value == "WHERE" ||
           token->value == "GROUP" ||
           // ... other clause keywords
}
```

## SQL Feature Coverage

### Fully Supported (100%)
- **DML**: SELECT, INSERT, UPDATE, DELETE
- **DDL**: CREATE (TABLE/INDEX/VIEW), DROP, ALTER, TRUNCATE
- **Joins**: INNER, LEFT, RIGHT, FULL OUTER, CROSS
- **CTEs**: WITH, WITH RECURSIVE
- **Expressions**: Arithmetic, logical, comparison, string concatenation
- **Functions**: Aggregates, CAST, EXTRACT, CASE
- **Operators**: BETWEEN, IN, LIKE, EXISTS, IS NULL
- **Set Operations**: UNION, UNION ALL, INTERSECT, EXCEPT

### Grammar Coverage
- ~92% of EBNF grammar implemented
- Handles production SQL queries
- Suitable for query analysis and optimization

## Directory Structure

```
DB25-parser2/
├── include/db25/
│   ├── parser/
│   │   ├── parser.hpp           # Main parser interface
│   │   └── tokenizer_adapter.hpp # Tokenizer integration
│   ├── ast/
│   │   └── ast_node.hpp         # AST node definitions
│   └── memory/
│       └── arena.hpp             # Arena allocator
├── src/
│   ├── parser/
│   │   ├── parser.cpp           # Parser implementation
│   │   └── tokenizer_adapter.cpp
│   ├── ast/
│   │   └── ast_node.cpp
│   └── memory/
│       └── arena.cpp
├── tests/                       # Comprehensive test suite
├── build/                       # CMake build directory
└── docs/                        # Documentation

DB25-sql-tokenizer2/             # External dependency (frozen)
```

## Build Configuration

### Requirements
- C++23 compiler (Clang 15+, GCC 13+)
- CMake 3.20+
- Native architecture optimization (`-march=native`)

### Build Commands
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### Compiler Flags
- `-std=c++23` - Modern C++ features
- `-O3` - Maximum optimization
- `-march=native` - CPU-specific optimizations
- `-fno-exceptions` - No exception overhead

## Testing Strategy

### Test Categories
1. **Unit Tests** - Component-level testing
2. **Integration Tests** - Parser-tokenizer integration
3. **Performance Tests** - Benchmark suite
4. **Security Tests** - DepthGuard validation
5. **Compatibility Tests** - EBNF grammar coverage

### Test Coverage
- 21 test suites
- 200+ individual tests
- 92% EBNF grammar coverage
- 100% success rate on supported features

## Performance Metrics

| Component | Optimization | Impact |
|-----------|-------------|--------|
| Tokenizer | SIMD | 4.5x faster |
| Parser | Prefetching | 10-20% faster |
| Memory | Arena allocator | 1.8x faster |
| Expressions | Pratt parsing | 1.4x faster |
| Overall | Combined | 100+ MB/s throughput |

## Future Enhancements

### Proposed (Filed with Tokenizer)
- Hardware CRC32 for keyword matching (5-10x speedup)
- SIMD operator precedence tables
- Parallel statement parsing

### Parser Improvements
- Incremental parsing for IDE integration
- Error recovery and suggestions
- Query optimization hints
- Parallel AST validation

## Design Decisions

### Why Frozen Components?
- **Tokenizer**: Stable API, extensively tested, SIMD-optimized
- **Arena**: Core memory management, used throughout
- **Changes require**: Full regression testing, performance validation

### Why Recursive Descent?
- Natural mapping to SQL grammar
- Excellent error messages
- Easy to maintain and extend
- Predictable performance

### Why Not Use Exceptions?
- Embedded system compatibility
- Predictable performance
- Reduced binary size
- RAII still provides safety

## Integration Guide

### Basic Usage
```cpp
#include "db25/parser/parser.hpp"

db25::parser::Parser parser;
auto result = parser.parse("SELECT * FROM users");

if (result.has_value()) {
    auto* ast = result.value();
    // Process AST
} else {
    std::cerr << "Error: " << result.error().message << std::endl;
}
```

### Custom Configuration
```cpp
db25::parser::ParserConfig config;
config.max_depth = 500;  // Adjust recursion limit
parser.set_config(config);
```

## Maintenance Notes

### Adding New SQL Features
1. Update grammar in EBNF specification
2. Add parsing function to parser.cpp
3. Add AST node type if needed
4. Write comprehensive tests
5. Update documentation

### Performance Testing
Always benchmark after changes:
```bash
./test_performance
./test_prefetch_performance
```

### Security Validation
Verify DepthGuard protection:
```bash
./test_depth_guard
```

---

*Last Updated: 2025-08-30*
*Version: 2.0*