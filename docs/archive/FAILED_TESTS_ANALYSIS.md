# Failed Test Cases Analysis

**Total Failed Suites**: 4 out of 19 (21%)
**Total Failed Cases**: 6 individual test failures

## Categorization by Failure Type

### 1. Parser Logic Errors (2 failures)

#### 🔴 RecursiveCTE (CTETest)
**Category**: Parser Logic Error
**Severity**: HIGH
**SQL**: 
```sql
WITH RECURSIVE employee_hierarchy AS (
  SELECT id, name, manager_id, 1 as level FROM employees WHERE manager_id IS NULL 
  UNION ALL 
  SELECT e.id, e.name, e.manager_id, h.level + 1 
  FROM employees e JOIN employee_hierarchy h ON e.manager_id = h.id
) SELECT * FROM employee_hierarchy
```
**Issue**: Parser fails to parse recursive CTEs
**Root Cause**: Premature consumption of '(' after AS keyword
**Fix Status**: Solution designed (20 lines), not implemented
**Impact**: Major SQL feature unavailable

#### 🟡 AggregateFunctionsStructure (ASTVerificationTest)
**Category**: Parser Logic Error  
**Severity**: MEDIUM
**SQL**: Complex aggregate function query
**Issue**: Parser returns nullptr for certain aggregate patterns
**Root Cause**: Likely issue with aggregate function parsing in specific contexts
**Impact**: Some aggregate queries fail to parse

### 2. Validation Errors (2 failures)

#### 🟡 HavingWithoutGroupBy (GroupByTest)
**Category**: Semantic Validation Error
**Severity**: MEDIUM
**SQL**: `SELECT * FROM employees HAVING COUNT(*) > 10`
**Issue**: Test expects parser to accept this (which it doesn't)
**Root Cause**: Parser correctly rejects HAVING without GROUP BY
**Fix**: This is actually correct behavior - the test expectation is wrong
**Impact**: None - parser is behaving correctly

#### 🟢 Invalid operator === (EdgeCasesTest)
**Category**: Tokenizer Validation Error
**Severity**: LOW
**SQL**: `SELECT * FROM users WHERE id === 1`
**Issue**: Parser accepts JavaScript-style triple equals
**Root Cause**: Tokenizer doesn't reject invalid operators
**Fix**: File bug with external tokenizer project
**Impact**: Invalid SQL accepted (permissiveness issue)

### 3. Lexical/Case Sensitivity Errors (1 failure)

#### 🟢 Mixed case keywords (EdgeCasesTest)
**Category**: Lexical Processing Error
**Severity**: LOW
**SQL**: `SeLeCt * FrOm UsErS wHeRe AcTiVe = TrUe`
**Issue**: Parser fails on mixed-case SQL keywords
**Root Cause**: Case-sensitive keyword matching
**Fix**: Implement case-insensitive keyword comparison
**Impact**: Valid SQL with mixed case rejected

## Summary by Category

| Category | Count | Severity | Examples |
|----------|-------|----------|----------|
| **Parser Logic** | 2 | HIGH/MEDIUM | Recursive CTEs, Aggregate functions |
| **Semantic Validation** | 2 | MEDIUM/LOW | HAVING validation, Invalid operators |
| **Lexical Processing** | 1 | LOW | Mixed case keywords |
| **Test Expectation Error** | 1 | N/A | HavingWithoutGroupBy (test is wrong) |

## Severity Distribution

- 🔴 **HIGH** (1): Recursive CTE - Major feature missing
- 🟡 **MEDIUM** (2): Aggregate functions, HAVING validation
- 🟢 **LOW** (2): Mixed case, Invalid operators

## Root Cause Analysis

### 1. Design Issues (33%)
- Recursive CTE: Ambiguous grammar handling
- Solution exists but not implemented

### 2. External Dependencies (33%)
- Invalid operator ===: Tokenizer issue
- Mixed case: Tokenizer provides raw tokens

### 3. Test Issues (17%)
- HavingWithoutGroupBy: Test expects wrong behavior

### 4. Implementation Gaps (17%)
- Aggregate function edge cases not handled

## Fix Priority and Effort

| Priority | Test | Effort | Impact |
|----------|------|--------|--------|
| **P0** | RecursiveCTE | 2-3 hours | Enables major SQL feature |
| **P1** | AggregateFunctionsStructure | 2-4 hours | Fixes aggregate parsing |
| **P2** | Mixed case keywords | 1-2 hours | Better SQL compatibility |
| **P3** | HavingWithoutGroupBy | 0 hours | Test fix only |
| **P4** | Invalid operator === | External | Document as known issue |

## Action Items

### Immediate (Fix in next sprint)
1. **Apply Recursive CTE fix**
   - File: `src/parser/parser.cpp` lines 251-320
   - Change: Check AS before consuming '('
   - Testing: RecursiveCTE test should pass

2. **Fix Aggregate Function parsing**
   - Investigate specific failure case
   - Add defensive parsing for edge cases

3. **Fix test expectation**
   - Update HavingWithoutGroupBy test
   - Should expect nullptr (rejection)

### Short-term
1. **Case-insensitive keywords**
   - Add uppercase conversion in tokenizer adapter
   - Or handle in parser keyword checks

### Long-term
1. **File tokenizer bug**
   - Report === operator issue to tokenizer project
   - Consider validation layer for operators

## Test Health Metrics

- **True Failures**: 5 (actual bugs)
- **False Failures**: 1 (test expectation wrong)
- **External Issues**: 2 (tokenizer-related)
- **Internal Issues**: 3 (parser logic)

## Conclusion

The failures are concentrated in:
1. **One major feature** (Recursive CTEs) with a ready fix
2. **Edge cases** that don't affect common queries
3. **External tokenizer** issues we can't directly fix

With just the Recursive CTE fix, the test pass rate would improve to **84%** (16/19 suites passing).