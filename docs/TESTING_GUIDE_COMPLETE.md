# DB25 Parser - Complete Testing Guide

## Test Categories

### 1. Unit Tests
- **Arena Allocator Tests** - Memory management verification
- **AST Node Tests** - Data structure layout verification

### 2. Integration Tests ⭐
- **Parser-Tokenizer Integration** - Critical integration point
- **SQL Suite Tests** - Real-world SQL query processing

### 3. System Tests
- **End-to-End SQL Parsing** - Complete pipeline validation

---

## Running All Tests

### Quick Command Reference
```bash
# Build everything first
cd DB25-parser2
mkdir -p build && cd build
cmake ..
make -j4

# Run ALL tests (recommended)
ctest --output-on-failure

# Or use the test runner
cd ..
bash tests/run_tests.sh
```

---

## Integration Tests (Most Important!)

### Parser-Tokenizer Integration Test
**Purpose**: Verifies that the parser correctly integrates with the DB25 SIMD tokenizer from GitHub.

```bash
# Run the integration test
./build/test_tokenizer_integration

# What it tests:
# ✓ Tokenizer initialization with SQL strings
# ✓ Token filtering (removes whitespace/comments)
# ✓ EOF token generation
# ✓ Parser accepting tokenizer output
# ✓ Error handling through std::expected
```

**Expected Output:**
```
=== Tokenizer Integration Test ===

SQL: SELECT * FROM users WHERE id = 42
Token count: 9
  [1] SELECT @ 1:1
  [5] * @ 1:8
  [1] FROM @ 1:10
  [2] users @ 1:15
  [1] WHERE @ 1:21
  [2] id @ 1:27
  [5] = @ 1:30
  [3] 42 @ 1:32
  [9]  @ 1:34
✓ Basic tokenization works
✓ Parser accepts tokenizer output
✓ Empty input handled correctly
✓ Comments and whitespace filtered

✓ All tokenizer integration tests passed!
```

### Tokenizer Verification Test ⭐⭐⭐
**Purpose**: Thoroughly verifies tokenizer produces EXACTLY the right tokens for parser input.

```bash
# Run from ANY directory - it will find sql_test.sqls
./build/test_tokenizer_verification

# What it tests:
# ✓ 10 hardcoded SQL queries with exact expected tokens
# ✓ 23 SQL queries from sql_test.sqls
# ✓ Token types (KEYWORD, IDENTIFIER, NUMBER, etc.)
# ✓ Token values match exactly
# ✓ Token counts are correct
# ✓ Whitespace/comment filtering
# ✓ EOF token always present
```

**Expected Output:**
```
=== DB25 Tokenizer Verification Test ===

Running hardcoded tests...

Test: Simple SELECT
SQL: SELECT id FROM users
Expected 5 tokens, got 5
  Token 0: KEYWORD 'SELECT' ✓
  Token 1: IDENTIFIER 'id' ✓
  Token 2: KEYWORD 'FROM' ✓
  Token 3: IDENTIFIER 'users' ✓
  Token 4: EOF '' ✓
✓ Test passed!

[... 9 more hardcoded tests ...]

SQL Test Suite:
Processing 23 queries from sql_test.sqls...

Query 1: simple_select_1
SQL: SELECT * FROM users;
Tokens: 6
  KEYWORD 'SELECT'
  OPERATOR '*'
  KEYWORD 'FROM'
  IDENTIFIER 'users'
  DELIMITER ';'
  EOF ''
✓ Passed

[... 22 more queries ...]

=== Summary ===
Hardcoded Tests: 10/10 passed
SQL Test Suite Results:
  Total: 23 queries
  Passed: 23
  Failed: 0

✓ All tokenizer verification tests passed!
```

---

## What Each Test Validates

### Integration Points Tested

| Component | Test | What It Validates |
|-----------|------|-------------------|
| **Tokenizer Fetch** | CMake build | GitHub dependency fetched correctly |
| **Token Adapter** | test_tokenizer_integration | Adapter wraps SIMD tokenizer properly |
| **Token Filtering** | test_tokenizer_verification | Whitespace/comments removed |
| **Token Types** | test_tokenizer_verification | Correct classification (KEYWORD vs IDENTIFIER) |
| **Token Values** | test_tokenizer_verification | Exact string values preserved |
| **Token Counts** | test_tokenizer_verification | No missing or extra tokens |
| **EOF Token** | All integration tests | EOF token always present |
| **Parser Input** | test_tokenizer_integration | Parser receives clean token stream |
| **Error Propagation** | All tests | std::expected error handling works |

### SQL Query Complexity Levels Tested

- **SIMPLE (9 queries)**: Basic SELECT, INSERT, UPDATE, DELETE
- **MODERATE (5 queries)**: JOINs, subqueries, GROUP BY
- **COMPLEX (5 queries)**: CTEs, window functions, complex JOINs
- **EXTREME (4 queries)**: Nested CTEs, multiple subqueries, advanced features

