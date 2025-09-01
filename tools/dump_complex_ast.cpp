/*
 * DB25 Parser - Ultra-Complex AST Dumper
 * Parses and visualizes AST for complex SQL queries
 */

#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>
#include <functional>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

// Color codes for terminal output
const char* RESET = "\033[0m";
const char* RED = "\033[31m";
const char* GREEN = "\033[32m";
const char* YELLOW = "\033[33m";
const char* BLUE = "\033[34m";
const char* MAGENTA = "\033[35m";
const char* CYAN = "\033[36m";
const char* GRAY = "\033[90m";
const char* BOLD = "\033[1m";

// Get color for node type
const char* get_node_color(NodeType type) {
    switch(type) {
        case NodeType::SelectStmt:
        case NodeType::InsertStmt:
        case NodeType::UpdateStmt:
        case NodeType::DeleteStmt:
        case NodeType::CreateTableStmt:
        case NodeType::CreateIndexStmt:
        case NodeType::CreateViewStmt:
        case NodeType::AlterTableStmt:
        case NodeType::DropStmt:
        case NodeType::TruncateStmt:
            return BLUE;
            
        case NodeType::CTEClause:
        case NodeType::CTEDefinition:
            return MAGENTA;
            
        case NodeType::FromClause:
        case NodeType::WhereClause:
        case NodeType::GroupByClause:
        case NodeType::HavingClause:
        case NodeType::OrderByClause:
        case NodeType::LimitClause:
            return GREEN;
            
        case NodeType::JoinClause:
        case NodeType::InnerJoin:
        case NodeType::LeftJoin:
        case NodeType::RightJoin:
        case NodeType::FullJoin:
        case NodeType::CrossJoin:
            return CYAN;
            
        case NodeType::BinaryExpr:
        case NodeType::UnaryExpr:
        case NodeType::FunctionCall:
        case NodeType::CaseExpr:
            return YELLOW;
            
        case NodeType::TableRef:
        case NodeType::ColumnRef:
        case NodeType::IntegerLiteral:
        case NodeType::FloatLiteral:
        case NodeType::StringLiteral:
        case NodeType::BooleanLiteral:
        case NodeType::NullLiteral:
        case NodeType::Identifier:
            return GRAY;
            
        default:
            return RESET;
    }
}

