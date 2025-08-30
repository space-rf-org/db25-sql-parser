# DB25 Parser - Interface Design Specification

**Version:** 1.0  
**Date:** March 2025  
**Author:** Chiradip Mandal  
**Organization:** Space-RF.org

## Overview

This document defines the interfaces between the DB25 Parser and its consumers (Binder, Semantic Analyzer, Query Optimizer). The design emphasizes clean separation of concerns, type safety, and performance.

---

## Parser Public API

### Core Parser Interface

```cpp
namespace db25::parser {

// Main parser class
class SQLParser {
public:
    // Configuration
    struct Config {
        enum class Dialect {
            ANSI_SQL,
            PostgreSQL,
            MySQL,
            SQLite,
            DuckDB
        };
        
        Dialect dialect = Dialect::ANSI_SQL;
        bool strict_mode = false;
        bool continue_on_error = true;
        bool enable_extensions = true;
        size_t max_errors = 100;
        size_t max_depth = 1000;
        size_t arena_initial_size = 1048576;  // 1MB
        size_t max_memory = 104857600;        // 100MB
    };
    
    // Construction
    explicit SQLParser(Config config = {});
    ~SQLParser();
    
    // Single statement parsing
    ParseResult parseStatement(std::string_view sql);
    
    // Multi-statement parsing
    std::vector<ParseResult> parseScript(std::string_view sql);
    
    // Streaming interface
    class StreamParser;
    std::unique_ptr<StreamParser> createStreamParser();
    
    // Configuration
    void setDialect(Config::Dialect dialect);
    Config::Dialect getDialect() const;
    
    // Statistics
    struct Statistics {
        size_t queries_parsed;
        size_t total_tokens;
        size_t total_nodes;
        size_t memory_used;
        double total_time_ms;
    };
    Statistics getStatistics() const;
    void resetStatistics();
};
```

### Parse Result Structure

```cpp
// Result of parsing operation
struct ParseResult {
    // Success case
    std::unique_ptr<ASTNode> ast;
    
    // Error information
    std::vector<ParseError> errors;
    std::vector<ParseWarning> warnings;
    
    // Metadata
    struct Metadata {
        size_t tokens_consumed;
        size_t nodes_created;
        size_t memory_used;
        double parse_time_ms;
        SourceRange source_range;
    } metadata;
    
    // Helper methods
    bool isSuccess() const { return ast != nullptr; }
    bool hasErrors() const { return !errors.empty(); }
    bool hasWarnings() const { return !warnings.empty(); }
};

// Error information
struct ParseError {
    enum class Kind {
        SYNTAX_ERROR,
        UNEXPECTED_TOKEN,
        UNEXPECTED_EOF,
        INVALID_EXPRESSION,
        INVALID_IDENTIFIER,
        AMBIGUOUS_GRAMMAR,
        DEPTH_EXCEEDED,
        MEMORY_EXCEEDED
    };
    
    Kind kind;
    std::string message;
    std::optional<std::string> hint;
    SourceLocation location;
    std::vector<SourceLocation> related_locations;
};

// Warning information
struct ParseWarning {
    enum class Kind {
        DEPRECATED_SYNTAX,
        AMBIGUOUS_COLUMN,
        IMPLICIT_CONVERSION,
        PERFORMANCE_HINT
    };
    
    Kind kind;
    std::string message;
    SourceLocation location;
};
```

### Streaming Interface

```cpp
class SQLParser::StreamParser {
public:
    // Feed chunks of SQL
    void feed(std::string_view chunk);
    
    // Check if complete statement available
    bool hasStatement() const;
    
    // Parse next available statement
    ParseResult parseNext();
    
    // Signal end of input
    void finish();
    
    // Reset state
    void reset();
    
private:
    std::string buffer_;
    size_t position_;
    std::unique_ptr<Tokenizer> tokenizer_;
};
```

---

## AST Interface

### Base AST Node

