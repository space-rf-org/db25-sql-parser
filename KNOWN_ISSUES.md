# Known Issues

## ~~Recursive CTE with Qualified Column Names~~ (FIXED)

**Issue**: ~~Recursive CTEs fail to parse when the recursive part contains qualified column names (e.g., `h.level`).~~

**Status**: FIXED - The issue was in the parser's `parse_column_ref()` function, not the tokenizer.

**Root Cause**: The parser's `parse_column_ref()` function only accepted identifiers after dots in qualified names, but SQL keywords (like 'level') can also be used as column names. When parsing `h.level`, the parser would:
1. Parse 'h' as identifier ✓
2. Consume the '.' ✓
3. Fail to parse 'level' (a keyword) as part of the qualified name ✗
4. Leave the parser in an invalid state with the dot consumed but 'level' not consumed

**Fix**: Modified `parse_column_ref()` to accept both identifiers and keywords after dots in qualified column references.

**Test Status**: All 6 CTE tests now pass, including `CTETest.RecursiveCTE`.