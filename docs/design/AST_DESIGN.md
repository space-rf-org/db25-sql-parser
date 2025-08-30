# DB25 Parser - AST Node Design Specification

**Version:** 1.0  
**Date:** March 2025  
**Author:** Chiradip Mandal  
**Organization:** Space-RF.org

## Overview

This document provides detailed specifications for the Abstract Syntax Tree (AST) node architecture of the DB25 SQL Parser. The design emphasizes memory efficiency, cache locality, and traversal performance.

## Core Design Principles

1. **Fixed-Size Nodes**: All nodes are exactly 64 bytes (one cache line)
2. **Type Erasure**: Polymorphism without virtual functions
3. **Intrusive Lists**: Child nodes contain their own linking pointers
4. **Arena Allocation**: All nodes allocated from per-query arenas
5. **Zero-Copy**: String data referenced via string_view

---

## Node Type Hierarchy

```
┌──────────────────────────────────────────────────────────────────────┐
│                        Complete Node Taxonomy                         │
├──────────────────────────────────────────────────────────────────────┤
│                                                                        │
│ ASTNode (Abstract Base)                                              │
│ ├── Statement Nodes                                                  │
│ │   ├── SelectStmt                                                   │
│ │   ├── InsertStmt                                                   │
│ │   ├── UpdateStmt                                                   │
│ │   ├── DeleteStmt                                                   │
│ │   ├── CreateTableStmt                                              │
│ │   ├── CreateIndexStmt                                              │
│ │   ├── CreateViewStmt                                               │
│ │   ├── AlterTableStmt                                               │
│ │   ├── DropStmt                                                     │
│ │   ├── TruncateStmt                                                 │
│ │   ├── TransactionStmt                                              │
│ │   └── ExplainStmt                                                  │
│ │                                                                     │
│ ├── Expression Nodes                                                 │
│ │   ├── BinaryExpr                                                   │
│ │   │   ├── ComparisonExpr (=, !=, <, >, <=, >=)                   │
│ │   │   ├── LogicalExpr (AND, OR)                                   │
│ │   │   ├── ArithmeticExpr (+, -, *, /, %)                         │
│ │   │   └── BitwiseExpr (&, |, ^, <<, >>)                          │
│ │   ├── UnaryExpr                                                    │
│ │   │   ├── NotExpr                                                 │
│ │   │   ├── NegateExpr                                              │
│ │   │   └── BitwiseNotExpr                                          │
│ │   ├── FunctionExpr                                                 │
│ │   ├── CaseExpr                                                     │
│ │   ├── CastExpr                                                     │
│ │   ├── SubqueryExpr                                                 │
│ │   ├── ExistsExpr                                                   │
│ │   ├── InExpr                                                       │
│ │   ├── BetweenExpr                                                  │
│ │   └── WindowExpr                                                   │
│ │                                                                     │
│ ├── Clause Nodes                                                     │
│ │   ├── FromClause                                                   │
│ │   ├── WhereClause                                                  │
│ │   ├── GroupByClause                                                │
│ │   ├── HavingClause                                                 │
│ │   ├── OrderByClause                                                │
│ │   ├── LimitClause                                                  │
│ │   ├── WithClause (CTE)                                            │
│ │   └── ReturningClause                                              │
│ │                                                                     │
│ ├── Reference Nodes                                                  │
│ │   ├── TableRef                                                     │
│ │   ├── ColumnRef                                                    │
│ │   ├── AliasRef                                                     │
│ │   └── SchemaRef                                                    │
│ │                                                                     │
│ ├── Literal Nodes                                                    │
│ │   ├── IntegerLiteral                                               │
│ │   ├── FloatLiteral                                                 │
│ │   ├── StringLiteral                                                │
│ │   ├── BooleanLiteral                                               │
│ │   ├── NullLiteral                                                  │
│ │   └── DateTimeLiteral                                              │
│ │                                                                     │
│ └── Join Nodes                                                       │
│     ├── InnerJoin                                                    │
│     ├── LeftJoin                                                     │
│     ├── RightJoin                                                    │
│     ├── FullJoin                                                     │
│     ├── CrossJoin                                                    │
│     └── LateralJoin                                                  │
│                                                                       │
└──────────────────────────────────────────────────────────────────────┘
```

