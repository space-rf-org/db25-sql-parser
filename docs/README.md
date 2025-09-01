# DB25 SQL Parser Documentation

## User Documentation

- 📖 **[User Manual](USER_MANUAL.md)** - Complete guide for using the parser
  - Installation and building
  - Using the parser in your programs
  - AST Viewer tool usage
  - Supported SQL features
  - Troubleshooting

- 🎯 **[SQL Examples](../tests/showcase_queries.sqls)** - Comprehensive SQL query examples
  - Basic DQL, DML, DDL
  - Advanced features (CTEs, Window Functions)
  - Complex analytical queries

## Developer Documentation

- 🔧 **[Developer Guide](DEVELOPER_GUIDE.md)** - For contributors and extenders
  - Architecture overview
  - Adding new features (5 examples)
  - Testing guidelines
  - Code style and best practices
  - Performance optimization tips

- 🏗️ **[Architecture Documentation](ARCHITECTURE_CONSOLIDATED.md)** - System design
  - Core components
  - SIMD tokenizer design
  - Parser architecture
  - AST node structure
  - Memory management

## Design Documents

- 📐 **[Parser Design](PARSER_DESIGN_FINAL.md)** - Detailed parser implementation
- 🎨 **[AST Design](AST_Design_Column_References.md)** - AST structure and semantics
- 💾 **[Arena Allocator](ARENA_README.md)** - Memory management design
- ⚠️ **[Exception Handling](EXCEPTION_HANDLING_PHILOSOPHY.md)** - Error handling approach

## Testing Documentation

- 🧪 **[Testing Guide](TESTING_GUIDE_COMPLETE.md)** - Comprehensive testing strategy
- 📊 **[Test Report](TEST_REPORT_FINAL.md)** - Test results and coverage
- 📝 **[Test Specification](PARSER_TEST_SPECIFICATION.md)** - Test requirements

## Academic Papers

- 📄 **[Arena Allocator Paper](arena_allocator_paper.pdf)** - Academic treatment of arena design
- 📄 **[AST Design Paper](AST_Design_Column_References.pdf)** - Formal AST specification

## Progress and Status

- ✅ **[Parser Progress](PARSER_PROGRESS.md)** - Implementation status
- 📋 **[Assessment](PARSER_ASSESSMENT.md)** - Current state evaluation
- 🚀 **[Next Steps](NEXT_STEPS.md)** - Future development plans

## Quick Links

- [Main README](../README.md)
- [CMakeLists.txt](../CMakeLists.txt)
- [Test Suites](../tests/)
- [Tools](../tools/)

## Document Organization

```
docs/
├── User Documentation
│   ├── USER_MANUAL.md          # How to use
│   └── Examples                # SQL examples
│
├── Developer Documentation
│   ├── DEVELOPER_GUIDE.md      # How to extend
│   ├── ARCHITECTURE_*.md       # System design
│   └── *_DESIGN_*.md           # Component designs
│
├── Testing
│   ├── TESTING_GUIDE*.md       # Test strategy
│   └── TEST_REPORT*.md         # Test results
│
└── Academic Papers
    ├── *.pdf                   # Published papers
    └── *.tex                   # LaTeX sources
```

## Getting Started

### For Users
Start with the [User Manual](USER_MANUAL.md) to learn how to build and use the parser.

### For Developers
Read the [Developer Guide](DEVELOPER_GUIDE.md) and [Architecture Documentation](ARCHITECTURE_CONSOLIDATED.md) to understand the system design.

### For Contributors
1. Read the [Developer Guide](DEVELOPER_GUIDE.md)
2. Review the [Testing Guide](TESTING_GUIDE_COMPLETE.md)
3. Check [Next Steps](NEXT_STEPS.md) for areas needing work
4. Follow the contribution guidelines in the main README

## Document Maintenance

All documentation should be kept up-to-date with code changes. When adding new features:

1. Update relevant design documents
2. Add examples to showcase_queries.sqls
3. Update the User Manual if user-facing
4. Add developer notes to the Developer Guide
5. Update test documentation

---

*Last Updated: August 2024*