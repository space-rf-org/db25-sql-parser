/*
 * Copyright (c) 2024 Space-RF.org
 * Copyright (c) 2024 Chiradip Mandal
 * 
 * DB25 AST Node - 128-byte Cache-Aligned Node Structure
 * Version 2.0 - Production Design
 * 
 * Design Principles:
 * - 128 bytes (2 cache lines) for rich in-node data
 * - First cache line: Core navigation and identification
 * - Second cache line: Context-specific data (analysis OR debug, never both)
 * - Zero-copy string handling via std::string_view
 * - Arena allocated for optimal performance
 */

#pragma once

#include "node_types.hpp"
#include <cstdint>
#include <string_view>
#include <span>
#include <expected>

namespace db25::ast {

/**
 * 128-byte cache-aligned AST Node
 * 
 * Memory Layout:
 * - First 64 bytes: Core data needed for traversal
 * - Second 64 bytes: Context-specific analysis or debug data
 * 
 * The context union is modal - either analysis OR debug, determined at
 * parser initialization. This eliminates overhead in production while
 * enabling rich debugging when needed.
 */
struct alignas(128) ASTNode {
    // ========== First Cache Line (64 bytes) ==========
    
    // === Header (8 bytes) ===
    NodeType node_type;           // 1 byte - Node type enum
    NodeFlags flags;              // 1 byte - Boolean flags
    uint16_t child_count;         // 2 bytes - Number of children
    uint32_t node_id;            // 4 bytes - Unique ID for this node
    
    // === Source Mapping (8 bytes) ===
    uint32_t source_start;        // 4 bytes - Start position in source
    uint32_t source_end;          // 4 bytes - End position in source
    
    // === Tree Links (24 bytes) ===
    ASTNode* parent;              // 8 bytes - Parent node
    ASTNode* first_child;         // 8 bytes - First child
    ASTNode* next_sibling;        // 8 bytes - Next sibling
    
    // === Primary Identifier (16 bytes) ===
    std::string_view primary_text; // 16 bytes - Main identifier/literal/keyword
    
    // === Type and Metadata (8 bytes) ===
    DataType data_type;           // 1 byte - Data type for expressions
    uint8_t precedence;           // 1 byte - Operator precedence
    uint16_t semantic_flags;      // 2 bytes - Type-specific flags
    uint32_t hash_cache;          // 4 bytes - Cached hash for comparisons
    
    // ========== Second Cache Line (64 bytes) ==========
    
    // === Secondary Identifiers (32 bytes) ===
    std::string_view schema_name;  // 16 bytes - Schema qualifier
    std::string_view catalog_name; // 16 bytes - Catalog qualifier
    
    // === Context-Specific Data (32 bytes) ===
    // This union is modal: either analysis OR debug, never both
    // Mode is determined at parser construction time
    union ContextData {
        
        // Production Mode: Semantic Analysis & Optimization
        struct AnalysisContext {
            // Expression analysis (for expression nodes)
            int64_t const_value;      // 8 bytes - Constant folded value
            double selectivity;       // 8 bytes - Selectivity estimate (0.0-1.0)
            
            // Reference information (for table/column nodes)
            uint32_t table_id;        // 4 bytes - Table catalog ID
            uint32_t column_id;       // 4 bytes - Column catalog ID
            
            // Optimization hints
            uint32_t cost_estimate;   // 4 bytes - Estimated cost units
            uint16_t cardinality_hint;// 2 bytes - Estimated row count (log scale)
            
            // Properties
            uint8_t nullability : 2;  // 0=unknown, 1=not null, 2=nullable
            uint8_t uniqueness : 2;   // 0=unknown, 1=unique, 2=not unique  
            uint8_t indexed : 1;      // Column is indexed
            uint8_t computed : 1;     // Computed/virtual column
            uint8_t reserved_bits : 2;
            
            uint8_t padding;          // 1 byte padding to reach 32 bytes
        } analysis;
        
        // Debug Mode: Visualization & Profiling
        struct DebugContext {
            // Tree structure
            uint16_t depth;           // 2 bytes - Depth in tree
            uint16_t subtree_size;    // 2 bytes - Total nodes in subtree
            uint32_t subtree_hash;    // 4 bytes - Hash of entire subtree
            
            // Visualization
            const char* display_label; // 8 bytes - Label for visualization
            uint32_t color_hint;      // 4 bytes - Color for rendering
            
            // Profiling
            uint32_t visit_count;     // 4 bytes - Times visited during execution
            uint64_t total_time_ns;   // 8 bytes - Cumulative execution time
        } debug;
        
        // Raw byte access
        uint8_t raw[32];
        
    } context;
    
    // ========== Constructors ==========
    
