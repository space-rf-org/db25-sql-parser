# DB25 SQL Parser

C++23 · CMake 3.20+ · SIMD · MIT

A SIMD-optimized SQL parser with AST generation.

[Features](#features) · [Quick Start](#quick-start) · [Documentation](#documentation) · [Performance](#performance) · [Contributing](#contributing)

## Features

- Fast: SIMD-optimized tokenizer with a 4.5x speedup over the scalar path
- Broad SQL coverage: CTEs, window functions, and more
- Depth protection: SQLite-inspired guard against stack overflow
- Extensible: clean architecture for adding features
- AST viewer: tree output for inspecting parsed queries
- Zero-copy: string_view used throughout to reduce allocations
- Modern C++23

## Quick Start

### Prerequisites

- **GCC 14+** — the supported and CI-tested toolchain (Release + Debug, plus
  AddressSanitizer/UBSan). This is the single baseline the project builds and
  tests against.
  - Other C++23 compilers (Clang 16+, Apple Clang 16+, MSVC 2022+) may work but
    are **not exercised in CI** and are not currently supported.
- CMake 3.20 or higher
- SIMD support (SSE4.2 minimum, AVX2 recommended)

### Build and Install

```bash
# Clone the repository WITH the SIMD tokenizer submodule (external/tokenizer)
git clone --recursive https://github.com/space-rf-org/db25-sql-parser.git
cd db25-sql-parser
# (already cloned without --recursive? run: git submodule update --init --recursive)

# Configure + build (tests and tools are ON by default)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Run the full test suite
ctest --test-dir build --output-on-failure

# Inspect a query's AST (tools land in build/ - see "AST Viewer" below)
echo "SELECT id, name FROM users WHERE age > 18" | ./build/ast_viewer_enhanced

# Install (optional)
sudo cmake --install build
```

### Basic Usage

```cpp
#include <db25/parser/parser.hpp>
#include <iostream>

int main() {
    db25::parser::Parser parser;
    
    auto result = parser.parse("SELECT * FROM users WHERE age > 18");
    
    if (result) {
        std::cout << "Parse successful!" << std::endl;
        // Work with AST...
    } else {
        std::cerr << "Parse error: " << result.error().message << std::endl;
    }
    
    return 0;
}
```

### AST Viewer

Visualize SQL queries as syntax trees (`build/ast_viewer_enhanced`, built by default):

```bash
# From stdin
echo "SELECT * FROM users" | ./build/ast_viewer_enhanced

# From a .sqls file (queries separated by `---`), one query or all of them
./build/ast_viewer_enhanced --file tests/showcase_queries.sqls --index 1
./build/ast_viewer_enhanced --file tests/showcase_queries.sqls --all --stats
```

For a **Graphviz** rendering of the AST (figures / docs), `build/ast_to_dot` emits DOT:

```bash
echo "SELECT name, RANK() OVER (PARTITION BY dept ORDER BY salary) FROM emp" \
  | ./build/ast_to_dot | dot -Tsvg -o ast.svg
```

<details>
<summary>Example AST Output (click to expand)</summary>

```
╔══════════════════════════════════════════════════════════════╗
║          DB25 SQL Parser - AST Viewer v1.0                  ║
╚══════════════════════════════════════════════════════════════╝

SQL: SELECT name, RANK() OVER (PARTITION BY dept ORDER BY salary DESC) FROM employees

Abstract Syntax Tree:
──────────────────────────────────────────────────────────────
└─ SELECT [#1, 2 children]
   ├─ SELECT LIST [#3, 2 children]
   │  ├─ COLUMN: name [#2]
   │  └─ WINDOW FUNC: RANK [#4, 1 children]
   │     └─ WINDOW [#5, 2 children]
   │        ├─ PARTITION BY [#6, 1 children]
   │        │  └─ COLUMN: dept [#7]
   │        └─ ORDER BY [#9, 1 children]
   │           └─ COLUMN: salary [#8, DESC]
   └─ FROM [#11, 1 children]
      └─ TABLE: employees [#10]
──────────────────────────────────────────────────────────────
```

</details>

## Supported SQL Features

### Data Query Language (DQL)
- SELECT with all standard clauses
- JOINs (INNER, LEFT, RIGHT, FULL, CROSS, LATERAL)
- Subqueries (scalar, IN, EXISTS, correlated)
- CTEs including recursive
- Window functions with frames
- Set operations (UNION, INTERSECT, EXCEPT)
- GROUPING SETS, CUBE, ROLLUP

### Data Manipulation Language (DML)
- INSERT (VALUES, SELECT, DEFAULT VALUES)
- UPDATE with complex expressions
- DELETE with USING clause
- RETURNING clause

### Data Definition Language (DDL)
- CREATE TABLE/INDEX/VIEW
- ALTER TABLE
- DROP with CASCADE

### Advanced Features
- CASE expressions
- CAST operations
- Complex expressions with precedence
- String, date, and math functions
- Aggregate functions

## Performance

| Component | Performance | Notes |
|-----------|------------|-------|
| Tokenizer | 4.5x faster | SIMD-optimized |
| Parser | ~100K queries/sec | O(1) dispatch |
| Memory | Zero fragmentation | Arena allocator |
| Depth | 1001 levels | Stack protection |

## Documentation

- [User Manual](docs/USER_MANUAL.md) - Usage guide
- [Developer Guide](docs/DEVELOPER_GUIDE.md) - Architecture and extension guide
- [Examples](tests/showcase_queries.sqls) - SQL examples (drive them through `ast_viewer_enhanced`)

## Project Structure

```
db25-sql-parser/
├── include/db25/         # Public headers
│   ├── ast/             # AST node definitions
│   ├── parser/          # Parser interface
│   └── memory/          # Memory management
├── src/                 # Implementation
│   ├── parser/          # Parser implementation
│   ├── ast/             # AST utilities
│   └── memory/          # Arena allocator
├── external/            # Submodules
│   └── tokenizer/       # SIMD tokenizer (protected)
├── tests/               # Test suites & test data
│   ├── showcase_queries.sqls  # Example queries
│   ├── test_*.cpp       # Unit tests
│   └── test_all_queries.sh    # Batch testing script
├── tools/               # Tool source code
│   ├── ast_viewer_enhanced.cpp  # AST tree viewer (build/ast_viewer_enhanced)
│   └── ast_to_dot.cpp           # Graphviz DOT emitter (build/ast_to_dot)
├── docs/                # Documentation (USER_MANUAL, DEVELOPER_GUIDE)
└── build/               # Build output, incl. the compiled tools (git-ignored)
```

## Building and Testing

### Clean Build from Scratch

```bash
# Ensure the tokenizer submodule is present
git submodule update --init --recursive

# Configure (tests + tools ON by default) and build everything
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_TOOLS=ON
cmake --build build -j

# Run all tests (authoritative; lists every case)
ctest --test-dir build --output-on-failure

# Run one suite (or a name pattern) directly
ctest --test-dir build -R test_parser_negative --output-on-failure
```

### Sanitizer / Debug Build

CI builds under AddressSanitizer + UBSan; reproduce locally with:

```bash
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-san -j && ctest --test-dir build-san --output-on-failure
```

## Contributing

We welcome contributions! Please see our [Developer Guide](docs/DEVELOPER_GUIDE.md) for details on:

- Architecture overview
- Adding new features
- Testing guidelines
- Code style
- Performance optimization

### Quick Contribution Guide

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Write tests first (TDD approach)
4. Implement your feature
5. Ensure all tests pass (`make test`)
6. Update documentation
7. Submit a Pull Request

## Performance Benchmarks

<details>
<summary>Benchmark Results (click to expand)</summary>

```
System: Apple M1 Pro, 32GB RAM
Compiler: Apple Clang 17.0.0
Build: Release with -O3 -march=native

Tokenization Performance:
- Scalar:    220,000 tokens/sec
- SSE4.2:    550,000 tokens/sec
- AVX2:      880,000 tokens/sec
- AVX512:    990,000 tokens/sec

Parse Performance:
- Simple SELECT:     1.2 μs
- Complex JOIN:      8.5 μs
- Recursive CTE:     15.3 μs
- Window Functions:  12.7 μs

Memory Usage:
- Node Size:         64 bytes
- Arena Block:       4 MB
- Fragmentation:     0%
```

</details>

## Architecture Highlights

- **Zero-Copy Design**: String views throughout for minimal allocations
- **SIMD Tokenizer**: Runtime CPU detection for optimal performance
- **Arena Allocation**: Fast, cache-friendly memory management
- **Depth Protection**: Safe recursive descent with stack guards
- **Modern C++23**: Concepts, ranges, and advanced template features

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Tokenizer SIMD optimizations inspired by [simdjson](https://github.com/simdjson/simdjson)
- Parser architecture influenced by PostgreSQL and DuckDB
- Arena allocator based on game engine techniques
- Depth guard pattern from SQLite

## Support

- [Report Issues](https://github.com/space-rf-org/db25-sql-parser/issues)
- [Request Features](https://github.com/space-rf-org/db25-sql-parser/issues)
- Contact: chiradip@chiradip.com

---

Built with modern C++23.

[Back to Top](#db25-sql-parser)
