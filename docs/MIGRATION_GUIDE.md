# Migration Guide: Freezing Parser for Semantic Layer

## Step 1: Prepare for Freeze (Day 1)

### 1.1 Version Tagging
```bash
git tag -a v1.0.0-syntactic -m "Syntactic Parser v1.0.0 - Feature Complete"
git push origin v1.0.0-syntactic
```

### 1.2 Create Release Branch
```bash
git checkout -b release/v1.0-syntactic
git push origin release/v1.0-syntactic
```

### 1.3 Documentation Snapshot
- Copy ARCHITECTURAL_ASSESSMENT.md to FROZEN_STATE.md
- Document all known limitations
- List the 3% unsupported features

## Step 2: Create Semantic Layer Repository (Day 1-2)

### 2.1 New Repository Setup
```bash
# Create new repository
mkdir DB25-semantic-analyzer
cd DB25-semantic-analyzer
git init

# Add parser as submodule
git submodule add -b release/v1.0-syntactic \
    https://github.com/yourorg/DB25-parser2.git \
    external/parser
git submodule update --init --recursive
```

### 2.2 Directory Structure
```bash
DB25-semantic-analyzer/
├── external/
│   └── parser/           # Submodule (read-only)
├── include/
│   └── db25/
│       └── semantic/
│           ├── analyzer.hpp
│           ├── schema_provider.hpp
│           ├── type_system.hpp
│           ├── scope_resolver.hpp
│           └── validator.hpp
├── src/
│   ├── analyzer.cpp
│   ├── type_checker.cpp
│   ├── scope_resolver.cpp
│   └── validator.cpp
├── tests/
├── docs/
└── CMakeLists.txt
```

### 2.3 CMake Configuration
```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(DB25SemanticAnalyzer VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add parser as external project (read-only)
add_subdirectory(external/parser EXCLUDE_FROM_ALL)

# Semantic analyzer library
add_library(db25semantic
    src/analyzer.cpp
    src/type_checker.cpp
    src/scope_resolver.cpp
    src/validator.cpp
)

target_include_directories(db25semantic PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/parser/include
)

target_link_libraries(db25semantic
    PRIVATE db25parser  # Link to frozen parser
)

# Tests
enable_testing()
# ... test configuration
```

## Step 3: Define Semantic Interfaces (Day 2-3)

### 3.1 Core Analyzer Interface
```cpp
// include/db25/semantic/analyzer.hpp
#pragma once
#include "db25/ast/ast_node.hpp"
#include <expected>

namespace db25::semantic {

class SemanticAnalyzer {
public:
    struct Options {
        bool validate_schema = true;
        bool check_types = true;
        bool resolve_aliases = true;
    };
    
    // Main analysis entry point
    std::expected<SemanticAST*, SemanticError> 
    analyze(const ast::ASTNode* syntax_tree, Options opts = {});
    
    // Plugin interfaces
    void set_schema_provider(std::unique_ptr<SchemaProvider> provider);
    void set_type_system(std::unique_ptr<TypeSystem> types);
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace
```

### 3.2 Schema Provider Interface
```cpp
// include/db25/semantic/schema_provider.hpp
#pragma once
#include <optional>
#include <vector>
#include <string_view>

namespace db25::semantic {

struct ColumnInfo {
    std::string name;
    SQLType type;
    bool nullable;
    bool is_primary_key;
};

struct TableInfo {
    std::string name;
    std::string schema;
    std::vector<ColumnInfo> columns;
    std::vector<std::string> primary_key;
};

class SchemaProvider {
public:
    virtual ~SchemaProvider() = default;
    
    virtual std::optional<TableInfo> 
    get_table(std::string_view schema, std::string_view name) = 0;
    
    virtual std::vector<std::string> 
    list_schemas() = 0;
    
    virtual bool 
    table_exists(std::string_view schema, std::string_view name) = 0;
};

// Mock implementation for testing
class MockSchemaProvider : public SchemaProvider {
public:
    void add_table(TableInfo table);
    // ... implementation
};

} // namespace
```

## Step 4: Implement Visitor Pattern (Day 3-4)

### 4.1 AST Visitor Base
```cpp
// include/db25/semantic/ast_visitor.hpp
#pragma once
#include "db25/ast/ast_node.hpp"

namespace db25::semantic {

class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;
    
    // Visit methods for each node type
    virtual void visit_select_stmt(const ast::ASTNode* node) {}
    virtual void visit_table_ref(const ast::ASTNode* node) {}
    virtual void visit_column_ref(const ast::ASTNode* node) {}
    virtual void visit_join_clause(const ast::ASTNode* node) {}
    // ... more visit methods
    
    // Traverse helpers
    void traverse(const ast::ASTNode* node);
    
protected:
    void visit_children(const ast::ASTNode* node);
};

} // namespace
```

### 4.2 Semantic Validator Implementation
```cpp
// src/validator.cpp
#include "db25/semantic/validator.hpp"

namespace db25::semantic {

class SemanticValidator : public ASTVisitor {
private:
    SchemaProvider* schema_;
    TypeSystem* types_;
    std::vector<SemanticError> errors_;
    
public:
    void visit_table_ref(const ast::ASTNode* node) override {
        auto table_name = node->primary_text;
        if (!schema_->table_exists("", table_name)) {
            errors_.push_back({
                .type = SemanticError::UNKNOWN_TABLE,
                .message = fmt::format("Table '{}' does not exist", table_name),
                .location = node->source_location
            });
        }
    }
    
    void visit_column_ref(const ast::ASTNode* node) override {
        // Validate column exists in context
        // Check type compatibility
        // etc.
    }
};

} // namespace
```