```cpp
class ASTNode {
public:
    // Node type identification
    enum class Type : uint8_t {
        // Statements
        SELECT_STMT,
        INSERT_STMT,
        UPDATE_STMT,
        DELETE_STMT,
        CREATE_TABLE_STMT,
        // ... more types
    };
    
    // Core interface
    virtual Type getType() const = 0;
    virtual void accept(ASTVisitor& visitor) = 0;
    
    // Tree navigation
    ASTNode* getParent() const;
    std::span<ASTNode*> getChildren() const;
    
    // Source location
    SourceLocation getLocation() const;
    
    // Utility
    template<typename T>
    T* as() {
        return dynamic_cast<T*>(this);
    }
    
    template<typename T>
    const T* as() const {
        return dynamic_cast<const T*>(this);
    }
    
protected:
    ASTNode* parent_;
    std::vector<ASTNode*> children_;
    SourceLocation location_;
};
```

### Statement Nodes

```cpp
// SELECT statement
class SelectStmt : public ASTNode {
public:
    // Components
    SelectList* getSelectList() const;
    FromClause* getFromClause() const;
    WhereClause* getWhereClause() const;
    GroupByClause* getGroupByClause() const;
    HavingClause* getHavingClause() const;
    OrderByClause* getOrderByClause() const;
    LimitClause* getLimitClause() const;
    
    // Properties
    bool isDistinct() const;
    bool isForUpdate() const;
    
    // Visitor
    void accept(ASTVisitor& visitor) override {
        visitor.visit(this);
    }
};

// INSERT statement
class InsertStmt : public ASTNode {
public:
    TableRef* getTable() const;
    std::vector<ColumnRef*> getColumns() const;
    
    // Values or Select
    std::variant<ValuesList*, SelectStmt*> getSource() const;
    
    // Properties
    bool hasOnConflict() const;
    ConflictAction getConflictAction() const;
    
    void accept(ASTVisitor& visitor) override {
        visitor.visit(this);
    }
};
```

### Expression Nodes

```cpp
// Base expression
class Expression : public ASTNode {
public:
    // Expression evaluation info (filled by binder)
    struct EvalInfo {
        DataType type;
        bool is_constant;
        bool is_nullable;
        std::optional<Value> constant_value;
    };
    
    std::optional<EvalInfo> getEvalInfo() const;
    void setEvalInfo(EvalInfo info);
    
private:
    std::optional<EvalInfo> eval_info_;
};

// Binary expression
class BinaryExpr : public Expression {
public:
    enum class Operator {
        // Arithmetic
        ADD, SUBTRACT, MULTIPLY, DIVIDE, MODULO,
        // Comparison
        EQUAL, NOT_EQUAL, LESS_THAN, GREATER_THAN,
        LESS_EQUAL, GREATER_EQUAL,
        // Logical
        AND, OR,
        // String
        CONCAT,
        // Bitwise
        BIT_AND, BIT_OR, BIT_XOR, BIT_SHIFT_LEFT, BIT_SHIFT_RIGHT
    };
    
    Expression* getLeft() const;
    Expression* getRight() const;
    Operator getOperator() const;
    
    void accept(ASTVisitor& visitor) override {
        visitor.visit(this);
    }
};
```

---

## Binder Interface

### DuckDB-Style Binder

