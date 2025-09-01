/*
 * Test to verify AST Node v2 memory layout
 */

#include <iostream>
#include <cstddef>
#include "db25/ast/ast_node.hpp"

using namespace db25::ast;

int main() {
    std::cout << "=== AST Node v2 Layout Verification ===\n\n";
    
    // Size verification
    std::cout << "Total size: " << sizeof(ASTNode) << " bytes ";
    std::cout << (sizeof(ASTNode) == 128 ? "✓" : "✗") << "\n";
    
    std::cout << "Alignment: " << alignof(ASTNode) << " bytes ";
    std::cout << (alignof(ASTNode) == 128 ? "✓" : "✗") << "\n\n";
    
    // First cache line offsets
    std::cout << "=== First Cache Line (0-63) ===\n";
    std::cout << "node_type:     " << offsetof(ASTNode, node_type) << " (size: " << sizeof(NodeType) << ")\n";
    std::cout << "flags:         " << offsetof(ASTNode, flags) << " (size: " << sizeof(NodeFlags) << ")\n";
    std::cout << "child_count:   " << offsetof(ASTNode, child_count) << "\n";
    std::cout << "node_id:       " << offsetof(ASTNode, node_id) << "\n";
    std::cout << "source_start:  " << offsetof(ASTNode, source_start) << "\n";
    std::cout << "source_end:    " << offsetof(ASTNode, source_end) << "\n";
    std::cout << "parent:        " << offsetof(ASTNode, parent) << "\n";
    std::cout << "first_child:   " << offsetof(ASTNode, first_child) << "\n";
    std::cout << "next_sibling:  " << offsetof(ASTNode, next_sibling) << "\n";
    std::cout << "primary_text:  " << offsetof(ASTNode, primary_text) << " (size: " << sizeof(std::string_view) << ")\n";
    std::cout << "data_type:     " << offsetof(ASTNode, data_type) << "\n";
    std::cout << "precedence:    " << offsetof(ASTNode, precedence) << "\n";
    std::cout << "semantic_flags:" << offsetof(ASTNode, semantic_flags) << "\n";
    std::cout << "hash_cache:    " << offsetof(ASTNode, hash_cache) << "\n";
    
    // Second cache line offsets
    std::cout << "\n=== Second Cache Line (64-127) ===\n";
    std::cout << "schema_name:   " << offsetof(ASTNode, schema_name) << " (size: " << sizeof(std::string_view) << ")\n";
    std::cout << "catalog_name:  " << offsetof(ASTNode, catalog_name) << " (size: " << sizeof(std::string_view) << ")\n";
    std::cout << "context:       " << offsetof(ASTNode, context) << " (size: " << sizeof(ASTNode::ContextData) << ")\n";
    
    // Context union details
    std::cout << "\n=== Context Union Details ===\n";
    std::cout << "analysis size: " << sizeof(ASTNode::ContextData::AnalysisContext) << " bytes\n";
    std::cout << "debug size:    " << sizeof(ASTNode::ContextData::DebugContext) << " bytes\n";
    
    // Verify cache line boundary
    bool first_line_ok = offsetof(ASTNode, schema_name) == 64;
    std::cout << "\n=== Cache Line Boundary Check ===\n";
    std::cout << "Second cache line starts at byte: " << offsetof(ASTNode, schema_name) << " ";
    std::cout << (first_line_ok ? "✓" : "✗") << "\n";
    
    // Usage example
    std::cout << "\n=== Usage Example ===\n";
    ASTNode node(NodeType::SelectStmt);
    node.primary_text = "users";
    node.schema_name = "public";
    node.catalog_name = "db25";
    
    // In production mode, use analysis context
    node.context.analysis.table_id = 42;
    node.context.analysis.selectivity = 0.75;
    node.context.analysis.cost_estimate = 1000;
    
    std::cout << "Qualified name: " << node.get_qualified_name() << "\n";
    std::cout << "Table ID: " << node.context.analysis.table_id << "\n";
    std::cout << "Selectivity: " << node.context.analysis.selectivity << "\n";
    
    std::cout << "\n✓ Layout verification complete\n";
    
    return 0;
}