## Step 5: Testing Strategy (Day 4-5)

### 5.1 Semantic Test Suite
```cpp
// tests/test_semantic_validation.cpp
#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/semantic/analyzer.hpp"

class SemanticTest : public ::testing::Test {
protected:
    db25::parser::Parser parser;
    db25::semantic::SemanticAnalyzer analyzer;
    db25::semantic::MockSchemaProvider* schema;
    
    void SetUp() override {
        auto provider = std::make_unique<MockSchemaProvider>();
        schema = provider.get();
        
        // Setup test schema
        schema->add_table({
            .name = "users",
            .columns = {
                {.name = "id", .type = SQLType::INTEGER},
                {.name = "name", .type = SQLType::VARCHAR},
                {.name = "email", .type = SQLType::VARCHAR}
            }
        });
        
        analyzer.set_schema_provider(std::move(provider));
    }
};

TEST_F(SemanticTest, ValidatesTableExists) {
    auto ast = parser.parse("SELECT * FROM users");
    ASSERT_TRUE(ast.has_value());
    
    auto result = analyzer.analyze(ast.value());
    EXPECT_TRUE(result.has_value());
}

TEST_F(SemanticTest, DetectsUnknownTable) {
    auto ast = parser.parse("SELECT * FROM unknown_table");
    ASSERT_TRUE(ast.has_value());  // Parser accepts it
    
    auto result = analyzer.analyze(ast.value());
    EXPECT_FALSE(result.has_value());  // Semantic layer rejects it
    EXPECT_EQ(result.error().type, SemanticError::UNKNOWN_TABLE);
}
```

## Step 6: Integration Points (Day 5)

### 6.1 Parser-Semantic Bridge
```cpp
// include/db25/integration/sql_processor.hpp
class SQLProcessor {
private:
    parser::Parser parser_;
    semantic::SemanticAnalyzer analyzer_;
    
public:
    std::expected<ValidatedQuery, Error> 
    process(std::string_view sql) {
        // Step 1: Parse
        auto ast = parser_.parse(sql);
        if (!ast) return unexpected(ast.error());
        
        // Step 2: Semantic Analysis
        auto semantic_tree = analyzer_.analyze(ast.value());
        if (!semantic_tree) return unexpected(semantic_tree.error());
        
        // Step 3: Return validated query
        return ValidatedQuery{
            .ast = ast.value(),
            .semantic = semantic_tree.value()
        };
    }
};
```

## Step 7: Performance Monitoring

### 7.1 Benchmark Separation
```cpp
// benchmarks/bench_layers.cpp
static void BM_Parser(benchmark::State& state) {
    Parser parser;
    for (auto _ : state) {
        auto result = parser.parse(complex_query);
        benchmark::DoNotOptimize(result);
    }
}

static void BM_Semantic(benchmark::State& state) {
    Parser parser;
    SemanticAnalyzer analyzer;
    auto ast = parser.parse(complex_query).value();
    
    for (auto _ : state) {
        auto result = analyzer.analyze(ast);
        benchmark::DoNotOptimize(result);
    }
}

BENCHMARK(BM_Parser);
BENCHMARK(BM_Semantic);
```

## Migration Checklist

### Week 1: Preparation
- [ ] Tag v1.0.0-syntactic release
- [ ] Create release branch
- [ ] Document all limitations
- [ ] Set up new semantic repository
- [ ] Configure submodule linkage

### Week 2: Foundation
- [ ] Define semantic interfaces
- [ ] Implement visitor pattern
- [ ] Create mock schema provider
- [ ] Set up semantic test framework
- [ ] Write initial validation tests

### Week 3: Core Features
- [ ] Implement table validation
- [ ] Add column resolution
- [ ] Build type system basics
- [ ] Create scope resolver
- [ ] Add alias tracking

### Week 4: Integration
- [ ] Build integration layer
- [ ] Add performance benchmarks
- [ ] Create end-to-end tests
- [ ] Documentation
- [ ] CI/CD setup

## Success Metrics

### Parser (Frozen)
- Zero changes after freeze
- All tests continue passing
- Performance unchanged
- API stability 100%

### Semantic Layer (New)
- Schema validation working
- Type checking operational
- 80% of semantic errors caught
- <2x parser execution time
- Clean separation maintained

## Rollback Plan

If separation causes issues:
1. Semantic layer can include parser source directly (not recommended)
2. Cherry-pick critical fixes to release branch
3. Plan v1.1 parser release with minimal changes
4. Maximum 1 patch per quarter

## Conclusion

This migration guide provides a practical path to:
1. **Freeze the parser** at its current excellent state (97% coverage)
2. **Create semantic layer** with clean separation
3. **Maintain quality** through comprehensive testing
4. **Enable parallel development** on both layers
5. **Preserve performance** through careful design

The parser is ready. Let's freeze it and build the future on this solid foundation.