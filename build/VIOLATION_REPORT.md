# Parser Code Violation Report

## 1. ❌ STRING COMPARISON VIOLATIONS (Should use Keyword IDs)

### Critical Issues in parser.cpp:
- Line 313: `current_token_->value == "TRUNCATE"` - Should use keyword ID
- Line 332: `current_token_->value == "VACUUM"` - Should use keyword ID  
- Line 334: `current_token_->value == "ANALYZE"` - Should use keyword ID
- Line 340: `current_token_->value == "REINDEX"` - Should use keyword ID
- Line 342: `current_token_->value == "PRAGMA"` - Should use keyword ID
- Line 1596: `current_token_->value == "GROUPING"` - Should check keyword ID
- Line 1598: `current_token_->value == "SETS"` - Should check keyword ID
- Line 1699: `current_token_->value == "CUBE"` - Should check keyword ID
- Line 1748: `current_token_->value == "ROLLUP"` - Should check keyword ID
- Line 2499, 2573: `current_token_->value == "INTERVAL"` - Should check keyword ID

### Critical Issues in parser_statements.cpp:
- Line 28, 48, 75: `current_token_->value == "WORK"` - Should use keyword ID
- Line 56: `current_token_->value == "CHAIN"` - Should use keyword ID
- Line 85, 123: `current_token_->value == "SAVEPOINT"` - Should use keyword ID
- Line 152: `current_token_->value == "QUERY"` - Should use keyword ID
- Line 154: `current_token_->value == "PLAN"` - Should use keyword ID
- Line 158: `current_token_->value == "ANALYZE"` - Should use keyword ID
- Line 306-307: `current_token_->value == "SESSION"/"LOCAL"` - Should use keyword ID
- Line 663: `current_token_->value == "NOTHING"` - Should use keyword ID

### Critical Issues in parser_ddl.cpp:
- Line 26: `current_token_->value == "INT"` - Should use keyword ID
- Line 35: `current_token_->value == "VARCHAR"` - Should use keyword ID
- Line 38: `current_token_->value == "CHAR"` - Should use keyword ID
- Line 41: `current_token_->value == "TEXT"` - Mixed with keyword_id check
- Line 44: `current_token_->value == "REAL"` - Should use keyword ID
- Line 50: `current_token_->value == "BOOLEAN"/"BOOL"` - Should use keyword ID
- Line 62: `current_token_->value == "DECIMAL"/"NUMERIC"` - Should use keyword ID
- Line 65: `current_token_->value == "INTERVAL"` - Should use keyword ID
- Line 68: `current_token_->value == "JSON"/"JSONB"` - Should use keyword ID
- Line 651: `current_token_->value == "RENAME"` - Should use keyword ID
- Line 821: `current_token_->value == "TEMPORARY"/"TEMP"` - Should use keyword ID
- Line 866: `current_token_->value == "INSTEAD"` - Should use keyword ID
- Line 909: `current_token_->value == "ROW"` - Should use keyword ID
- Line 912: `current_token_->value == "STATEMENT"` - Should use keyword ID
- Line 1019: `current_token_->value == "AUTHORIZATION"` - Should use keyword ID

## 2. ⚠️ UNIMPLEMENTED CODE SECTIONS

### parser.cpp:
- Line 1534: Comment says "USING clause (not implemented yet)" but USING IS implemented
- Line 3793: `return true; // TODO: Add specific validation` - Missing semantic validation

### parser_statements.cpp:
- Line 33: `// TODO: Parse transaction modes (ISOLATION LEVEL, READ WRITE/ONLY, etc.)`

### parser_ddl.cpp:
- Line 520: `// TODO: Parse specific table options`
- Line 887: `// TODO: Implement column list parsing for UPDATE OF`

## 3. 🔄 CODE DUPLICATION

