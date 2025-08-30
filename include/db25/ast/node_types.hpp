#pragma once

#include <cstdint>
#include <string>

namespace db25::ast {

/**
 * AST Node Types
 * Based on comprehensive taxonomy from AST_DESIGN.md
 */
enum class NodeType : uint8_t {
    // Unknown/Invalid
    Unknown = 0,
    
    // === Statement Nodes ===
    SelectStmt,
    InsertStmt,
    UpdateStmt,
    DeleteStmt,
    CreateTableStmt,
    CreateIndexStmt,
    CreateViewStmt,
    AlterTableStmt,
    DropStmt,
    TruncateStmt,
    TransactionStmt,
    ExplainStmt,
    ValuesStmt,
    SetStmt,
    VacuumStmt,
    AnalyzeStmt,
    AttachStmt,
    DetachStmt,
    ReindexStmt,
    PragmaStmt,
    BeginStmt,
    CommitStmt,
    RollbackStmt,
    SavepointStmt,
    ReleaseSavepointStmt,
    CreateTriggerStmt,
    CreateSchemaStmt,
    CreateFunctionStmt,
    CreateProcedureStmt,
    AlterSchemaStmt,
    AlterIndexStmt,
    AlterViewStmt,
    
    // === DDL Components ===
    ColumnDefinition,
    ColumnConstraint,
    TableConstraint,
    DataTypeNode,
    DefaultClause,
    CheckConstraint,
    ForeignKeyConstraint,
    PrimaryKeyConstraint,
    UniqueConstraint,
    IndexColumn,
    AlterTableAction,
    
    // === Expression Nodes ===
    BinaryExpr,
    UnaryExpr,
    FunctionExpr,
    FunctionCall,    // Function invocation
    CaseExpr,
    CastExpr,
    SubqueryExpr,
    ExistsExpr,
    InExpr,
    BetweenExpr,
    LikeExpr,        // LIKE pattern matching
    IsNullExpr,      // IS NULL / IS NOT NULL
    WindowExpr,
    
    // === Clause Nodes ===
    SelectList,
    FromClause,
    JoinClause,      // JOIN operations
    WhereClause,
    GroupByClause,
    HavingClause,
    OrderByClause,
    LimitClause,
    WithClause,      // CTE
    ReturningClause,
    OnConflictClause,
    UsingClause,     // JOIN USING or DELETE USING
    ValuesClause,    // VALUES (row), (row), ...
    GroupingElement, // GROUPING SETS/CUBE/ROLLUP
    
    // === Reference Nodes ===
    TableRef,
    ColumnRef,
    AliasRef,
    SchemaRef,
    
    // === Literal Nodes ===
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,
    BooleanLiteral,
    NullLiteral,
    DateTimeLiteral,
    
    // === Join Nodes ===
    InnerJoin,
    LeftJoin,
    RightJoin,
    FullJoin,
    CrossJoin,
    LateralJoin,
    
    // === Other Nodes ===
    Identifier,
    Star,           // SELECT *
    Parameter,      // ? or $1
    Comment,
    
    // === Subquery & CTE Nodes ===
    Subquery,       // (SELECT ...)
    CTEClause,      // WITH clause
    CTEDefinition,  // Single CTE definition
    ColumnList,     // List of column names (for CTE columns)
    
    // === Window Function Nodes ===
    WindowFunction, // Function with OVER clause
    WindowSpec,     // Window specification
    PartitionBy,    // PARTITION BY clause
    PartitionByClause, // PARTITION BY clause (alternate name)
    WindowFrame,    // Frame specification (ROWS/RANGE)
    FrameClause,    // Frame specification (alternate name)
    FrameBound,     // Frame boundary (UNBOUNDED PRECEDING, CURRENT ROW, etc.)
    
    // === Set Operation Nodes ===
    UnionStmt,      // UNION [ALL]
    IntersectStmt,  // INTERSECT [ALL]
    ExceptStmt,     // EXCEPT [ALL]
    