    // Default constructor
    constexpr ASTNode() noexcept
        : node_type(NodeType::Unknown)
        , flags(NodeFlags::None)
        , child_count(0)
        , node_id(0)
        , source_start(0)
        , source_end(0)
        , parent(nullptr)
        , first_child(nullptr)
        , next_sibling(nullptr)
        , primary_text{}
        , data_type(DataType::Unknown)
        , precedence(0)
        , semantic_flags(0)
        , hash_cache(0)
        , schema_name{}
        , catalog_name{}
        , context{} {}
    
    // Constructor with node type
    explicit constexpr ASTNode(const NodeType type) noexcept
        : node_type(type)
        , flags(NodeFlags::None)
        , child_count(0)
        , node_id(0)
        , source_start(0)
        , source_end(0)
        , parent(nullptr)
        , first_child(nullptr)
        , next_sibling(nullptr)
        , primary_text{}
        , data_type(DataType::Unknown)
        , precedence(0)
        , semantic_flags(0)
        , hash_cache(0)
        , schema_name{}
        , catalog_name{}
        , context{} {}
    
    // ========== Methods ==========
    
    // Node type checking
    [[nodiscard]] constexpr bool is_statement() const noexcept {
        return node_type >= NodeType::SelectStmt && 
               node_type <= NodeType::ExplainStmt;
    }
    
    [[nodiscard]] constexpr bool is_expression() const noexcept {
        return node_type >= NodeType::BinaryExpr && 
               node_type <= NodeType::WindowExpr;
    }
    
    [[nodiscard]] constexpr bool is_literal() const noexcept {
        return node_type >= NodeType::IntegerLiteral && 
               node_type <= NodeType::DateTimeLiteral;
    }
    
    [[nodiscard]] constexpr bool is_identifier() const noexcept {
        return node_type == NodeType::Identifier ||
               node_type == NodeType::ColumnRef ||
               node_type == NodeType::TableRef;
    }
    
    // Tree navigation
    [[nodiscard]] constexpr ASTNode* get_parent() const noexcept { 
        return parent; 
    }
    
    [[nodiscard]] constexpr ASTNode* get_first_child() const noexcept { 
        return first_child; 
    }
    
    [[nodiscard]] constexpr ASTNode* get_next_sibling() const noexcept { 
        return next_sibling; 
    }
    
    // Get all children as span
    [[nodiscard]] std::span<ASTNode*> get_children() const noexcept;
    
    // Tree manipulation
    void add_child(ASTNode* child) noexcept;
    void remove_child(ASTNode* child) noexcept;
    [[nodiscard]] ASTNode* find_child(NodeType type) const noexcept;
    
    // Visitor pattern with C++23 deducing this
    template<typename Visitor>
    auto accept(this auto&& self, Visitor&& visitor) {
        return std::forward<Visitor>(visitor).visit(
            std::forward<decltype(self)>(self)
        );
    }
    
    // Source location helpers
    [[nodiscard]] constexpr std::pair<uint32_t, uint32_t> 
    get_source_range() const noexcept {
        return {source_start, source_end};
    }
    
    [[nodiscard]] constexpr uint32_t get_source_length() const noexcept {
        return source_end - source_start;
    }
    
    // Flag management
    [[nodiscard]] constexpr bool has_flag(const NodeFlags flag) const noexcept {
        return static_cast<uint8_t>(flags & flag) == static_cast<uint8_t>(flag);
    }
    
    constexpr void set_flag(const NodeFlags flag) noexcept {
        flags = flags | flag;
    }
    
    constexpr void clear_flag(const NodeFlags flag) noexcept {
        flags = static_cast<NodeFlags>(
            static_cast<uint8_t>(flags) & ~static_cast<uint8_t>(flag)
        );
    }
    
    // Identifier helpers
    [[nodiscard]] constexpr bool has_schema() const noexcept {
        return !schema_name.empty();
    }
    
    [[nodiscard]] constexpr bool has_catalog() const noexcept {
        return !catalog_name.empty();
    }
    
    [[nodiscard]] std::string get_qualified_name() const {
        std::string result;
        if (has_catalog()) {
            result.append(catalog_name);
            result.append(".");
        }
        if (has_schema()) {
            result.append(schema_name);
            result.append(".");
        }
        result.append(primary_text);
        return result;
    }
};

// Compile-time size verification
static_assert(sizeof(ASTNode) == 128, "ASTNode must be exactly 128 bytes");
static_assert(alignof(ASTNode) == 128, "ASTNode must be 128-byte aligned");

// Verify first cache line is 64 bytes
static_assert(offsetof(ASTNode, schema_name) == 64, 
              "First cache line must be exactly 64 bytes");

// Type traits for C++20/23 concepts
template<typename T>
concept IsASTNode = std::is_same_v<std::remove_cvref_t<T>, ASTNode>;

/**
 * Parser mode selector - determines context union usage
 */
enum class ParserMode {
    Production,  // Use analysis context
    Debug       // Use debug context
};

} // namespace db25::ast