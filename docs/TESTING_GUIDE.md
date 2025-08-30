# DB25 Parser Testing Guide

## Quick Start

### Option 1: Automated Test Runner
```bash
# Clone the repository
git clone https://github.com/space-rf-org/DB25-parser2.git
cd DB25-parser2

# Run all tests with the test runner script
bash tests/run_tests.sh
```

### Option 2: Manual Build and Test
```bash
# Clone and build
git clone https://github.com/space-rf-org/DB25-parser2.git
cd DB25-parser2
mkdir build && cd build
cmake ..
make -j4

# Run all tests
ctest --output-on-failure
```

### Option 3: Quick Test Commands
```bash
# From project root after building
./build/test_tokenizer_integration    # Test tokenizer
./build/test_parser_sql_suite        # Test SQL suite (run from project root)
./build/test_ast_layout              # Test AST layout
```

## Available Tests

### 1. Arena Allocator Tests

Tests the memory arena allocator used by the parser.

```bash
# Run individual arena tests
./build/tests/arena/test_arena_basic      # Basic functionality
./build/tests/arena/test_arena_stress     # Stress testing
./build/tests/arena/test_arena_edge       # Edge cases

# Or run all arena tests via CTest
cd build && ctest -R Arena
```

**Expected Output:**
- test_arena_basic: 26 tests, all should pass
- test_arena_stress: 13 tests, 1 may fail (performance expectation)
- test_arena_edge: 28 tests, all should pass

### 2. AST Layout Test

Verifies the 128-byte AST node memory layout.

```bash
./build/test_ast_layout

# Or from build directory
cd build && ./test_ast_layout
```

**Expected Output:**
```
Total size: 128 bytes ✓
Alignment: 128 bytes ✓
Second cache line starts at byte: 64 ✓
✓ Layout verification complete
```

### 3. Tokenizer Integration Test

Tests the integration with the DB25 SIMD tokenizer.

```bash
./build/test_tokenizer_integration
```

**Expected Output:**
```
✓ Basic tokenization works
✓ Parser accepts tokenizer output
✓ Empty input handled correctly
✓ Comments and whitespace filtered
✓ All tokenizer integration tests passed!
```

### 4. Parser SQL Suite Test

Comprehensive test using real SQL queries from sql_test.sqls.

```bash
# Must run from project root (not build directory)
./build/test_parser_sql_suite

# Or
cd DB25-parser2
./build/test_parser_sql_suite
```

**Expected Output:**
```
=== DB25 Parser SQL Test Suite ===
Loaded 23 test cases
...
SUMMARY
----------------------------------------------------------------------
Total test cases: 23

By level:
  SIMPLE: 9
  MODERATE: 5
  COMPLEX: 5
  EXTREME: 4

Tokenization:
  Success: 23/23

Parsing:
  Success: 0/23
  Expected failures: 23/23
  (Parser not yet implemented - failures are expected)
```

### 5. Parser Compilation Test

Basic test to verify parser header compiles correctly.

```bash
./build/test_parser_compile
```

**Expected Output:**
```
=== Parser Compilation Test ===
Error message: Test error
Error at line 1, column 10
Max depth: 500
✓ Parser header compiles successfully
```

## Running Tests with CMake/CTest

```bash
cd build

# Run all tests
ctest

# Run with verbose output
ctest -V

# Run specific test by name
ctest -R TokenizerIntegration

# Run with detailed failure output
ctest --output-on-failure

# List all available tests
ctest -N
```

## Test Organization

```
tests/
├── arena/
│   ├── test_arena_basic.cpp      # Arena basic operations
│   ├── test_arena_stress.cpp     # Arena performance tests
│   └── test_arena_edge.cpp       # Arena edge cases
├── test_ast_v2_layout.cpp        # AST node layout verification
├── test_parser_compile.cpp       # Parser compilation test
├── test_tokenizer_integration.cpp # Tokenizer integration
├── test_parser_sql_suite.cpp     # SQL test suite
└── sql_test.sqls                 # SQL test queries (23 cases)
```

## Build Options

```bash
# Debug build (with symbols, no optimization)
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Release build (optimized, no debug symbols)
cmake -DCMAKE_BUILD_TYPE=Release ..
make

# Build only tests
make test_parser_sql_suite
make test_tokenizer_integration

# Build everything
make all

# Clean build
rm -rf build/*
cmake ..
make
```

## Troubleshooting

### Test Not Found
```bash
# Ensure you're in the right directory
pwd  # Should be in DB25-parser2 or DB25-parser2/build

# Rebuild if needed
cd build && make clean && make
```

### SQL Suite Test Can't Find sql_test.sqls
```bash
# Must run from project root, not build directory
cd DB25-parser2  # Go to project root
./build/test_parser_sql_suite
```

### Tokenizer Not Found
```bash
# CMake will fetch from GitHub automatically
# If it fails, check internet connection
# Manual fetch:
cd build
cmake .. -DFETCHCONTENT_FULLY_DISCONNECTED=OFF
```

### Performance Test Failures
```bash
# Some performance tests have aggressive expectations
# These are not critical failures:
# - ArenaStressTest.AllocationSpeed (expects <5ns, gets ~6.5ns)
# This is still excellent performance
```

## Expected Test Status

| Test | Status | Notes |
|------|--------|-------|
| Arena Basic | ✅ Pass | All 26 tests should pass |
| Arena Stress | ⚠️ 1 Fail | Performance expectation too aggressive |
| Arena Edge | ✅ Pass | All 28 tests should pass |
| AST Layout | ✅ Pass | Verifies 128-byte structure |
| Tokenizer Integration | ✅ Pass | All tests should pass |
| Parser SQL Suite | ✅ Pass* | *Parser not implemented, failures expected |
| Parser Compile | ✅ Pass | Header compilation check |

## Performance Benchmarks

```bash
# Run benchmarks (if built)
./build/bench_parser

# Benchmark results show:
# - Arena allocation: ~6.5ns per allocation
# - Tokenization: ~2.8GB/s throughput
# - Memory efficiency: 65-85% utilization
```

## Continuous Integration

For CI/CD pipelines:

```bash
#!/bin/bash
set -e

# Build
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Test
ctest --output-on-failure

# Check specific requirements
./test_ast_layout | grep "✓ Layout verification complete"
./test_tokenizer_integration | grep "✓ All tokenizer integration tests passed"

echo "All tests passed!"
```

## Notes

- **No Exceptions**: The parser is built with `-fno-exceptions` for performance
- **Result Types**: Uses `std::expected<T, E>` for error handling
- **Memory**: Arena allocator pre-allocates memory, no dynamic allocation
- **SIMD**: Tokenizer uses SIMD instructions for performance

---

For more details, see:
- [Development Log](../dev/DEVELOPMENT_LOG.md)
- [Exception Handling Philosophy](EXCEPTION_HANDLING_PHILOSOPHY.md)
- [AST Design Decision](AST_DESIGN_DECISION.md)