    // === Advanced Type Nodes ===
    IntervalLiteral,    // INTERVAL '1 day'
    ArrayConstructor,   // ARRAY[1,2,3]
    RowConstructor,     // ROW(1,2,3)
    JsonLiteral,        // JSON data
    CollateClause,      // COLLATE collation_name
    
    // === CTE Advanced ===
    SearchClause,       // SEARCH DEPTH/BREADTH FIRST
    CycleClause,        // CYCLE detection
    
    // === Transaction Modes ===
    IsolationLevel,     // ISOLATION LEVEL
    TransactionMode,
    
    // Add more as needed
    MaxNodeType
};

/**
 * Node flags for additional properties
 */
enum class NodeFlags : uint8_t {
    None         = 0,
    Distinct     = 1 << 0,
    All          = 1 << 1,
    ForUpdate    = 1 << 2,
    IsLateral    = 1 << 3,
    IsRecursive  = 1 << 4,
    HasAlias     = 1 << 5,
    IsSubquery   = 1 << 6,
    IsCorrelated = 1 << 7
};

// Flag operations with C++23 improvements
[[nodiscard]] constexpr NodeFlags operator|(const NodeFlags a, const NodeFlags b) noexcept {
    return static_cast<NodeFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

[[nodiscard]] constexpr NodeFlags operator&(const NodeFlags a, const NodeFlags b) noexcept {
    return static_cast<NodeFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

[[nodiscard]] constexpr bool has_flag(const NodeFlags flags, const NodeFlags flag) noexcept {
    return (flags & flag) == flag;
}

/**
 * Binary operator types
 */
enum class BinaryOp : uint8_t {
    // Arithmetic
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    
    // Comparison
    Equal,
    NotEqual,
    LessThan,
    LessEqual,
    GreaterThan,
    GreaterEqual,
    
    // Logical
    And,
    Or,
    
    // String
    Concat,
    Like,
    NotLike,
    
    // Bitwise
    BitAnd,
    BitOr,
    BitXor,
    BitShiftLeft,
    BitShiftRight,
    
    // Other
    Is,
    IsNot,
    In,
    NotIn
};

/**
 * Unary operator types
 */
enum class UnaryOp : uint8_t {
    Not,
    Negate,
    BitwiseNot,
    IsNull,
    IsNotNull,
    Exists,
    NotExists
};

/**
 * Join types
 */
enum class JoinType : uint8_t {
    Inner,
    Left,
    Right,
    Full,
    Cross,
    Lateral
};

/**
 * Set operations
 */
enum class SetOp : uint8_t {
    Union,
    UnionAll,
    Intersect,
    Except
};

/**
 * Sort order
 */
enum class SortOrder : uint8_t {
    Ascending,
    Descending
};

/**
 * Nulls ordering
 */
enum class NullsOrder : uint8_t {
    First,
    Last
};

/**
 * Data types for semantic analysis
 */
enum class DataType : uint8_t {
    Unknown,
    
    // Numeric
    Boolean,
    TinyInt,
    SmallInt,
    Integer,
    BigInt,
    Decimal,
    Real,
    Double,
    
    // String
    Char,
    VarChar,
    Text,
    
    // Date/Time
    Date,
    Time,
    Timestamp,
    Interval,
    
    // Binary
    Blob,
    
    // Complex
    Array,
    Struct,
    Map,
    
    // Special
    Null,
    Any
};

/**
 * Convert enum to string for debugging
 * Using C++23 constexpr improvements
 */
[[nodiscard]] constexpr const char* node_type_to_string(NodeType type) noexcept;
[[nodiscard]] constexpr const char* binary_op_to_string(BinaryOp op) noexcept;
[[nodiscard]] constexpr const char* unary_op_to_string(UnaryOp op) noexcept;
[[nodiscard]] constexpr const char* data_type_to_string(DataType type) noexcept;

} // namespace db25::ast