---

## Memory Layout Specification

### Base Node Structure (64 bytes)

```cpp
// Conceptual structure - actual implementation uses unions
struct alignas(64) ASTNode {
    // Header (8 bytes)
    uint8_t  node_type;      // NodeType enum
    uint8_t  flags;          // Various flags (is_distinct, is_all, etc.)
    uint16_t child_count;    // Number of children
    uint32_t source_loc;     // Packed line:20 + column:12
    
    // Links (24 bytes)
    ASTNode* parent;         // Parent node
    ASTNode* first_child;    // First child in list
    ASTNode* next_sibling;   // Next sibling in parent's child list
    
    // Payload (32 bytes)
    union {
        // For expressions
        struct {
            ASTNode* left;
            ASTNode* right;
            uint32_t op_type;
            uint32_t precedence;
        } binary;
        
        // For literals
        struct {
            union {
                int64_t  int_val;
                double   float_val;
                bool     bool_val;
            };
            string_view str_val;
        } literal;
        
        // For references
        struct {
            string_view schema;
            string_view table;
            string_view column;
        } reference;
        
        // For function calls
        struct {
            string_view name;
            ASTNode* args;
            bool is_aggregate;
            bool is_window;
        } function;
    };
};
```

---

## Node-Specific Layouts

### SELECT Statement Node

```
┌──────────────────────────────────────────────────────────────────────┐
│                        SelectStmt Layout                              │
├──────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  Header (8 bytes)                                                     │
│  ├─ node_type: SELECT_STMT                                           │
│  ├─ flags: DISTINCT | ALL | FOR_UPDATE                               │
│  ├─ child_count: number of clauses                                   │
│  └─ source_loc: position in original query                           │
│                                                                        │
│  Links (24 bytes)                                                     │
│  ├─ parent: nullptr (top-level) or parent query                      │
│  ├─ first_child: → SelectList                                        │
│  └─ next_sibling: → next statement in batch                          │
│                                                                        │
│  Children (linked list)                                               │
│  ├─ SelectList    → select expressions                               │
│  ├─ FromClause    → table references                                 │
│  ├─ WhereClause   → filter predicate                                 │
│  ├─ GroupByClause → grouping expressions                             │
│  ├─ HavingClause  → aggregate filter                                 │
│  ├─ OrderByClause → sorting specification                            │
│  ├─ LimitClause   → row limit/offset                                 │
│  └─ WithClause    → CTEs (if present)                                │
│                                                                        │
└──────────────────────────────────────────────────────────────────────┘
```

### Binary Expression Node

```
┌──────────────────────────────────────────────────────────────────────┐
│                      BinaryExpr Layout                                │
├──────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  Header (8 bytes)                                                     │
│  ├─ node_type: BINARY_EXPR                                           │
│  ├─ flags: operator properties                                       │
│  ├─ child_count: 2 (left and right)                                  │
│  └─ source_loc: operator position                                    │
│                                                                        │
│  Links (24 bytes)                                                     │
│  ├─ parent: → parent expression                                      │
│  ├─ first_child: → left operand                                      │
│  └─ next_sibling: → next in parent                                   │
│                                                                        │
│  Binary-Specific (32 bytes)                                           │
│  ├─ left: → left expression (redundant with first_child)             │
│  ├─ right: → right expression                                        │
│  ├─ op_type: PLUS | MINUS | MULTIPLY | DIVIDE | etc.                 │
│  ├─ precedence: operator precedence level                            │
│  └─ associativity: LEFT | RIGHT                                      │
│                                                                        │
└──────────────────────────────────────────────────────────────────────┘
```

