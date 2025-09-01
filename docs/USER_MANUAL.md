# DB25 SQL Parser - User Manual

## Overview

DB25 SQL Parser is a high-performance, SIMD-optimized SQL parser written in modern C++23. It provides fast, secure, and extensible SQL parsing with comprehensive AST (Abstract Syntax Tree) generation.

### Key Features
- **Ultra-fast parsing**: ~280,000-300,000 queries/second
- **SIMD-optimized tokenizer**: 4.5x speedup with AVX2/NEON
- **Zero-copy design**: String views throughout
- **Arena memory management**: O(1) allocation, zero fragmentation
- **100% SQL coverage**: Complete SELECT/INSERT/UPDATE/DELETE/DDL support
- **Production-ready**: Extensive test coverage, stress-tested with TPC-C benchmarks

## Installation

### Prerequisites
- C++23 compatible compiler (GCC 13+, Clang 15+, or MSVC 2022+)
- CMake 3.20 or higher
- Python 3.8+ (for code generation tools)
- Google Test (automatically downloaded if BUILD_TESTS=ON)

### Quick Build (Recommended)

```bash
# Clone the repository
git clone https://github.com/yourusername/DB25-parser2.git
cd DB25-parser2

# Use the automated build script
./build.sh

# This will:
# 1. Create build directory
# 2. Configure with CMake (Release mode)
# 3. Build all components including tools
# 4. Run tests to verify installation
```

### Manual Build

```bash
# Create build directory
mkdir build && cd build

# Configure with all features
cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTS=ON \
      -DBUILD_TOOLS=ON \
      -DBUILD_BENCHMARKS=OFF ..

# Build (use all cores)
make -j$(nproc)  # Linux
make -j$(sysctl -n hw.ncpu)  # macOS

# Run tests
ctest --output-on-failure

# Install system-wide (optional)
sudo make install
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | Release | Build type (Debug/Release/RelWithDebInfo) |
| `BUILD_TESTS` | ON | Build test suites |
| `BUILD_TOOLS` | ON | Build AST viewer and benchmarks |
| `BUILD_BENCHMARKS` | OFF | Build Google Benchmark suite |
| `CMAKE_INSTALL_PREFIX` | /usr/local | Installation directory |

## Using the Parser in Your Program

### Basic Usage

```cpp
#include <db25/parser/parser.hpp>
#include <iostream>

int main() {
    // Create parser instance
    db25::parser::Parser parser;
    
    // Parse SQL query
    std::string sql = "SELECT * FROM users WHERE age > 18";
    auto result = parser.parse(sql);
    
    if (result) {
        // Success - access the AST
        auto* ast = result.value();
        std::cout << "Parse successful! Root node: " 
                  << static_cast<int>(ast->type) << std::endl;
        
        // Traverse the AST
        // ... your code here ...
    } else {
        // Error handling
        std::cerr << "Parse error at line " << result.error().line 
                  << ", column " << result.error().column 
                  << ": " << result.error().message << std::endl;
    }
    
    return 0;
}
```

### Linking with CMake

```cmake
# In your CMakeLists.txt
find_package(DB25Parser REQUIRED)
target_link_libraries(your_app PRIVATE db25parser)
```

### Manual Compilation

```bash
g++ -std=c++23 -I/path/to/db25/include your_app.cpp -L/path/to/db25/lib -ldb25parser
```

## Tools and Utilities

### Enhanced AST Viewer

The Enhanced AST Viewer (`ast_viewer_enhanced`) is a professional tool for visualizing Abstract Syntax Trees with vivid colors, comprehensive node information, and batch processing capabilities.

#### Features
- **Vivid color coding**: Different colors for statements, clauses, operators, and values
- **Complete node information**: Shows all node properties including aliases, operators, flags
- **Multiple input modes**: Pipe, file, or interactive
- **Batch processing**: Process all queries in a file with progress indicators
- **Performance metrics**: Parse time, node count, tree depth statistics

#### Usage

```bash
# From pipe with colors
echo "SELECT * FROM users" | ast_viewer_enhanced

# Parse specific query from file (1-based index)
ast_viewer_enhanced --file queries.sqls --index 5

# Parse all queries in file with progress
ast_viewer_enhanced --file queries.sqls --all

# Save output to file (preserves colors with ANSI codes)
ast_viewer_enhanced --file queries.sqls --all --output results.txt

