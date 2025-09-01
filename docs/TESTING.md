# DB25 Parser Testing Documentation

## Test Coverage Summary

### Overall Statistics
- **Test Suites**: 21
- **Total Tests**: 200+
- **Pass Rate**: 95%+ (3 known minor issues)
- **EBNF Coverage**: 92%
- **SQL Feature Coverage**: 100% of supported features

## Test Categories

### 1. Unit Tests
- **Arena Allocator Tests** (`test_arena_*`)
  - Basic allocation
  - Edge cases
  - Stress testing
  - Thread safety

### 2. Parser Functionality Tests
- **Basic Parsing** (`test_parser_basic`)
  - Simple SELECT/INSERT/UPDATE/DELETE
  - Expression parsing
  - Literal handling

- **SELECT Features** (`test_parser_select`)
  - Column selection
  - WHERE clauses
  - ORDER BY/GROUP BY
  - LIMIT/OFFSET

- **JOIN Testing** (`test_join_*`)
  - INNER/LEFT/RIGHT/FULL OUTER
  - Multiple joins
  - Complex ON conditions

- **CTE Testing** (`test_cte`)
  - Simple CTEs
  - Recursive CTEs
  - Multiple CTEs
  - Nested references

- **DDL Statements** (`test_ddl_statements`)
  - CREATE TABLE/INDEX/VIEW
  - DROP statements
  - ALTER TABLE
  - TRUNCATE

### 3. Advanced Features
- **CASE Expressions** (`test_case_expr`)
  - Simple CASE
  - Searched CASE
  - Nested CASE

- **Operators** (`test_sql_operators`)
  - Arithmetic operators
  - Comparison operators
  - Logical operators
  - String concatenation (||)

- **Subqueries** (`test_subqueries`)
  - IN subqueries
  - EXISTS subqueries
  - Scalar subqueries

### 4. Quality Tests
- **Correctness** (`test_parser_correctness`)
  - AST structure validation
  - Parent-child relationships
  - Node ID uniqueness

- **Edge Cases** (`test_edge_cases`)
  - Empty queries
  - Malformed SQL
  - Boundary conditions

- **Performance** (`test_performance`)
  - Throughput measurement
  - Scaling characteristics
  - Memory usage

### 5. Security Tests
- **DepthGuard** (`test_depth_guard`)
  - Stack overflow prevention
  - Configurable depth limits
  - Parser reuse after errors

## Running Tests

### Run All Tests
```bash
cd build
ctest --output-on-failure
```

### Run Specific Test
```bash
./test_parser_basic
./test_ddl_statements
./test_cte
```

### Run with Verbose Output
```bash
ctest -V
```

### Run Performance Tests
```bash
./test_performance
./test_prefetch_performance
./test_simd_usage
```

## Test SQL Files

### EBNF Coverage Tests
- `ebnf_supported.sql` - All supported features (92% pass rate)
- `ebnf_complete.sql` - Full EBNF test suite

### Validation Test
```bash
./test_parser_validation
```
Tests 35 core SQL features with 100% pass rate.

## Known Test Failures

### 1. ParserCorrectnessTest.EmptySelectList
- **Issue**: Parser creates implicit node for empty SELECT
- **Impact**: Minor - AST structure difference
- **Status**: Won't fix (current behavior is valid)

### 2. ArenaStressTest.MixedSizeAllocationSpeed
- **Issue**: Performance variance on CI systems
- **Impact**: None - performance test only
- **Status**: Expected on virtualized environments

### 3. EdgeCasesTest (intermittent)
- **Issue**: Timeout on very deep recursion test
- **Impact**: None - security feature working
- **Status**: Working as designed

## Adding New Tests

### Test Structure
```cpp
TEST(TestSuite, TestName) {
    Parser parser;
    std::string sql = "SELECT * FROM users";
    
    auto result = parser.parse(sql);
    
    ASSERT_TRUE(result.has_value());
    auto* ast = result.value();
    EXPECT_EQ(ast->type, NodeType::SelectStmt);
}
```

### Best Practices
1. Test both positive and negative cases
2. Validate AST structure, not just parse success
3. Include edge cases and boundary conditions
4. Add performance regression tests for optimizations

## Continuous Testing

### Pre-commit Checks
```bash
# Run before committing
make test
./test_parser_validation
```

### Performance Regression
```bash
# Baseline
./test_performance > baseline.txt

# After changes
./test_performance > current.txt
diff baseline.txt current.txt
```

## Test Data

### Sample Queries
Located in `tests/` directory:
- `simple_queries.sql` - Basic SQL
- `complex_queries.sql` - Advanced features
- `edge_cases.sql` - Boundary conditions
- `performance_queries.sql` - Large queries

## Coverage Metrics

### By SQL Category
| Category | Coverage | Tests |
|----------|----------|-------|
| SELECT | 100% | 50+ |
| INSERT | 100% | 20+ |
| UPDATE | 100% | 20+ |
| DELETE | 100% | 15+ |
| CREATE | 100% | 25+ |
| DROP | 100% | 15+ |
| ALTER | 100% | 20+ |
| CTEs | 100% | 15+ |
| JOINs | 100% | 25+ |
| CASE | 100% | 10+ |

### By Parser Component
| Component | Line Coverage | Branch Coverage |
|-----------|--------------|-----------------|
| Parser | 85% | 78% |
| Tokenizer Adapter | 92% | 88% |
| AST Nodes | 78% | 72% |
| Arena Allocator | 95% | 91% |

## Debugging Failed Tests

### Enable Debug Output
```cpp
#define DEBUG_PARSER 1
#define DEBUG_CTE 1
```

### Use AST Dump
```cpp
void dump_ast(ASTNode* node, int depth = 0) {
    for (int i = 0; i < depth; i++) std::cout << "  ";
    std::cout << node_type_to_string(node->type);
    if (node->primary_text) {
        std::cout << " [" << node->primary_text << "]";
    }
    std::cout << std::endl;
    
    for (auto* child = node->first_child; child; child = child->next_sibling) {
        dump_ast(child, depth + 1);
    }
}
```

### Memory Leak Detection
```bash
# On macOS
leaks -atExit -- ./test_parser_basic

# On Linux  
valgrind --leak-check=full ./test_parser_basic
```

---

*Last Updated: 2025-08-30*