### Table Reference Node

```
┌──────────────────────────────────────────────────────────────────────┐
│                        TableRef Layout                                │
├──────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  Header (8 bytes)                                                     │
│  ├─ node_type: TABLE_REF                                             │
│  ├─ flags: is_lateral | is_only | etc.                               │
│  ├─ child_count: 0 or 1 (for subquery)                               │
│  └─ source_loc: table name position                                  │
│                                                                        │
│  Links (24 bytes)                                                     │
│  ├─ parent: → FROM clause                                            │
│  ├─ first_child: → subquery (if table subquery)                      │
│  └─ next_sibling: → next table in FROM                               │
│                                                                        │
│  Reference-Specific (32 bytes)                                        │
│  ├─ schema: string_view to schema name                               │
│  ├─ table: string_view to table name                                 │
│  ├─ alias: string_view to alias                                      │
│  └─ sample_clause: → sampling specification                          │
│                                                                        │
└──────────────────────────────────────────────────────────────────────┘
```

---

## Traversal Patterns

### Visitor Pattern Implementation

```
┌──────────────────────────────────────────────────────────────────────┐
│                     Visitor Pattern Design                            │
├──────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  Base Visitor Interface                                               │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ class ASTVisitor {                                             │   │
│  │   // Statement visitors                                        │   │
│  │   virtual void visit(SelectStmt*) = 0;                         │   │
│  │   virtual void visit(InsertStmt*) = 0;                         │   │
│  │   virtual void visit(UpdateStmt*) = 0;                         │   │
│  │   virtual void visit(DeleteStmt*) = 0;                         │   │
│  │                                                                │   │
│  │   // Expression visitors                                       │   │
│  │   virtual void visit(BinaryExpr*) = 0;                         │   │
│  │   virtual void visit(UnaryExpr*) = 0;                          │   │
│  │   virtual void visit(FunctionExpr*) = 0;                       │   │
│  │   virtual void visit(LiteralExpr*) = 0;                        │   │
│  │                                                                │   │
│  │   // Traversal control                                         │   │
│  │   void traverse(ASTNode* root, TraversalOrder order);         │   │
│  │ };                                                             │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                        │
│  Traversal Orders                                                     │
│  ├─ PRE_ORDER:  Visit node before children                           │
│  ├─ POST_ORDER: Visit node after children                            │
│  ├─ IN_ORDER:   Visit between children (binary only)                 │
│  └─ LEVEL_ORDER: Breadth-first traversal                             │
│                                                                        │
└──────────────────────────────────────────────────────────────────────┘
```

### Iterator Interface

```
┌──────────────────────────────────────────────────────────────────────┐
│                      AST Iterator Design                              │
├──────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  Iterator Types                                                       │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ // Depth-first iterator                                        │   │
│  │ class DFSIterator {                                            │   │
│  │   ASTNode* current;                                            │   │
│  │   stack<ASTNode*> stack;                                       │   │
│  │   DFSIterator& operator++();                                   │   │
│  │   ASTNode* operator*();                                        │   │
│  │ };                                                             │   │
│  │                                                                │   │
│  │ // Breadth-first iterator                                      │   │
│  │ class BFSIterator {                                            │   │
│  │   ASTNode* current;                                            │   │
│  │   queue<ASTNode*> queue;                                       │   │
│  │   BFSIterator& operator++();                                   │   │
│  │   ASTNode* operator*();                                        │   │
│  │ };                                                             │   │
│  │                                                                │   │
│  │ // Child iterator                                               │   │
│  │ class ChildIterator {                                           │   │
│  │   ASTNode* current;                                            │   │
│  │   ChildIterator& operator++() {                                │   │
│  │     current = current->next_sibling;                           │   │
│  │   }                                                            │   │
│  │ };                                                             │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                        │
└──────────────────────────────────────────────────────────────────────┘
```