# Without colors (for plain text)
ast_viewer_enhanced --no-color --file queries.sqls --all > plain.txt
```

#### Command Line Options

| Option | Description |
|--------|-------------|
| `--file <path>` | Read SQL from .sqls file |
| `--index <n>` | Parse query at index n (1-based) |
| `--all` | Parse all queries in file |
| `--output <path>` | Save output to file |
| `--no-color` | Disable colored output |
| `--help` | Show help message |

#### Color Scheme

- **Statements** (Red): SELECT, INSERT, UPDATE, DELETE
- **Clauses** (Blue): FROM, WHERE, GROUP BY, ORDER BY
- **Operators** (Purple): =, >, <, AND, OR
- **Values** (Green): Strings, numbers
- **Columns** (Cyan): Column references
- **Tables** (Orange): Table names
- **Functions** (Magenta): COUNT, SUM, AVG

### TPC-C Stress Testing Tool

The TPC-C Stress Runner (`tpcc_stress_runner`) performs comprehensive performance testing using TPC-C benchmark queries.

#### Features
- **Realistic workload**: 44 complex TPC-C queries including transactions, analytics, and reports
- **Performance metrics**: Throughput, latency percentiles, memory usage
- **Warm-up runs**: Ensures consistent measurements
- **Memory stress testing**: Tests parser instance creation and parallel parsing
- **Scalability estimates**: Projects multi-threaded performance

#### Usage

```bash
# Run with default settings (100 iterations)
tpcc_stress_runner

# Custom iterations
tpcc_stress_runner --iterations 1000

# Use custom query file
tpcc_stress_runner --file my_queries.sqls --iterations 500

# Verbose output (show each query result)
tpcc_stress_runner --verbose
```

#### Command Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `--file <path>` | Input file with queries | tests/tpcc_stress_test.sqls |
| `--iterations <n>` | Number of iterations | 100 |
| `--verbose` | Show detailed output | false |
| `--help` | Show help message | - |

#### Performance Metrics

The tool reports:
- **Throughput**: Queries per second
- **Latency**: Min, max, average, and percentiles (50th, 90th, 95th, 99th)
- **AST Metrics**: Node count, tree depth, memory usage
- **Scalability**: Estimated performance with multiple threads

Typical results on modern hardware:
- Single thread: ~280,000-300,000 queries/sec
- Average latency: 2-3 microseconds
- 99th percentile: 10-15 microseconds

### .sqls File Format

DB25 uses a custom `.sqls` format for batch query processing:

```sql
# Comments start with #
# This is a comment

# Query 1: Simple select
SELECT * FROM users
---
# Query 2: Join query
SELECT u.name, o.total 
FROM users u 
JOIN orders o ON u.id = o.user_id
---
# Query 3: Complex aggregation
SELECT department, COUNT(*) as count
FROM employees
GROUP BY department
HAVING COUNT(*) > 10
===  # Triple equals also works as separator
```

Separators:
- `---` (triple dash): Standard query separator
- `===` (triple equals): Alternative separator
- Comments: Lines starting with `#`

## Supported SQL Features

### Data Query Language (DQL)
- SELECT with all standard clauses
- JOINs (INNER, LEFT, RIGHT, FULL, CROSS, LATERAL)
- Subqueries (scalar, IN, EXISTS, correlated)
- CTEs (Common Table Expressions) including recursive
- Window functions with frames
- Set operations (UNION, INTERSECT, EXCEPT)
- GROUPING SETS, CUBE, ROLLUP

### Data Manipulation Language (DML)
- INSERT (VALUES, SELECT, DEFAULT VALUES)
- UPDATE with complex expressions
- DELETE with USING clause
- RETURNING clause support

### Data Definition Language (DDL)
- CREATE TABLE with constraints
- CREATE INDEX, VIEW
- ALTER TABLE operations
- DROP statements with CASCADE

### Expressions
- All standard operators
- CASE expressions (simple and searched)
- CAST operations
- String, date, and math functions
- Window functions
- Aggregate functions

## Performance Characteristics

- **SIMD Tokenization**: 4.5x faster than scalar implementation
- **Zero-copy parsing**: Uses string_view throughout
- **Arena allocation**: O(1) allocation, zero fragmentation
- **Depth protection**: SQLite-inspired guard against stack overflow
- **Parse speed**: ~100,000 queries/second on modern hardware

## Memory Management

The parser uses an arena allocator for all AST nodes:
- No manual memory management required
- Automatic cleanup when parser is destroyed
- Predictable memory usage
- Cache-friendly allocation patterns

## Error Handling

The parser uses a Result type for error handling:

```cpp
auto result = parser.parse(sql);
if (!result) {
    auto error = result.error();
    // error.line - line number (1-based)
    // error.column - column number (1-based)
    // error.message - error description
}
```

## Testing

### Test Suite Overview

DB25 includes comprehensive test suites built with the CMake build system:

```bash
# Build and run all tests
cd build
cmake -DBUILD_TESTS=ON ..
make -j$(nproc)
ctest --output-on-failure
```

### Available Test Executables

After building with `BUILD_TESTS=ON`, the following test executables are available:

| Test Executable | Description | Coverage |
|-----------------|-------------|----------|
| `test_parser_validation` | Core parser validation | Basic SQL statements |
| `test_parser_violations` | Error handling tests | Invalid SQL detection |
| `test_sql_completion` | SQL completion tests | Auto-completion logic |
| `test_tokenizer_verification` | Tokenizer tests | SIMD tokenization |
| `test_ast_comprehensive` | Comprehensive AST tests | All node types |
| `test_ast_dump` | AST dumping tests | Serialization |

### Running Individual Tests

```bash
# Run specific test suite
./build/test_parser_validation
./build/test_ast_comprehensive

# Run with verbose output
./build/test_parser_validation --gtest_verbose

# Run specific test case
./build/test_parser_validation --gtest_filter="ParserTest.SelectBasic"

# List all available tests
./build/test_parser_validation --gtest_list_tests
```

### Test Data Files

Test queries are organized in the `tests/` directory:

| File | Description | Queries |
|------|-------------|---------|
| `showcase_queries.sqls` | Demo queries for all features | 39 |
| `sql_test.sqls` | Comprehensive SQL tests | 100+ |
| `tpcc_stress_test.sqls` | TPC-C benchmark queries | 44 |

### Performance Testing

```bash
# Run stress test with default settings
./build/tpcc_stress_runner

# Extended stress test (1000 iterations)
./build/tpcc_stress_runner --iterations 1000

# Test with custom queries
./build/tpcc_stress_runner --file my_queries.sqls
```

### Generating AST Dumps

```bash
# Generate AST dumps for all showcase queries
./build/ast_viewer_enhanced --file tests/showcase_queries.sqls --all --output /tmp/showcase_ast.txt

# Generate dumps without colors for analysis
./build/ast_viewer_enhanced --no-color --file tests/sql_test.sqls --all > /tmp/sql_test_ast.txt
```

### Memory Testing

The TPC-C stress runner includes memory stress testing:

```bash
# This automatically runs memory tests
./build/tpcc_stress_runner

# Output includes:
# - Creation of 1000 parser instances
# - Memory usage per query
# - Parallel parsing performance
```

## Troubleshooting

### Common Issues

1. **Parse errors on valid SQL**: Check if the feature is supported (see Supported SQL Features)
2. **Memory issues**: Increase arena size if parsing very large queries
3. **Performance**: Ensure Release build, check SIMD support
4. **Linking errors**: Verify include and library paths

### Debug Mode

Build with debug symbols for detailed diagnostics:

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

## Project Documentation

### Documentation Organization

The DB25 project follows a structured documentation architecture:

| Directory | Contents | Purpose |
|-----------|----------|---------|
| `docs/` | All documentation | Centralized documentation |
| `tests/` | Test files and benchmarks | Testing resources |
| `tools/` | Utility programs | Development tools |
| `include/db25/` | Public headers | API reference |
| `external/` | External dependencies | Third-party code |

### Key Documentation Files

Located in `docs/`:

| File | Description |
|------|-------------|
| `USER_MANUAL.md` | This file - comprehensive user guide |
| `ARCHITECTURE.md` | System architecture and design decisions |
| `PERFORMANCE.md` | Performance benchmarks and optimizations |
| `BUILD_INSTRUCTIONS.md` | Detailed build instructions |
| `TESTING.md` | Testing strategy and coverage |
| `PROJECT_LAYOUT.md` | Complete project structure |
| `MIGRATION_GUIDE.md` | Migration from other parsers |
| `PARSER_STATUS_REPORT.md` | Current implementation status |
| `GAP_ANALYSIS.md` | Known limitations and roadmap |

### Examples and Demos

```bash
# View example queries
cat tests/showcase_queries.sqls

# Run examples through AST viewer
./build/ast_viewer_enhanced --file tests/showcase_queries.sqls --all

# Benchmark with real queries
./build/tpcc_stress_runner --file tests/tpcc_stress_test.sqls
```

## Support

### Getting Help

- **Documentation**: Complete docs in `docs/` directory
- **Examples**: See `tests/*.sqls` files
- **Issues**: GitHub issue tracker
- **Performance**: Run `tpcc_stress_runner` for benchmarks

### Common Commands

```bash
# Quick build and test
./build.sh

# View colorful AST
echo "SELECT * FROM users" | ./build/ast_viewer_enhanced

# Run all tests
cd build && ctest

# Stress test
./build/tpcc_stress_runner

# Parse file with progress
./build/ast_viewer_enhanced --file tests/sql_test.sqls --all
```

## License

MIT License - See LICENSE file for details

## Acknowledgments

- **SIMD Tokenizer**: Inspired by simdjson's vectorized parsing techniques
- **Parser Architecture**: Influenced by PostgreSQL and DuckDB design patterns
- **Arena Allocator**: Based on high-performance game engine memory management
- **Expression Parsing**: Pratt parser with precedence climbing
- **Testing Framework**: Google Test for comprehensive coverage
- **Benchmark Suite**: TPC-C for realistic performance testing

## Authors

- Chiradip Mandal - Space-RF.org
- DB25 Project Contributors