```cpp
namespace db25::binder {

class Binder {
public:
    // Main binding entry point
    BoundStatement bind(const parser::ASTNode* ast);
    
    // Catalog interface
    void setCatalog(std::shared_ptr<Catalog> catalog);
    
    // Scope management
    void pushScope();
    void popScope();
    void addBinding(std::string_view name, BindingInfo info);
    std::optional<BindingInfo> lookupBinding(std::string_view name);
    
private:
    // Binding methods for each node type
    BoundSelect bindSelect(const parser::SelectStmt* stmt);
    BoundExpression bindExpression(const parser::Expression* expr);
    BoundTable bindTable(const parser::TableRef* table);
    
    // Type resolution
    DataType resolveType(const parser::Expression* expr);
    bool canCast(DataType from, DataType to);
    
    // Error handling
    void reportError(std::string message, SourceLocation loc);
    void reportWarning(std::string message, SourceLocation loc);
};

// Bound statement (ready for optimizer)
struct BoundStatement {
    enum class Type {
        SELECT, INSERT, UPDATE, DELETE, CREATE_TABLE
    };
    
    Type type;
    std::unique_ptr<BoundNode> root;
    std::vector<BindingError> errors;
    std::vector<BindingWarning> warnings;
};

// Base bound node
class BoundNode {
public:
    virtual ~BoundNode() = default;
    virtual void accept(BoundVisitor& visitor) = 0;
    
    // Type information
    DataType getType() const { return type_; }
    
protected:
    DataType type_;
    std::vector<std::unique_ptr<BoundNode>> children_;
};
```

---

## Semantic Analyzer Interface

### PostgreSQL-Style Analyzer

```cpp
namespace db25::analyzer {

class SemanticAnalyzer {
public:
    // Main analysis entry point
    AnalyzedQuery analyze(const parser::ASTNode* ast);
    
    // Analysis phases
    void resolveNames(parser::ASTNode* node);
    void checkTypes(parser::ASTNode* node);
    void validateSemantics(parser::ASTNode* node);
    void rewriteQuery(parser::ASTNode* node);
    
    // Catalog access
    void setCatalog(std::shared_ptr<Catalog> catalog);
    
private:
    // Name resolution
    void resolveTableRefs(parser::FromClause* from);
    void resolveColumnRefs(parser::Expression* expr);
    void resolveFunction(parser::FunctionExpr* func);
    
    // Type checking
    void coerceTypes(parser::Expression* expr);
    DataType inferType(parser::Expression* expr);
    
    // Semantic validation
    void validateAggregates(parser::SelectStmt* select);
    void validateGroupBy(parser::SelectStmt* select);
    void validateWindowFunctions(parser::SelectStmt* select);
    void validateSubqueries(parser::Expression* expr);
    
    // Query rewriting
    void expandWildcards(parser::SelectList* list);
    void flattenSubqueries(parser::ASTNode* node);
    void normalizePredicates(parser::Expression* expr);
};

// Analyzed query result
struct AnalyzedQuery {
    // Rewritten AST
    std::unique_ptr<parser::ASTNode> ast;
    
    // Analysis results
    struct Metadata {
        std::vector<TableInfo> referenced_tables;
        std::vector<ColumnInfo> referenced_columns;
        std::vector<FunctionInfo> called_functions;
        bool has_aggregates;
        bool has_window_functions;
        bool has_subqueries;
        size_t estimated_complexity;
    } metadata;
    
    // Errors and warnings
    std::vector<AnalysisError> errors;
    std::vector<AnalysisWarning> warnings;
};
```

---

## Visitor Pattern

### Base Visitor Interface

```cpp
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;
    
    // Statement visitors
    virtual void visit(SelectStmt* node) = 0;
    virtual void visit(InsertStmt* node) = 0;
    virtual void visit(UpdateStmt* node) = 0;
    virtual void visit(DeleteStmt* node) = 0;
    virtual void visit(CreateTableStmt* node) = 0;
    
    // Expression visitors
    virtual void visit(BinaryExpr* node) = 0;
    virtual void visit(UnaryExpr* node) = 0;
    virtual void visit(FunctionExpr* node) = 0;
    virtual void visit(CaseExpr* node) = 0;
    virtual void visit(CastExpr* node) = 0;
    virtual void visit(ColumnRef* node) = 0;
    virtual void visit(Literal* node) = 0;
    
    // Clause visitors
    virtual void visit(FromClause* node) = 0;
    virtual void visit(WhereClause* node) = 0;
    virtual void visit(GroupByClause* node) = 0;
    virtual void visit(HavingClause* node) = 0;
    virtual void visit(OrderByClause* node) = 0;
    virtual void visit(LimitClause* node) = 0;
    
    // Traversal control
    enum class TraversalControl {
        CONTINUE,      // Continue traversal
        SKIP_CHILDREN, // Skip children of current node
        STOP          // Stop traversal completely
    };
    
    TraversalControl control_ = TraversalControl::CONTINUE;
};
```