---

## Memory Management

### Arena Allocation Strategy

```
┌──────────────────────────────────────────────────────────────────────┐
│                    Arena Allocation for AST                           │
├──────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  Arena Layout for Query AST                                           │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ Block 0 (64KB) - Initial                                       │   │
│  │ ┌────────────────────────────────────────────────────────┐   │   │
│  │ │ SelectStmt (64B) │ SelectList (64B) │ FromClause (64B) │   │   │
│  │ ├────────────────────────────────────────────────────────┤   │   │
│  │ │ WhereClause (64B) │ BinaryExpr (64B) │ ColumnRef (64B) │   │   │
│  │ ├────────────────────────────────────────────────────────┤   │   │
│  │ │ ... additional nodes ...                                │   │   │
│  │ └────────────────────────────────────────────────────────┘   │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                        │
│  Allocation Pattern                                                   │
│  • Linear bump allocation                                            │
│  • 64-byte alignment guaranteed                                      │
│  • No gaps between nodes                                             │
│  • Predictable memory layout                                         │
│  • Bulk deallocation after query                                     │
│                                                                        │
└──────────────────────────────────────────────────────────────────────┘
```

### Node Pool Design

```
┌──────────────────────────────────────────────────────────────────────┐
│                        Node Pool Design                               │
├──────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  Fast Node Allocation                                                 │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ class NodePool {                                               │   │
│  │   struct Block {                                               │   │
│  │     alignas(64) uint8_t data[65536];  // 64KB                 │   │
│  │     size_t used;                                              │   │
│  │   };                                                          │   │
│  │                                                                │   │
│  │   vector<unique_ptr<Block>> blocks;                           │   │
│  │   Block* current_block;                                        │   │
│  │                                                                │   │
│  │   void* allocate_node() {                                      │   │
│  │     if (current_block->used + 64 > 65536) {                   │   │
│  │       allocate_new_block();                                   │   │
│  │     }                                                         │   │
│  │     void* ptr = &current_block->data[current_block->used];   │   │
│  │     current_block->used += 64;                               │   │
│  │     return ptr;                                               │   │
│  │   }                                                           │   │
│  │ };                                                             │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                        │
└──────────────────────────────────────────────────────────────────────┘
```

---

## Optimization Techniques

### Cache Line Optimization

```
┌──────────────────────────────────────────────────────────────────────┐
│                    Cache Line Optimization                            │
├──────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  Hot Field Placement                                                  │
│  • node_type at offset 0 for fast dispatch                          │
│  • first_child at offset 16 for traversal                            │
│  • Frequently accessed fields in first 32 bytes                      │
│                                                                        │
│  Prefetch Strategy                                                    │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ void traverse_with_prefetch(ASTNode* node) {                  │   │
│  │   if (node->first_child) {                                    │   │
│  │     __builtin_prefetch(node->first_child, 0, 3);            │   │
│  │   }                                                           │   │
│  │   if (node->next_sibling) {                                   │   │
│  │     __builtin_prefetch(node->next_sibling, 0, 2);           │   │
│  │   }                                                           │   │
│  │   // Process current node                                     │   │
│  │   visit(node);                                                │   │
│  │ }                                                             │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                        │
└──────────────────────────────────────────────────────────────────────┘
```

### SIMD Node Processing