// Get node type name
const char* get_node_type_name(NodeType type) {
    switch(type) {
        case NodeType::Unknown: return "Unknown";
        case NodeType::SelectStmt: return "SELECT";
        case NodeType::InsertStmt: return "INSERT";
        case NodeType::UpdateStmt: return "UPDATE";
        case NodeType::DeleteStmt: return "DELETE";
        case NodeType::CreateTableStmt: return "CREATE_TABLE";
        case NodeType::CreateIndexStmt: return "CREATE_INDEX";
        case NodeType::CreateViewStmt: return "CREATE_VIEW";
        case NodeType::AlterTableStmt: return "ALTER_TABLE";
        case NodeType::DropStmt: return "DROP";
        case NodeType::TruncateStmt: return "TRUNCATE";
        case NodeType::BinaryExpr: return "BINARY_EXPR";
        case NodeType::UnaryExpr: return "UNARY_EXPR";
        case NodeType::FunctionExpr: return "FUNCTION_EXPR";
        case NodeType::FunctionCall: return "FUNCTION_CALL";
        case NodeType::CaseExpr: return "CASE";
        case NodeType::CastExpr: return "CAST";
        case NodeType::SubqueryExpr: return "SUBQUERY_EXPR";
        case NodeType::ExistsExpr: return "EXISTS";
        case NodeType::InExpr: return "IN";
        case NodeType::BetweenExpr: return "BETWEEN";
        case NodeType::LikeExpr: return "LIKE";
        case NodeType::TableRef: return "TABLE";
        case NodeType::ColumnRef: return "COLUMN";
        case NodeType::IntegerLiteral: return "INT_LITERAL";
        case NodeType::FloatLiteral: return "FLOAT_LITERAL";
        case NodeType::StringLiteral: return "STRING_LITERAL";
        case NodeType::BooleanLiteral: return "BOOL_LITERAL";
        case NodeType::NullLiteral: return "NULL_LITERAL";
        case NodeType::Identifier: return "IDENTIFIER";
        case NodeType::Star: return "STAR";
        case NodeType::SelectList: return "SELECT_LIST";
        case NodeType::FromClause: return "FROM";
        case NodeType::WhereClause: return "WHERE";
        case NodeType::GroupByClause: return "GROUP_BY";
        case NodeType::HavingClause: return "HAVING";
        case NodeType::OrderByClause: return "ORDER_BY";
        case NodeType::LimitClause: return "LIMIT";
        case NodeType::JoinClause: return "JOIN";
        case NodeType::InnerJoin: return "INNER_JOIN";
        case NodeType::LeftJoin: return "LEFT_JOIN";
        case NodeType::RightJoin: return "RIGHT_JOIN";
        case NodeType::FullJoin: return "FULL_JOIN";
        case NodeType::CrossJoin: return "CROSS_JOIN";
        case NodeType::ColumnList: return "COLUMN_LIST";
        case NodeType::Subquery: return "SUBQUERY";
        case NodeType::UnionStmt: return "UNION";
        case NodeType::IntersectStmt: return "INTERSECT";
        case NodeType::ExceptStmt: return "EXCEPT";
        case NodeType::WithClause: return "WITH";
        case NodeType::CTEClause: return "CTE_CLAUSE";
        case NodeType::CTEDefinition: return "CTE_DEF";
        case NodeType::WindowSpec: return "WINDOW_SPEC";
        case NodeType::WindowFunction: return "WINDOW_FUNC";
        case NodeType::WindowFrame: return "WINDOW_FRAME";
        case NodeType::PartitionBy: return "PARTITION_BY";
        default: return "???";
    }
}

// Dump AST recursively
void dump_ast(const ASTNode* node, int depth = 0, const std::string& prefix = "", bool is_last = true) {
    if (!node) return;
    
    // Indentation and tree characters
    std::cout << prefix;
    if (depth > 0) {
        std::cout << (is_last ? "└── " : "├── ");
    }
    
    // Node type with color
    const char* color = get_node_color(node->node_type);
    std::cout << color << BOLD << get_node_type_name(node->node_type) << RESET;
    
    // Node ID
    std::cout << GRAY << " [#" << node->node_id << "]" << RESET;
    
    // Primary text if available
    if (!node->primary_text.empty()) {
        std::cout << " " << CYAN << "'" << node->primary_text << "'" << RESET;
    }
    
    // Schema/catalog if available
    if (!node->schema_name.empty()) {
        std::cout << GRAY << " (schema: " << node->schema_name << ")" << RESET;
    }
    
    // Semantic flags if set
    if (node->semantic_flags) {
        std::cout << YELLOW << " flags:" << std::hex << node->semantic_flags << std::dec << RESET;
    }
    
    // Child count
    if (node->child_count > 0) {
        std::cout << GRAY << " [" << node->child_count << " children]" << RESET;
    }
    
    std::cout << std::endl;
    
    // Prepare prefix for children
    std::string child_prefix = prefix;
    if (depth > 0) {
        child_prefix += (is_last ? "    " : "│   ");
    }
    
    // Dump children
    ASTNode* child = node->first_child;
    while (child) {
        bool is_last_child = (child->next_sibling == nullptr);
        dump_ast(child, depth + 1, child_prefix, is_last_child);
        child = child->next_sibling;
    }
}

// Calculate AST statistics
struct ASTStats {
    int total_nodes = 0;
    int max_depth = 0;
    int leaf_nodes = 0;
    std::vector<int> type_counts;
    
    ASTStats() : type_counts(100, 0) {}
};