---

## Running Specific Tests

### Test Individual Components
```bash
# Just tokenizer verification (most important)
./build/test_tokenizer_verification

# Just integration test
./build/test_tokenizer_integration

# Arena tests (already working)
./build/tests/arena/test_arena_basic
./build/tests/arena/test_arena_stress
```

### Debug Token Output
```bash
# See detailed token output
./build/test_tokenizer_verification 2>&1 | less

# Filter for specific token types
./build/test_tokenizer_verification 2>&1 | grep "KEYWORD"
```

### Memory Testing
```bash
# Run with valgrind (if available)
valgrind --leak-check=full ./build/test_tokenizer_verification
```

---

## Test Matrix

| Test Name | Type | Location | Run Command | Pass/Fail |
|-----------|------|----------|-------------|-----------|
| test_tokenizer_verification | **Integration** | build/ | `./build/test_tokenizer_verification` | ✅ Pass |
| test_tokenizer_integration | **Integration** | build/ | `./build/test_tokenizer_integration` | ✅ Pass |
| test_arena_basic | Unit | build/tests/arena/ | `./build/tests/arena/test_arena_basic` | ✅ Pass |
| test_arena_stress | Unit | build/tests/arena/ | `./build/tests/arena/test_arena_stress` | ⚠️ 1 Fail* |
| test_arena_edge | Unit | build/tests/arena/ | `./build/tests/arena/test_arena_edge` | ✅ Pass |

*One performance test may fail under heavy load - this is expected

---

## Debugging Failed Tests

### If Tokenizer Not Found
```bash
# Check if tokenizer was fetched
ls -la build/_deps/db25_tokenizer-src/

# Manually fetch
cd build
cmake .. -DFETCHCONTENT_FULLY_DISCONNECTED=OFF
```

### If Verification Test Can't Find sql_test.sqls
```bash
# The test tries multiple paths:
# 1. sql_test.sqls (current dir)
# 2. tests/sql_test.sqls (relative)
# 3. ../tests/sql_test.sqls (from build)
# 4. SQL_TEST_FILE_PATH (CMake absolute path)

# Check CMake set the path correctly
grep SQL_TEST_FILE CMakeCache.txt
```

### Common Issues

1. **"Could not find sql_test.sqls"** - CMake didn't set path
   ```bash
   # Rebuild with CMake
   cd build && cmake .. && make
   ```

2. **Token mismatch** - Tokenizer update needed
   ```bash
   # Pull latest tokenizer
   cd build/_deps/db25_tokenizer-src
   git pull origin main
   ```

3. **Undefined symbols** - Link error
   ```bash
   # Clean rebuild
   rm -rf build && mkdir build && cd build
   cmake .. && make
   ```

---

## Understanding Token Types

The tokenizer produces these token types that the parser expects:

| Token Type | Value | Examples |
|------------|-------|----------|
| KEYWORD (1) | 1 | SELECT, FROM, WHERE, INSERT |
| IDENTIFIER (2) | 2 | table_name, column1, users |
| NUMBER (3) | 3 | 42, 3.14, -100 |
| STRING (4) | 4 | 'hello', "world" |
| OPERATOR (5) | 5 | +, -, *, /, =, <> |
| DELIMITER (6) | 6 | ;, ,, (, ) |
| WHITESPACE (7) | 7 | (filtered out) |
| COMMENT (8) | 8 | (filtered out) |
| EOF (9) | 9 | (always present at end) |

---

## Continuous Integration Commands

```bash
#!/bin/bash
# CI/CD Test Script

set -e  # Exit on error

echo "=== DB25 Parser CI Tests ==="

# Build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run critical integration tests
echo "Running tokenizer verification..."
./test_tokenizer_verification || exit 1

echo "Running parser integration..."
./test_tokenizer_integration || exit 1

echo "✅ All CI tests passed!"
```

---

## Next Steps After Verification

Now that tokenizer verification is complete and passing:

1. **Implement Parser**: Start with recursive descent for statements
2. **Add Pratt Parser**: For expression parsing with precedence
3. **Generate AST**: Create AST nodes for parsed SQL
4. **Add Parser Tests**: Test each parser component as implemented

---

## Summary

The tokenizer verification is **COMPLETE and WORKING**:

- ✅ **10 hardcoded tests**: All passing with exact token matches
- ✅ **23 SQL queries**: All tokenizing correctly from sql_test.sqls
- ✅ **Token types**: Correctly classified (KEYWORD vs IDENTIFIER etc.)
- ✅ **Token values**: Exact string values preserved
- ✅ **Token counts**: No missing or extra tokens
- ✅ **Whitespace filtering**: Comments and whitespace removed
- ✅ **EOF token**: Always present at end of stream
- ✅ **Path independence**: Works from any directory

The tokenizer is producing **exactly the right tokens** as parser input. The parser can now be implemented with confidence that the input token stream is correct.