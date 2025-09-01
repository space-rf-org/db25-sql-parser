# DB25 Parser Binaries

This directory contains compiled executables and tools.

## Tools

### ast_viewer
Interactive AST visualization tool for SQL queries.

```bash
# Usage examples:
echo "SELECT * FROM users" | ./ast_viewer
./ast_viewer --file ../tests/showcase_queries.sqls --index 1
./ast_viewer --help
```

Options:
- `--color` - Enhanced color mode with Unicode (default)
- `--simple` - Simple ASCII mode
- `--no-color` - Disable all colors
- `--stats` - Show parsing statistics
- `--file <path>` - Read from .sqls file
- `--index <n>` - Parse specific query (1-based)
- `--all` - Parse all queries in file

## Build Instructions

To rebuild the tools:
```bash
cd ../build
make ast_viewer
cp ast_viewer ../bin/
```

## Note

The binaries in this directory are copies from the build directory.
Always rebuild from source after code changes.