# Architectural Assessment: Recursive CTE Fix

## Problem Analysis

### The Failing Query
```sql
WITH RECURSIVE employee_hierarchy AS (
  SELECT id, name, manager_id, 1 as level FROM employees WHERE manager_id IS NULL 
  UNION ALL 
  SELECT e.id, e.name, e.manager_id, h.level + 1 
  FROM employees e JOIN employee_hierarchy h ON e.manager_id = h.id
)
```

### Root Cause
The parser's CTE logic (lines 251-320) has a flawed ambiguity resolution:

```cpp
// Line 254-259: Current problematic logic
if (current_token_ && current_token_->value == "(") {
    advance();  // Consumes '('
    already_in_paren = true;
    
    if (current_token_->type == TokenType::Identifier) {
        // Assumes it's a column list
    }
    // But if it's SELECT, we've already consumed '(' incorrectly!
}
```

The issue: We consume `(` before checking what follows, causing state confusion.

## Risk Assessment

### 🟢 Low Risk Areas
1. **Isolated Code Path**: CTE parsing is separate from main SELECT parsing
2. **Flag-Based**: RECURSIVE flag (0x100) doesn't affect other parsing
3. **New Feature**: CTEs are relatively new, less legacy dependency

### 🟡 Medium Risk Areas
1. **Parenthesis Tracking**: Modifying parenthesis handling could affect:
   - Subqueries in FROM
   - Grouped expressions
   - Function calls
   
2. **State Management**: The `already_in_paren` flag affects downstream parsing

### 🔴 High Risk Areas
1. **Token Stream Position**: Advancing/not advancing at wrong time breaks everything
2. **Existing CTE Tests**: 5 tests currently pass - must not break them

## Implementation Strategy

### Approach 1: Minimal Fix (Low Risk) ✅
```cpp
// Don't consume '(' immediately - peek ahead first
if (current_token_ && current_token_->value == "(") {
    // Save position
    auto* saved_token = current_token_;
    tokenizer::Tokenizer* saved_state = tokenizer_;
    
    // Peek ahead without consuming
    advance(); // temporary advance
    bool is_select = (current_token_ && 
                     current_token_->type == TokenType::Keyword &&
                     (current_token_->value == "SELECT"));
    
    // Restore position if needed
    if (!is_select) {
        // It's a column list, proceed normally
        // Parse column list...
    } else {
        // It's SELECT, don't consume the paren yet
        // Let parse_select_stmt handle it
    }
}
```

**Risk**: Very low - uses lookahead pattern
**Impact**: Only affects CTE parsing

### Approach 2: Refactor Token Management (Medium Risk) ⚠️
Add proper lookahead capability:
```cpp
class Parser {
    // Add lookahead method
    const Token* peek_next(int n = 1) {
        // Look ahead n tokens without consuming
    }
};
```

**Risk**: Medium - requires tokenizer API changes
**Impact**: Benefits entire parser but needs careful testing

### Approach 3: State Machine (High Risk) ⚠️
Implement CTE parsing as explicit state machine:
```cpp
enum class CTEParseState {
    EXPECT_NAME,
    EXPECT_COLUMN_LIST_OR_AS,
    EXPECT_AS,
    EXPECT_QUERY,
    DONE
};
```

**Risk**: High - complete rewrite of CTE parsing
**Impact**: More maintainable but high breakage risk

## Test Impact Analysis

### Tests That Could Break
1. **CTEWithColumns** - Most at risk (uses column list syntax)
2. **SimpleCTE** - Low risk (no column list)
3. **MultipleCTEs** - Medium risk (multiple parsing iterations)
4. **NestedCTEReference** - Low risk
5. **CTEWithSubquery** - Low risk

### Regression Test Strategy
```bash
# Before change
./build/test_cte > before.txt

# After change  
./build/test_cte > after.txt

# Verify no regression
diff before.txt after.txt
```

## Recommendation

### Use Approach 1 (Minimal Fix) because:

1. **Lowest Risk**: Touches minimum code
2. **Surgical**: Only fixes the specific issue
3. **Preserves Working Tests**: 5/6 tests continue to pass
4. **Easy Rollback**: Can revert if issues arise
5. **No API Changes**: No tokenizer modifications needed

### Implementation Plan

1. **Create Safety Net**
   ```bash
   git branch fix-recursive-cte
   cp src/parser/parser.cpp src/parser/parser.cpp.backup
   ```

2. **Add Diagnostic Output**
   ```cpp
   #ifdef DEBUG_CTE
   std::cerr << "CTE: Token = " << current_token_->value << "\n";
   #endif
   ```

3. **Implement Fix**
   - Modify lines 254-275 with lookahead logic
   - Don't consume `(` until we know what follows

4. **Test Incrementally**
   ```bash
   make && ./test_cte  # After each small change
   ```

5. **Verify No Regression**
   ```bash
   ctest  # Run all tests
   ```

## Risk Mitigation

### Safeguards
1. **Add CTE-specific flag**: `bool parsing_cte = true;`
2. **Preserve parenthesis_depth_**: Ensure balance maintained
3. **Add assertions**: `assert(parenthesis_depth_ >= 0);`
4. **Comprehensive logging**: Track token consumption

### Rollback Plan
If issues arise:
1. Revert parser.cpp changes
2. Disable RECURSIVE support temporarily
3. Document as known limitation

## Estimated Effort

- **Development**: 1-2 hours
- **Testing**: 1 hour  
- **Total**: 2-3 hours

## Success Criteria

1. ✅ RecursiveCTE test passes
2. ✅ All 5 existing CTE tests still pass
3. ✅ No regression in other test suites
4. ✅ Parenthesis tracking remains correct
5. ✅ Performance unchanged

## Conclusion

The fix is **architecturally safe** with proper approach. The risk is manageable because:
- CTE parsing is well-isolated
- We have good test coverage
- The fix is localized to specific ambiguity point
- No changes to core parsing logic needed

**Recommendation**: Proceed with Approach 1 (Minimal Fix) using careful incremental testing.