void collect_stats(const ASTNode* node, ASTStats& stats, int depth = 0) {
    if (!node) return;
    
    stats.total_nodes++;
    if (depth > stats.max_depth) stats.max_depth = depth;
    
    int type_idx = static_cast<int>(node->node_type);
    if (type_idx < stats.type_counts.size()) {
        stats.type_counts[type_idx]++;
    }
    
    if (node->child_count == 0) {
        stats.leaf_nodes++;
    }
    
    ASTNode* child = node->first_child;
    while (child) {
        collect_stats(child, stats, depth + 1);
        child = child->next_sibling;
    }
}

int main(int argc, char* argv[]) {
    // Ultra-complex query that should work with current parser
    std::string complex_query = R"(
WITH RECURSIVE employee_tree AS (
    SELECT 
        e.employee_id,
        e.name,
        e.manager_id,
        1 as level,
        CAST(e.name AS VARCHAR(1000)) as path
    FROM employees e
    WHERE e.manager_id IS NULL
    UNION ALL
    SELECT 
        e.employee_id,
        e.name,
        e.manager_id,
        t.level + 1,
        CONCAT(t.path, ' > ', e.name) as path
    FROM employees e
    INNER JOIN employee_tree t ON e.manager_id = t.employee_id
    WHERE t.level < 10
),
sales_summary AS (
    SELECT 
        s.employee_id,
        s.product_id,
        p.category,
        SUM(s.amount) as total_sales,
        COUNT(DISTINCT s.customer_id) as unique_customers,
        AVG(s.amount) as avg_sale,
        MAX(s.amount) as max_sale,
        MIN(s.amount) as min_sale,
        ROW_NUMBER() OVER (PARTITION BY s.employee_id ORDER BY SUM(s.amount) DESC) as sales_rank,
        DENSE_RANK() OVER (ORDER BY SUM(s.amount) DESC) as global_rank,
        PERCENT_RANK() OVER (PARTITION BY p.category ORDER BY SUM(s.amount)) as category_percentile
    FROM sales s
    INNER JOIN products p ON s.product_id = p.product_id
    LEFT JOIN customers c ON s.customer_id = c.customer_id
    WHERE s.sale_date BETWEEN '2025-01-01' AND '2025-12-31'
      AND s.amount > 0
      AND p.category IN ('Electronics', 'Clothing', 'Books')
      AND (c.country = 'USA' OR c.country = 'Canada' OR c.country IS NULL)
    GROUP BY s.employee_id, s.product_id, p.category
    HAVING SUM(s.amount) > 1000
       AND COUNT(DISTINCT s.customer_id) >= 5
)
SELECT 
    et.employee_id,
    et.name,
    et.path,
    et.level,
    COALESCE(ss.total_sales, 0) as total_sales,
    COALESCE(ss.unique_customers, 0) as customers,
    CASE 
        WHEN ss.global_rank <= 10 THEN 'Top Performer'
        WHEN ss.global_rank <= 50 THEN 'High Performer'
        WHEN ss.global_rank <= 100 THEN 'Good Performer'
        WHEN ss.global_rank IS NOT NULL THEN 'Standard Performer'
        ELSE 'No Sales'
    END as performance_tier,
    CASE ss.category
        WHEN 'Electronics' THEN ss.total_sales * 1.2
        WHEN 'Clothing' THEN ss.total_sales * 1.1
        ELSE ss.total_sales
    END as weighted_sales,
    (SELECT COUNT(*) 
     FROM sales s2 
     WHERE s2.employee_id = et.employee_id 
       AND s2.sale_date >= CURRENT_DATE - INTERVAL '30' DAY) as recent_sales_count,
    EXISTS (
        SELECT 1 
        FROM awards a 
        WHERE a.employee_id = et.employee_id 
          AND a.year = 2025
    ) as has_award
FROM employee_tree et
LEFT JOIN sales_summary ss ON et.employee_id = ss.employee_id
WHERE et.level <= 5
  AND (ss.total_sales > 5000 OR ss.total_sales IS NULL)
ORDER BY 
    et.level,
    ss.global_rank NULLS LAST,
    et.name
LIMIT 100 OFFSET 0
    )";
    
    std::cout << BOLD << "==================================" << RESET << std::endl;
    std::cout << BOLD << "DB25 Parser - Ultra-Complex AST Dump" << RESET << std::endl;
    std::cout << BOLD << "==================================" << RESET << std::endl;
    std::cout << std::endl;
    
    // Parse the query
    Parser parser;
    std::cout << "Parsing query (" << complex_query.length() << " characters)..." << std::endl;
    
    auto result = parser.parse(complex_query);
    
    if (!result.has_value()) {
        std::cerr << RED << "Failed to parse query!" << RESET << std::endl;
        const auto& error = result.error();
        std::cerr << "Error at line " << error.line << ", column " << error.column << std::endl;
        std::cerr << "Message: " << error.message << std::endl;
        return 1;
    }
    
    ASTNode* root = result.value();
    std::cout << GREEN << "✓ Parse successful!" << RESET << std::endl;
    std::cout << std::endl;
    
    // Collect statistics
    ASTStats stats;
    collect_stats(root, stats);
    
    std::cout << BOLD << "AST Statistics:" << RESET << std::endl;
    std::cout << "├── Total Nodes: " << CYAN << stats.total_nodes << RESET << std::endl;
    std::cout << "├── Max Depth: " << CYAN << stats.max_depth << RESET << std::endl;
    std::cout << "├── Leaf Nodes: " << CYAN << stats.leaf_nodes << RESET << std::endl;
    std::cout << "└── Memory Used: " << CYAN << parser.get_memory_used() << " bytes" << RESET << std::endl;
    std::cout << std::endl;
    
    // Node type distribution
    std::cout << BOLD << "Node Type Distribution:" << RESET << std::endl;
    for (int i = 0; i < stats.type_counts.size(); i++) {
        if (stats.type_counts[i] > 0) {
            NodeType type = static_cast<NodeType>(i);
            std::cout << "├── " << get_node_type_name(type) << ": " 
                      << CYAN << stats.type_counts[i] << RESET << std::endl;
        }
    }
    std::cout << std::endl;
    
    // Dump the full AST
    std::cout << BOLD << "Full AST Structure:" << RESET << std::endl;
    std::cout << std::endl;
    dump_ast(root);
    
    // Also save to file
    std::ofstream outfile("complex_ast_dump.txt");
    if (outfile.is_open()) {
        // Redirect cout to file temporarily
        std::streambuf* cout_buf = std::cout.rdbuf();
        std::cout.rdbuf(outfile.rdbuf());
        
        std::cout << "DB25 Parser - Ultra-Complex AST Dump\n";
        std::cout << "=====================================\n\n";
        std::cout << "Query: " << complex_query << "\n\n";
        std::cout << "Total Nodes: " << stats.total_nodes << "\n";
        std::cout << "Max Depth: " << stats.max_depth << "\n";
        std::cout << "Memory Used: " << parser.get_memory_used() << " bytes\n\n";
        
        // Simple text dump without colors
        std::function<void(const ASTNode*, int, const std::string&)> text_dump;
        text_dump = [&](const ASTNode* node, int depth, const std::string& prefix) {
            if (!node) return;
            
            std::cout << prefix << get_node_type_name(node->node_type);
            std::cout << " [#" << node->node_id << "]";
            
            if (!node->primary_text.empty()) {
                std::cout << " '" << node->primary_text << "'";
            }
            
            if (node->semantic_flags) {
                std::cout << " flags:" << std::hex << node->semantic_flags << std::dec;
            }
            
            std::cout << "\n";
            
            ASTNode* child = node->first_child;
            while (child) {
                text_dump(child, depth + 1, prefix + "  ");
                child = child->next_sibling;
            }
        };
        
        text_dump(root, 0, "");
        
        // Restore cout
        std::cout.rdbuf(cout_buf);
        outfile.close();
        
        std::cout << std::endl;
        std::cout << GREEN << "✓ AST dump saved to complex_ast_dump.txt" << RESET << std::endl;
    }
    
    return 0;
}