### Concrete Visitor Examples

```cpp
// Pretty printer visitor
class PrettyPrintVisitor : public ASTVisitor {
public:
    void visit(SelectStmt* node) override {
        output_ << "SELECT ";
        if (node->isDistinct()) output_ << "DISTINCT ";
        // ... format select statement
    }
    
    std::string getOutput() const { return output_.str(); }
    
private:
    std::stringstream output_;
    int indent_level_ = 0;
};

// Statistics collector visitor
class StatsCollectorVisitor : public ASTVisitor {
public:
    void visit(SelectStmt* node) override {
        stats_.select_count++;
        // Continue traversal
    }
    
    void visit(FunctionExpr* node) override {
        stats_.function_calls++;
        if (node->isAggregate()) stats_.aggregate_calls++;
        if (node->isWindow()) stats_.window_calls++;
    }
    
    struct Statistics {
        size_t select_count = 0;
        size_t function_calls = 0;
        size_t aggregate_calls = 0;
        size_t window_calls = 0;
        // ... more statistics
    } stats_;
};
```

---

## Type System

### Data Types

```cpp
enum class DataType {
    // Numeric types
    BOOLEAN,
    TINYINT,
    SMALLINT,
    INTEGER,
    BIGINT,
    DECIMAL,
    REAL,
    DOUBLE,
    
    // String types
    CHAR,
    VARCHAR,
    TEXT,
    
    // Date/Time types
    DATE,
    TIME,
    TIMESTAMP,
    INTERVAL,
    
    // Binary types
    BLOB,
    
    // Complex types
    ARRAY,
    STRUCT,
    MAP,
    
    // Special
    NULL_TYPE,
    UNKNOWN
};

// Type information
struct TypeInfo {
    DataType base_type;
    std::optional<size_t> precision;
    std::optional<size_t> scale;
    std::optional<size_t> length;
    bool nullable = true;
    
    // For complex types
    std::vector<TypeInfo> element_types;
    std::unordered_map<std::string, TypeInfo> field_types;
};
```

---

## Error Handling

### Error Context

```cpp
class ParseContext {
public:
    // Error reporting
    void reportError(ParseError error);
    void reportWarning(ParseWarning warning);
    
    // Error recovery
    void enterErrorRecovery();
    void exitErrorRecovery();
    bool isInErrorRecovery() const;
    
    // Synchronization points
    void addSyncPoint(TokenType type);
    bool synchronize();
    
    // Context information
    void pushContext(std::string description);
    void popContext();
    std::string getCurrentContext() const;
    
private:
    std::vector<ParseError> errors_;
    std::vector<ParseWarning> warnings_;
    std::vector<std::string> context_stack_;
    bool in_error_recovery_ = false;
};
```

---

## Performance Monitoring

### Parser Metrics

```cpp
class ParserMetrics {
public:
    // Timing
    void startTimer(std::string_view phase);
    void stopTimer(std::string_view phase);
    
    // Counters
    void incrementCounter(std::string_view name);
    void addSample(std::string_view metric, double value);
    
    // Memory tracking
    void recordAllocation(size_t bytes);
    void recordDeallocation(size_t bytes);
    
    // Reporting
    struct Report {
        std::unordered_map<std::string, double> timings;
        std::unordered_map<std::string, size_t> counters;
        std::unordered_map<std::string, double> averages;
        size_t peak_memory;
        size_t total_allocated;
    };
    
    Report generateReport() const;
    void reset();
};
```

---

## Integration Examples

### Complete Parse Pipeline