```
┌──────────────────────────────────────────────────────────────────────┐
│                      SIMD Node Processing                             │
├──────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  Batch Node Type Classification                                       │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ // Process 8 nodes at once using AVX2                         │   │
│  │ void classify_nodes_simd(ASTNode* nodes[8]) {                 │   │
│  │   // Load node types into SIMD register                       │   │
│  │   __m256i types = _mm256_set_epi32(                          │   │
│  │     nodes[7]->node_type, nodes[6]->node_type,                │   │
│  │     nodes[5]->node_type, nodes[4]->node_type,                │   │
│  │     nodes[3]->node_type, nodes[2]->node_type,                │   │
│  │     nodes[1]->node_type, nodes[0]->node_type                 │   │
│  │   );                                                          │   │
│  │                                                                │   │
│  │   // Compare with statement types                             │   │
│  │   __m256i stmt_mask = _mm256_cmpgt_epi32(types, STMT_MIN);   │   │
│  │   __m256i expr_mask = _mm256_cmpgt_epi32(types, EXPR_MIN);   │   │
│  │                                                                │   │
│  │   // Extract masks for branching                              │   │
│  │   uint32_t stmts = _mm256_movemask_epi8(stmt_mask);          │   │
│  │   uint32_t exprs = _mm256_movemask_epi8(expr_mask);          │   │
│  │ }                                                             │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                        │
└──────────────────────────────────────────────────────────────────────┘
```

---

## AST Validation

### Structural Validation

```
┌──────────────────────────────────────────────────────────────────────┐
│                     AST Structural Validation                         │
├──────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  Validation Rules                                                     │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ 1. Parent-Child Consistency                                    │   │
│  │    • Every child's parent points back correctly                │   │
│  │    • child_count matches actual children                       │   │
│  │                                                                │   │
│  │ 2. Type Constraints                                            │   │
│  │    • Binary expressions have exactly 2 children                │   │
│  │    • Unary expressions have exactly 1 child                    │   │
│  │    • Literals have no children                                 │   │
│  │                                                                │   │
│  │ 3. Semantic Constraints                                        │   │
│  │    • SELECT must have FROM clause (except subquery)            │   │
│  │    • GROUP BY requires aggregate or HAVING                     │   │
│  │    • Window functions not in WHERE/GROUP BY                    │   │
│  │                                                                │   │
│  │ 4. Memory Constraints                                          │   │
│  │    • All nodes 64-byte aligned                                 │   │
│  │    • All nodes within arena bounds                             │   │
│  │    • No cycles in tree structure                               │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                        │
└──────────────────────────────────────────────────────────────────────┘
```

---

## Performance Characteristics

### Expected Performance Metrics

```
┌──────────────────────────────────────────────────────────────────────┐
│                    AST Performance Metrics                            │
├──────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  Construction Performance                                             │
│  • Node allocation: < 10 ns per node                                 │
│  • Tree building: < 100 ns per statement                             │
│  • Total overhead: < 5% of parse time                                │
│                                                                        │
│  Traversal Performance                                                │
│  • Depth-first: 2-3 ns per node                                      │
│  • Breadth-first: 3-4 ns per node                                    │
│  • Visitor dispatch: < 1 ns overhead                                 │
│                                                                        │
│  Memory Efficiency                                                    │
│  • Fixed 64 bytes per node                                           │
│  • No fragmentation with arena allocation                            │
│  • Typical query: 10-100 nodes (640-6400 bytes)                     │
│  • Complex query: 100-1000 nodes (6.4-64 KB)                         │
│                                                                        │
│  Cache Performance                                                    │
│  • L1 hit rate: > 95% during traversal                               │
│  • Sequential access pattern                                         │
│  • Prefetch effectiveness: > 80%                                     │
│                                                                        │
└──────────────────────────────────────────────────────────────────────┘
```

---

## Conclusion

This AST design provides:

1. **Optimal memory layout** with fixed 64-byte nodes
2. **Cache-friendly** traversal patterns
3. **Fast allocation** via arena allocator
4. **Type safety** without virtual function overhead
5. **Flexibility** for all SQL constructs

The design enables the parser to build and traverse ASTs at maximum speed while maintaining clean interfaces for the binder and semantic analyzer stages.

---

**Document Version:** 1.0  
**Last Updated:** March 2025  
**Author:** Chiradip Mandal  
**Organization:** Space-RF.org