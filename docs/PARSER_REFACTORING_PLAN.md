# Parser Refactoring Plan

## Current State
- **File**: `src/parser/parser.cpp`
- **Size**: 4,368 lines
- **Methods**: 36 parser methods
- **Problem**: Monolithic file makes bug fixes difficult and compilation slow

## Refactoring Strategy

### Phase 1: Create Infrastructure (No Breaking Changes)

#### 1.1 Create Internal Header
```cpp
// src/parser/parser_internal.hpp
#pragma once
#include "db25/parser/parser.hpp"

namespace db25::parser::internal {
    // Shared utilities for all parser modules
    
    // Forward declarations for friend access
    class DDLParser;
    class DMLParser;
    class SelectParser;
    class ExpressionParser;
}
```

#### 1.2 Update Parser Class
Add friend declarations to `include/db25/parser/parser.hpp`:
```cpp
class Parser {
    // ... existing members ...
private:
    friend class internal::DDLParser;
    friend class internal::DMLParser;
    friend class internal::SelectParser;
    friend class internal::ExpressionParser;
};
```

### Phase 2: Extract DDL Statements (391 lines)
**File**: `src/parser/parser_ddl.cpp`

Methods to move:
- `parse_create_stmt()` - Line 1419
- `parse_create_table_stmt()` - Line 1482
- `parse_create_table_impl()` - Line 1488
- `parse_create_index_stmt()` - Line 1530
- `parse_create_index_impl()` - Line 1536
- `parse_create_view_stmt()` - Line 1603
- `parse_create_view_impl()` - Line 1609
- `parse_drop_stmt()` - Line 1658
- `parse_alter_table_stmt()` - Line 1708
- `parse_truncate_stmt()` - Line 1780

**Benefits**: DDL is self-contained, rarely changes, easy to test separately

### Phase 3: Extract DML Statements (564 lines)
**File**: `src/parser/parser_dml.cpp`

Methods to move:
- `parse_insert_stmt()` - Line 854
- `parse_update_stmt()` - Line 1124
- `parse_delete_stmt()` - Line 1330

**Benefits**: Clear boundaries, can fix INSERT column list bug here

### Phase 4: Extract Expression Parser (1,372 lines) 
**File**: `src/parser/parser_expressions.cpp`

Methods to move:
- `parse_expression()` - Line 3526
- `parse_primary_expression()` - Line 3162
- `parse_primary()` - Line 4174
- `parse_case_expression()` - Line 3872
- `parse_cast_expression()` - Line 3981
- `parse_extract_expression()` - Line 4071
- `parse_window_spec()` - Line 2808
- Precedence table (lines 3444-3523)

**Benefits**: 
- Can fix `table.*` bug here with new `parse_qualified_wildcard()` method
- Expression parsing is complex and benefits from isolation
- Easier to add new operators and expressions

### Phase 5: Extract SELECT Clauses (979 lines)
**File**: `src/parser/parser_select.cpp`

Methods to move:
- `parse_select_stmt()` - Line 632
- `parse_select_list()` - Line 1814
- `parse_select_item()` - Line 3092
- `parse_from_clause()` - Line 1902
- `parse_join_clause()` - Line 1973
- `parse_where_clause()` - Line 2068
- `parse_group_by_clause()` - Line 2092
- `parse_having_clause()` - Line 2370
- `parse_order_by_clause()` - Line 2394
- `parse_limit_clause()` - Line 2503

**Benefits**: SELECT is the most complex statement, benefits from dedicated file

### Phase 6: Extract Utilities (446 lines)
**File**: `src/parser/parser_utils.cpp`

Methods to move:
- `parse_identifier()` - Line 2553
- `parse_table_reference()` - Line 2575
- `parse_column_ref()` - Line 2624
- `parse_function_call()` - Line 2676
- Error handling (lines 4181-4225)
- Validation methods (lines 4226-4368)

**Benefits**: Shared utilities used by all parsers

### Phase 7: Core Parser Remains (817 lines)
**File**: `src/parser/parser.cpp` (reduced)

Keeps:
- Constructor/destructor
- `parse()` and `parse_script()` main interface
- `parse_statement()` dispatcher - Line 262
- `parse_with_statement()` - Line 349
- Token stream management (lines 231-259)
- Memory management (lines 209-230)
- Helper methods (lines 35-60)

## Implementation Plan

### Step 1: Create Structure (Day 1)
```bash
# Create new files
touch src/parser/parser_internal.hpp
touch src/parser/parser_ddl.cpp
touch src/parser/parser_dml.cpp
touch src/parser/parser_expressions.cpp
touch src/parser/parser_select.cpp
touch src/parser/parser_utils.cpp
```

### Step 2: Extract DDL First (Day 1)
1. Copy DDL methods to `parser_ddl.cpp`
2. Update CMakeLists.txt to include new file
3. Build and test
4. Remove DDL methods from original

### Step 3: Continue with DML, Expressions, etc. (Days 2-3)
- One module at a time
- Test after each extraction
- Keep original as backup

### Step 4: Fix Bugs in Isolated Modules (Day 4)
- Fix `table.*` bug in `parser_expressions.cpp`
- Fix INSERT column list in `parser_dml.cpp`

## File Size Targets

| File | Current | Target | Description |
|------|---------|--------|-------------|
| parser.cpp | 4,368 | ~800 | Core dispatcher only |
| parser_ddl.cpp | 0 | ~400 | DDL statements |
| parser_dml.cpp | 0 | ~600 | DML statements |
| parser_expressions.cpp | 0 | ~1,400 | Expression parser |
| parser_select.cpp | 0 | ~1,000 | SELECT clauses |
| parser_utils.cpp | 0 | ~500 | Shared utilities |

## Testing Strategy

1. **Before Refactoring**: Run full test suite, save output
2. **After Each Phase**: Run tests, compare output
3. **Regression Test**: Use `test_all_queries.sh` script
4. **Bug Fix Tests**: Add specific tests for `table.*` and INSERT issues

## CMakeLists.txt Update

```cmake
set(PARSER_SOURCES
    src/parser/parser.cpp
    src/parser/parser_ddl.cpp
    src/parser/parser_dml.cpp
    src/parser/parser_expressions.cpp
    src/parser/parser_select.cpp
    src/parser/parser_utils.cpp
)

add_library(db25parser STATIC
    ${PARSER_SOURCES}
    # ... other sources ...
)
```

## Success Criteria

- [ ] All tests pass after refactoring
- [ ] Compilation time reduced by >30%
- [ ] `table.*` bug fixed
- [ ] INSERT column list bug fixed
- [ ] Each file < 1,500 lines
- [ ] Clear module boundaries
- [ ] No performance regression

## Rollback Plan

1. Keep backup: `cp parser.cpp parser.cpp.backup`
2. Git commit before each phase
3. Can revert individual modules if needed
4. Original parser.cpp preserved until fully validated