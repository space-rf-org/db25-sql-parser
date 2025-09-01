# DB25 Parser Scripts

Utility scripts for testing and analysis.

## Scripts

### test_all_queries.sh
Runs the AST viewer on all queries in a .sqls file and saves output.

```bash
# Usage:
./test_all_queries.sh [sqls_file] [output_file] [options]

# Examples:
./test_all_queries.sh                                              # Use defaults
./test_all_queries.sh ../tests/showcase_queries.sqls output.txt    # Specific files
./test_all_queries.sh input.sqls output.txt --no-color --stats     # With options
```

Features:
- Iterates through all queries in a .sqls file
- Saves all AST outputs to a single file
- Shows progress on console
- Provides summary of successes/failures
- Identifies problematic queries for debugging

## Other Useful Commands

```bash
# Find all .sqls test files
find .. -name "*.sqls" -type f

# Count queries in a file
grep -v '^--' ../tests/showcase_queries.sqls | grep -v '^[[:space:]]*$' | wc -l

# Test a specific query
echo "SELECT * FROM users" | ../bin/ast_viewer --stats

# Debug a failing query
../bin/ast_viewer --file ../tests/showcase_queries.sqls --index 19 --stats
```