```cpp
// Example: Full parsing pipeline
void parseAndAnalyze(std::string_view sql) {
    // 1. Create parser
    db25::parser::SQLParser parser({
        .dialect = SQLParser::Config::Dialect::PostgreSQL,
        .strict_mode = true
    });
    
    // 2. Parse SQL
    auto result = parser.parseStatement(sql);
    if (!result.isSuccess()) {
        for (const auto& error : result.errors) {
            std::cerr << "Parse error: " << error.message << "\n";
        }
        return;
    }
    
    // 3. Bind names (DuckDB style)
    db25::binder::Binder binder;
    binder.setCatalog(catalog);
    auto bound = binder.bind(result.ast.get());
    
    // 4. Semantic analysis (PostgreSQL style)
    db25::analyzer::SemanticAnalyzer analyzer;
    analyzer.setCatalog(catalog);
    auto analyzed = analyzer.analyze(result.ast.get());
    
    // 5. Pass to optimizer
    optimizer.optimize(analyzed.ast.get());
}
```

### Custom Visitor Implementation

```cpp
// Example: Find all table references
class TableFinderVisitor : public ASTVisitor {
public:
    void visit(TableRef* node) override {
        tables_.push_back({
            .schema = node->getSchema(),
            .table = node->getTable(),
            .alias = node->getAlias()
        });
    }
    
    // Default implementations for other nodes
    void visit(SelectStmt* node) override {
        // Continue traversal
        for (auto* child : node->getChildren()) {
            child->accept(*this);
        }
    }
    
    std::vector<TableInfo> getTables() const { return tables_; }
    
private:
    std::vector<TableInfo> tables_;
};
```

---

## Thread Safety

### Concurrency Model

```cpp
// Parser is thread-safe for concurrent parsing
class ThreadSafeParser {
public:
    // Each thread gets its own parser instance
    ParseResult parse(std::string_view sql) {
        thread_local SQLParser parser(config_);
        return parser.parseStatement(sql);
    }
    
private:
    SQLParser::Config config_;
};

// Shared string pool with lock-free operations
class StringPool {
public:
    std::string_view intern(std::string_view str) {
        // Lock-free hash table lookup
        auto hash = std::hash<std::string_view>{}(str);
        auto& bucket = buckets_[hash % NUM_BUCKETS];
        
        // RCU-style read
        auto* current = bucket.load(std::memory_order_acquire);
        while (current) {
            if (current->str == str) {
                return current->str;
            }
            current = current->next;
        }
        
        // Add new string (rare path, uses lock)
        return internSlow(str, hash);
    }
    
private:
    static constexpr size_t NUM_BUCKETS = 1024;
    std::atomic<Node*> buckets_[NUM_BUCKETS];
};
```

---

## Extensibility

### Custom SQL Extensions

```cpp
// Register custom functions
class ExtensionRegistry {
public:
    // Register custom function
    void registerFunction(
        std::string_view name,
        std::vector<DataType> arg_types,
        DataType return_type,
        std::function<Value(std::span<Value>)> impl
    );
    
    // Register custom operator
    void registerOperator(
        std::string_view symbol,
        int precedence,
        Associativity assoc,
        std::function<Expression*(Expression*, Expression*)> builder
    );
    
    // Register custom statement type
    void registerStatement(
        std::string_view keyword,
        std::function<ASTNode*(Parser&)> parser
    );
};
```

---

## Conclusion

The DB25 Parser interface design provides:

1. **Clean separation** between parser, binder, and analyzer
2. **Type safety** through strong typing and templates
3. **Performance** via zero-copy interfaces and visitor pattern
4. **Flexibility** through extensibility points
5. **Compatibility** with both DuckDB and PostgreSQL architectures

This interface enables seamless integration with any SQL processing pipeline while maintaining the parser's extreme performance characteristics.

---

**Document Version:** 1.0  
**Last Updated:** March 2025  
**Author:** Chiradip Mandal  
**Organization:** Space-RF.org