### Pattern: IF NOT EXISTS checking (duplicated 3 times)
```cpp
if (current_token_ && current_token_->keyword_id == db25::Keyword::IF) {
    advance();
    if (current_token_ && current_token_->keyword_id == db25::Keyword::NOT) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::EXISTS) {
            // Set flag
        }
    }
}
```
Locations: parser.cpp lines 993-999, 1056-1062, 1177-1180

### Pattern: CASCADE/RESTRICT checking (duplicated multiple times)
```cpp
if (current_token_ && current_token_->keyword_id == db25::Keyword::CASCADE) {
    // set CASCADE flag
} else if (current_token_ && current_token_->keyword_id == db25::Keyword::RESTRICT) {
    // set RESTRICT flag  
}
```

### Pattern: Optional COLUMN keyword (duplicated 4 times)
```cpp
if (current_token_ && current_token_->keyword_id == db25::Keyword::COLUMN) {
    advance(); // optional COLUMN keyword
}
```
Locations: parser.cpp lines 1233, 1241, 1251, 1258

## 4. ⚠️ CONST CORRECTNESS ISSUES

### Missing const on getter methods:
- `get_context_hint()` should be `const`
- `get_last_error()` should be `const` 
- Token accessor methods should return `const` references where appropriate

### Parameters that should be const:
- String parameters in helper functions should be `const std::string&`
- AST node parameters in validation functions should be `const ast::ASTNode*`

## 5. 🔧 RECOMMENDED FIXES

### Priority 1: Fix String Comparisons
Replace all string comparisons with keyword ID checks. Example:
```cpp
// BAD:
if (current_token_->value == "VACUUM" || current_token_->value == "vacuum")

// GOOD:
if (current_token_ && current_token_->keyword_id == db25::Keyword::VACUUM)
```

### Priority 2: Add Missing Keywords
These tokens are being compared as strings but should be keywords:
- TRUNCATE, VACUUM, ANALYZE, REINDEX, PRAGMA
- GROUPING, SETS, CUBE, ROLLUP
- WORK, CHAIN, SAVEPOINT, QUERY, PLAN
- INT, VARCHAR, CHAR, REAL, BOOLEAN, DECIMAL, NUMERIC, JSON, JSONB
- TEMPORARY, TEMP, INSTEAD, ROW, STATEMENT, AUTHORIZATION

### Priority 3: Extract Duplicate Code
Create helper functions:
```cpp
bool Parser::parse_if_not_exists() {
    if (current_token_ && current_token_->keyword_id == db25::Keyword::IF) {
        advance();
        if (current_token_ && current_token_->keyword_id == db25::Keyword::NOT) {
            advance();
            if (current_token_ && current_token_->keyword_id == db25::Keyword::EXISTS) {
                advance();
                return true;
            }
        }
    }
    return false;
}

bool Parser::parse_cascade_restrict(uint32_t& flags) {
    if (current_token_ && current_token_->keyword_id == db25::Keyword::CASCADE) {
        flags |= 0x04;
        advance();
        return true;
    } else if (current_token_ && current_token_->keyword_id == db25::Keyword::RESTRICT) {
        flags |= 0x08;
        advance();
        return true;
    }
    return false;
}
```

### Priority 4: Complete TODOs
1. Implement transaction isolation levels in parser_statements.cpp
2. Implement UPDATE OF column list in triggers
3. Add semantic validation framework

### Priority 5: Const Correctness
Mark all non-modifying methods as const in parser.hpp

## Summary Statistics
- **String comparison violations**: 40+ instances
- **Unimplemented TODOs**: 5 instances  
- **Code duplication patterns**: 4 major patterns
- **Missing keywords**: 25+ tokens
- **Const correctness issues**: Multiple getter methods

## Severity Assessment
- 🔴 **CRITICAL**: String comparisons bypass keyword system
- 🟡 **MEDIUM**: Code duplication increases maintenance burden
- 🟢 **LOW**: TODOs for advanced features not affecting core functionality