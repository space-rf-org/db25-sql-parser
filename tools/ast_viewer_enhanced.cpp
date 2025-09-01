/*
 * DB25 SQL Parser - Enhanced AST Viewer
 * 
 * A powerful tool for visualizing Abstract Syntax Trees from SQL queries.
 * Features:
 * - Piped input support
 * - File-based input with index/all options
 * - Vivid color output with multiple color schemes
 * - Summary statistics and batch processing
 * - Progress indicators for large files
 * 
 * Usage:
 *   echo "SELECT * FROM users" | ast_viewer
 *   ast_viewer --file queries.sqls --index 3
 *   ast_viewer --file queries.sqls --all
 *   ast_viewer --file queries.sqls --all --output results.txt
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <unistd.h>
#include <expected>
#include <cstring>
#include <algorithm>

#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include "db25/ast/node_types.hpp"

namespace {

// Enhanced ANSI color codes for vivid visual output
namespace colors {
    // Basic formatting
    const char* RESET = "\033[0m";
    const char* BOLD = "\033[1m";
    const char* DIM = "\033[2m";
    const char* ITALIC = "\033[3m";
    const char* UNDERLINE = "\033[4m";
    const char* BLINK = "\033[5m";
    const char* REVERSE = "\033[7m";
    
    // Vivid foreground colors (256-color mode)
    const char* STMT_COLOR = "\033[38;5;196m";      // Bright Red for statements
    const char* CLAUSE_COLOR = "\033[38;5;39m";     // Deep Sky Blue for clauses  
    const char* EXPR_COLOR = "\033[38;5;214m";      // Orange for expressions
    const char* REF_COLOR = "\033[38;5;220m";       // Gold for references
    const char* LITERAL_COLOR = "\033[38;5;82m";    // Lime Green for literals
    const char* JOIN_COLOR = "\033[38;5;201m";      // Hot Magenta for joins
    const char* FUNC_COLOR = "\033[38;5;51m";       // Cyan for functions
    const char* OPERATOR_COLOR = "\033[38;5;141m";  // Light Purple for operators
    const char* KEYWORD_COLOR = "\033[38;5;207m";   // Pink for keywords
    const char* STAR_COLOR = "\033[38;5;226m";      // Yellow for wildcards
    const char* SUBQUERY_COLOR = "\033[38;5;87m";   // Light Cyan for subqueries
    const char* CTE_COLOR = "\033[38;5;177m";       // Mauve for CTEs
    
    // Background colors for headers
    const char* BG_BLUE = "\033[48;5;17m";
    const char* BG_GREEN = "\033[48;5;22m";
    const char* BG_PURPLE = "\033[48;5;54m";
    const char* BG_RED = "\033[48;5;52m";
    const char* BG_CYAN = "\033[48;5;23m";
    
    // Success/Error indicators
    const char* SUCCESS = "\033[38;5;46m";  // Bright Green
    const char* ERROR = "\033[38;5;196m";   // Bright Red
    const char* WARNING = "\033[38;5;226m"; // Yellow
    const char* INFO = "\033[38;5;45m";     // Turquoise
}

// Box drawing characters for tree visualization
struct BoxStyle {
    const char* VERTICAL;
    const char* HORIZONTAL;
    const char* BRANCH;
    const char* LAST_BRANCH;
    const char* SPACE;
    const char* CROSS;
};

namespace box {
    // Unicode box drawing (for color mode)
    const BoxStyle unicode = {
        "│",    // VERTICAL
        "─",    // HORIZONTAL
        "├─",   // BRANCH
        "└─",   // LAST_BRANCH
        "  ",   // SPACE
        "┼"     // CROSS
    };
    
    // Heavy Unicode box drawing (alternative style)
    const BoxStyle heavy = {
        "┃",    // VERTICAL
        "━",    // HORIZONTAL
        "┣━",   // BRANCH
        "┗━",   // LAST_BRANCH
        "  ",   // SPACE
        "╋"     // CROSS
    };
    
    // ASCII box drawing (for simple mode or no-color)
    const BoxStyle ascii = {
        "|",    // VERTICAL
        "-",    // HORIZONTAL
        "|-",   // BRANCH
        "`-",   // LAST_BRANCH
        "  ",   // SPACE
        "+"     // CROSS
    };
}

// Configuration for display
struct DisplayConfig {
    bool use_colors = true;
    bool simple_mode = false;
    bool show_stats = false;
    bool show_progress = true;
    bool compact_output = false;
    BoxStyle box_style = box::unicode;
};

// Statistics tracking
struct ParseStats {
    int total_queries = 0;
    int successful = 0;
    int failed = 0;
    std::vector<int> failed_indices;
    std::chrono::microseconds total_parse_time{0};
    size_t total_nodes = 0;
    size_t max_depth = 0;
};

// Get node type name as string
const char* get_node_type_name(db25::ast::NodeType type) {
    using namespace db25::ast;
    
    switch(type) {
        // Statements
        case NodeType::SelectStmt: return "SELECT";
        case NodeType::InsertStmt: return "INSERT";
        case NodeType::UpdateStmt: return "UPDATE";
        case NodeType::DeleteStmt: return "DELETE";
        case NodeType::CreateTableStmt: return "CREATE TABLE";
        case NodeType::CreateIndexStmt: return "CREATE INDEX";
        case NodeType::CreateViewStmt: return "CREATE VIEW";
        case NodeType::AlterTableStmt: return "ALTER TABLE";
        case NodeType::DropStmt: return "DROP";
        case NodeType::TruncateStmt: return "TRUNCATE";
        
        // Clauses
        case NodeType::SelectList: return "SELECT LIST";
        case NodeType::FromClause: return "FROM";
        case NodeType::WhereClause: return "WHERE";
        case NodeType::GroupByClause: return "GROUP BY";
        case NodeType::HavingClause: return "HAVING";
        case NodeType::OrderByClause: return "ORDER BY";
        case NodeType::LimitClause: return "LIMIT";
        case NodeType::JoinClause: return "JOIN";
        case NodeType::SetClause: return "SET";
        case NodeType::ValuesClause: return "VALUES";
        case NodeType::ReturningClause: return "RETURNING";
        case NodeType::WithClause: return "WITH";
        
        // Expressions
        case NodeType::BinaryExpr: return "BINARY";
        case NodeType::UnaryExpr: return "UNARY";
        case NodeType::CaseExpr: return "CASE";
        case NodeType::CastExpr: return "CAST";
        case NodeType::FunctionCall: return "FUNCTION";
        case NodeType::InExpr: return "IN";
        case NodeType::ExistsExpr: return "EXISTS";
        case NodeType::BetweenExpr: return "BETWEEN";
        case NodeType::LikeExpr: return "LIKE";
        case NodeType::IsNullExpr: return "IS NULL";
        case NodeType::SubqueryExpr: return "SUBQUERY";
        case NodeType::WindowSpec: return "WINDOW";
        
        // References
        case NodeType::TableRef: return "TABLE";
        case NodeType::ColumnRef: return "COLUMN";
        case NodeType::Star: return "*";
        
        // Literals
        case NodeType::IntegerLiteral: return "INT";
        case NodeType::FloatLiteral: return "FLOAT";
        case NodeType::StringLiteral: return "STRING";
        case NodeType::BooleanLiteral: return "BOOL";
        case NodeType::NullLiteral: return "NULL";
        
        // Others
        case NodeType::Identifier: return "IDENTIFIER";
        case NodeType::CTEDefinition: return "CTE";
        
        // DDL specific
        case NodeType::ColumnDefinition: return "COLUMN_DEF";
        case NodeType::DataTypeNode: return "DATA_TYPE";
        case NodeType::ColumnConstraint: return "COLUMN_CONSTRAINT";
        case NodeType::TableConstraint: return "TABLE_CONSTRAINT";
        
        // Set operations
        case NodeType::UnionStmt: return "UNION";
        case NodeType::IntersectStmt: return "INTERSECT";
        case NodeType::ExceptStmt: return "EXCEPT";
        
        default: {
            static std::string temp;
            temp = "NODE_" + std::to_string(static_cast<int>(type));
            return temp.c_str();
        }
    }
}

// Get color for node type
const char* get_node_color(db25::ast::NodeType type) {
    using namespace db25::ast;
    
    // Statements
    if (type >= NodeType::SelectStmt && type <= NodeType::TruncateStmt) {
        return colors::STMT_COLOR;
    }
    
    // Clauses
    if (type >= NodeType::SelectList && type <= NodeType::WithClause) {
        return colors::CLAUSE_COLOR;
    }
    
    // Expressions
    if (type >= NodeType::BinaryExpr && type <= NodeType::WindowSpec) {
        return colors::EXPR_COLOR;
    }
    
    // References
    if (type == NodeType::TableRef || type == NodeType::ColumnRef) {
        return colors::REF_COLOR;
    }
    
    // Star/Wildcard
    if (type == NodeType::Star) {
        return colors::STAR_COLOR;
    }
    
    // Literals
    if (type >= NodeType::IntegerLiteral && type <= NodeType::NullLiteral) {
        return colors::LITERAL_COLOR;
    }
    
    // Functions
    if (type == NodeType::FunctionCall) {
        return colors::FUNC_COLOR;
    }
    
    // Joins
    if (type == NodeType::JoinClause) {
        return colors::JOIN_COLOR;
    }
    
    // Subqueries
    if (type == NodeType::SubqueryExpr) {
        return colors::SUBQUERY_COLOR;
    }
    
    // CTEs
    if (type == NodeType::CTEDefinition || type == NodeType::WithClause) {
        return colors::CTE_COLOR;
    }
    
    // Set operations
    if (type == NodeType::UnionStmt || type == NodeType::IntersectStmt || type == NodeType::ExceptStmt) {
        return colors::OPERATOR_COLOR;
    }
    
    return colors::RESET;
}

// Calculate AST depth
size_t calculate_depth(const db25::ast::ASTNode* node) {
    if (!node) return 0;
    
    size_t max_child_depth = 0;
    db25::ast::ASTNode* child = node->first_child;
    while (child) {
        max_child_depth = std::max(max_child_depth, calculate_depth(child));
        child = child->next_sibling;
    }
    
    return 1 + max_child_depth;
}

// Count nodes in AST
size_t count_nodes(const db25::ast::ASTNode* node) {
    if (!node) return 0;
    
    size_t count = 1;
    db25::ast::ASTNode* child = node->first_child;
    while (child) {
        count += count_nodes(child);
        child = child->next_sibling;
    }
    
    return count;
}

// Print AST node recursively with beautiful formatting and full information
void print_ast_node(const db25::ast::ASTNode* node, 
                    const std::string& prefix, 
                    bool is_last,
                    const DisplayConfig& config,
                    std::ostream& out = std::cout) {
    if (!node) return;
    
    // Print prefix and branch with better indentation
    out << prefix;
    
    if (config.use_colors && !config.simple_mode) {
        out << colors::DIM;
    }
    
    if (!prefix.empty()) {
        out << (is_last ? config.box_style.LAST_BRANCH : config.box_style.BRANCH);
    }
    
    if (config.use_colors && !config.simple_mode) {
        out << colors::RESET;
    }
    
    // Print node type with color based on node category
    if (config.use_colors && !config.simple_mode) {
        out << get_node_color(node->node_type) << colors::BOLD;
    }
    
    const char* type_name = get_node_type_name(node->node_type);
    out << type_name;
    
    if (config.use_colors && !config.simple_mode) {
        out << colors::RESET;
    }
    
    // Print all available node information
    bool has_info = false;
    
    // Primary text
    if (!node->primary_text.empty()) {
        has_info = true;
        if (config.use_colors && !config.simple_mode) {
            out << colors::DIM << " {" << colors::RESET;
            
            // Use appropriate color for the value
            if (node->node_type == db25::ast::NodeType::StringLiteral) {
                out << colors::LITERAL_COLOR << "'" << node->primary_text << "'";
            } else if (node->node_type == db25::ast::NodeType::BinaryExpr ||
                      node->node_type == db25::ast::NodeType::UnaryExpr) {
                out << colors::OPERATOR_COLOR << "op=" << node->primary_text;
            } else if (node->node_type == db25::ast::NodeType::JoinClause) {
                out << colors::JOIN_COLOR << "type=" << node->primary_text;
            } else {
                out << colors::INFO << "val=" << node->primary_text;
            }
        } else {
            out << " {";
            if (node->node_type == db25::ast::NodeType::StringLiteral) {
                out << "'" << node->primary_text << "'";
            } else if (node->node_type == db25::ast::NodeType::BinaryExpr ||
                      node->node_type == db25::ast::NodeType::UnaryExpr) {
                out << "op=" << node->primary_text;
            } else if (node->node_type == db25::ast::NodeType::JoinClause) {
                out << "type=" << node->primary_text;
            } else {
                out << "val=" << node->primary_text;
            }
        }
    }
    
    // Schema name (or alias for tables)
    if (!node->schema_name.empty()) {
        if (!has_info) {
            out << " {";
            has_info = true;
        } else {
            out << ", ";
        }
        
        // Check if this is an alias using the HasAlias flag in semantic_flags
        bool is_alias = (node->semantic_flags & static_cast<uint16_t>(db25::ast::NodeFlags::HasAlias)) != 0;
        
        if (config.use_colors && !config.simple_mode) {
            out << colors::DIM << (is_alias ? "alias=" : "schema=") 
                << colors::WARNING << node->schema_name << colors::RESET;
        } else {
            out << (is_alias ? "alias=" : "schema=") << node->schema_name;
        }
    }
    
    // Catalog name
    if (!node->catalog_name.empty()) {
        if (!has_info) {
            out << " {";
            has_info = true;
        } else {
            out << ", ";
        }
        if (config.use_colors && !config.simple_mode) {
            out << colors::DIM << "catalog=" << colors::WARNING << node->catalog_name << colors::RESET;
        } else {
            out << "catalog=" << node->catalog_name;
        }
    }
    
    // Node ID (if in debug mode)
    if (config.show_stats && node->node_id > 0) {
        if (!has_info) {
            out << " {";
            has_info = true;
        } else {
            out << ", ";
        }
        if (config.use_colors && !config.simple_mode) {
            out << colors::DIM << "id=" << node->node_id << colors::RESET;
        } else {
            out << "id=" << node->node_id;
        }
    }
    
    // Flags (always show for debugging)
    if (true || node->flags != db25::ast::NodeFlags::None) {
        if (!has_info) {
            out << " {";
            has_info = true;
        } else {
            out << ", ";
        }
        if (config.use_colors && !config.simple_mode) {
            out << colors::DIM << "flags=";
            out << colors::WARNING;
        } else {
            out << "flags=";
        }
        
        uint8_t flags = static_cast<uint8_t>(node->flags);
        if (flags == 0) {
            out << "NONE";
        } else {
            bool first_flag = true;
            if (flags & static_cast<uint8_t>(db25::ast::NodeFlags::Distinct)) {
                out << (first_flag ? "" : "|") << "DISTINCT";
                first_flag = false;
            }
            if (flags & static_cast<uint8_t>(db25::ast::NodeFlags::All)) {
                out << (first_flag ? "" : "|") << "ALL";
                first_flag = false;
            }
            if (flags & static_cast<uint8_t>(db25::ast::NodeFlags::IsRecursive)) {
                out << (first_flag ? "" : "|") << "RECURSIVE";
                first_flag = false;
            }
            if (flags & static_cast<uint8_t>(db25::ast::NodeFlags::HasAlias)) {
                out << (first_flag ? "" : "|") << "ALIAS";
                first_flag = false;
            }
            if (flags & static_cast<uint8_t>(db25::ast::NodeFlags::IsSubquery)) {
                out << (first_flag ? "" : "|") << "SUBQUERY";
                first_flag = false;
            }
        }
        
        if (config.use_colors && !config.simple_mode) {
            out << colors::RESET;
        }
    }
    
    // Data type (for expressions)
    if (node->data_type != db25::ast::DataType::Unknown) {
        if (!has_info) {
            out << " {";
            has_info = true;
        } else {
            out << ", ";
        }
        if (config.use_colors && !config.simple_mode) {
            out << colors::DIM << "dtype=" << colors::KEYWORD_COLOR 
                << static_cast<int>(node->data_type) << colors::RESET;
        } else {
            out << "dtype=" << static_cast<int>(node->data_type);
        }
    }
    
    // Child count
    if (config.show_stats && node->child_count > 0) {
        if (!has_info) {
            out << " {";
            has_info = true;
        } else {
            out << ", ";
        }
        if (config.use_colors && !config.simple_mode) {
            out << colors::DIM << "children=" << node->child_count << colors::RESET;
        } else {
            out << "children=" << node->child_count;
        }
    }
    
    // Source position (if available)
    if (config.show_stats && (node->source_start > 0 || node->source_end > 0)) {
        if (!has_info) {
            out << " {";
            has_info = true;
        } else {
            out << ", ";
        }
        if (config.use_colors && !config.simple_mode) {
            out << colors::DIM << "pos=" << node->source_start << "-" << node->source_end << colors::RESET;
        } else {
            out << "pos=" << node->source_start << "-" << node->source_end;
        }
    }
    
    if (has_info) {
        if (config.use_colors && !config.simple_mode) {
            out << colors::DIM << "}" << colors::RESET;
        } else {
            out << "}";
        }
    }
    
    out << "\n";
    
    // Process children with improved indentation
    db25::ast::ASTNode* child = node->first_child;
    std::string new_prefix = prefix;
    
    // Add proper indentation for children
    if (!prefix.empty()) {
        if (config.use_colors && !config.simple_mode) {
            new_prefix += colors::DIM;
        }
        new_prefix += is_last ? "    " : (std::string(config.box_style.VERTICAL) + "   ");
        if (config.use_colors && !config.simple_mode) {
            new_prefix += colors::RESET;
        }
    } else {
        // Root level - start indentation
        new_prefix = "  ";
    }
    
    // Count children for better display
    int child_count = 0;
    db25::ast::ASTNode* temp = node->first_child;
    while (temp) {
        child_count++;
        temp = temp->next_sibling;
    }
    
    int child_index = 0;
    while (child) {
        child_index++;
        bool child_is_last = (child->next_sibling == nullptr);
        
        // Add child numbering for clarity in complex trees
        if (config.show_stats && child_count > 5) {
            if (config.use_colors && !config.simple_mode) {
                out << new_prefix << colors::DIM << "[" << child_index << "/" << child_count << "]" << colors::RESET << "\n";
            }
        }
        
        print_ast_node(child, new_prefix, child_is_last, config, out);
        child = child->next_sibling;
    }
}

// Load queries from .sqls file (separated by ---)
std::vector<std::string> load_queries_from_file(const std::string& filename) {
    std::vector<std::string> queries;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << colors::ERROR << "Error: Cannot open file: " << filename << colors::RESET << std::endl;
        return {};
    }
    
    std::string current_query;
    std::string line;
    
    while (std::getline(file, line)) {
        // Skip comment lines starting with #
        if (!line.empty() && line[0] == '#') {
            continue;
        }
        
        // Check for separator
        if (line == "---") {
            if (!current_query.empty()) {
                // Trim trailing whitespace
                while (!current_query.empty() && std::isspace(current_query.back())) {
                    current_query.pop_back();
                }
                if (!current_query.empty()) {
                    queries.push_back(current_query);
                }
                current_query.clear();
            }
        } else {
            // Add line to current query
            if (!current_query.empty()) {
                current_query += " ";
            }
            current_query += line;
        }
    }
    
    // Add last query if exists
    if (!current_query.empty()) {
        while (!current_query.empty() && std::isspace(current_query.back())) {
            current_query.pop_back();
        }
        if (!current_query.empty()) {
            queries.push_back(current_query);
        }
    }
    
    return queries;
}

// Print progress bar
void print_progress(int current, int total, const DisplayConfig& config) {
    if (!config.show_progress || !config.use_colors) return;
    
    const int bar_width = 50;
    float progress = static_cast<float>(current) / total;
    int filled = static_cast<int>(bar_width * progress);
    
    std::cout << "\r" << colors::INFO << "[";
    for (int i = 0; i < bar_width; ++i) {
        if (i < filled) {
            std::cout << "#";
        } else {
            std::cout << ".";
        }
    }
    std::cout << "] " << colors::BOLD << std::setw(3) << (int)(progress * 100) << "%" 
              << colors::RESET << " (" << current << "/" << total << ")";
    std::cout.flush();
}

// Process a single query
bool process_query(const std::string& sql, 
                  int index, 
                  const DisplayConfig& config,
                  ParseStats& stats,
                  std::ostream& out = std::cout) {
    
    // Show query header
    if (index > 0) {
        if (config.use_colors && !config.simple_mode) {
            out << "\n" << colors::BG_CYAN << colors::BOLD 
                << "=== Query #" << index << " ===" 
                << colors::RESET << "\n";
        } else {
            out << "\n=== Query #" << index << " ===\n";
        }
    }
    
    // Show SQL
    if (!config.compact_output) {
        if (config.use_colors && !config.simple_mode) {
            out << colors::DIM << "SQL: " << colors::RESET 
                << colors::ITALIC << sql << colors::RESET << "\n\n";
        } else {
            out << "SQL: " << sql << "\n\n";
        }
    }
    
    // Parse the query
    db25::parser::Parser parser;
    auto start = std::chrono::high_resolution_clock::now();
    auto result = parser.parse(sql);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto parse_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    stats.total_parse_time += parse_time;
    
    if (result) {
        stats.successful++;
        
        auto* root = result.value();
        size_t node_count = count_nodes(root);
        size_t depth = calculate_depth(root);
        
        stats.total_nodes += node_count;
        stats.max_depth = std::max(stats.max_depth, depth);
        
        // Show statistics if requested
        if (config.show_stats) {
            if (config.use_colors && !config.simple_mode) {
                out << colors::INFO << "Parse Statistics:" << colors::RESET << "\n";
                out << "  Parse Time:     " << colors::BOLD << std::setw(6) 
                    << parse_time.count() << " us" << colors::RESET << "\n";
                out << "  Node Count:     " << colors::BOLD << std::setw(6) 
                    << node_count << " nodes" << colors::RESET << "\n";
                out << "  Tree Depth:     " << colors::BOLD << std::setw(6) 
                    << depth << " levels" << colors::RESET << "\n\n";
            } else {
                out << "Parse Statistics:\n";
                out << "  Parse Time:     " << std::setw(6) << parse_time.count() << " us\n";
                out << "  Node Count:     " << std::setw(6) << node_count << " nodes\n";
                out << "  Tree Depth:     " << std::setw(6) << depth << " levels\n\n";
            }
        }
        
        // Print AST
        if (config.use_colors && !config.simple_mode) {
            out << colors::BOLD << "Abstract Syntax Tree:" << colors::RESET << "\n";
            out << colors::DIM << std::string(50, '-') << colors::RESET << "\n";
        } else {
            out << "Abstract Syntax Tree:\n";
            out << std::string(50, '-') << "\n";
        }
        
        print_ast_node(root, "", true, config, out);
        
        if (config.use_colors && !config.simple_mode) {
            out << colors::DIM << std::string(50, '-') << colors::RESET << "\n";
        } else {
            out << std::string(50, '-') << "\n";
        }
        
        return true;
    } else {
        stats.failed++;
        stats.failed_indices.push_back(index);
        
        auto error = result.error();
        if (config.use_colors && !config.simple_mode) {
            out << colors::ERROR << "[ERROR]" << colors::RESET << " Parse Error\n";
            out << "  Line " << error.line << ", Column " << error.column 
                << ": " << error.message << "\n";
        } else {
            out << "[ERROR] Parse Error\n";
            out << "  Line " << error.line << ", Column " << error.column 
                << ": " << error.message << "\n";
        }
        
        return false;
    }
}

// Print summary statistics
void print_summary(const ParseStats& stats, const DisplayConfig& config, std::ostream& out = std::cout) {
    if (config.use_colors && !config.simple_mode) {
        out << "\n" << colors::BG_BLUE << colors::BOLD 
            << "                    SUMMARY                    " 
            << colors::RESET << "\n\n";
        
        out << "Total queries:      " << colors::BOLD << std::setw(6) 
            << stats.total_queries << colors::RESET << "\n";
        out << "Successful:         " << colors::SUCCESS << std::setw(6) 
            << stats.successful << colors::RESET << "\n";
        out << "Failed:             " << colors::ERROR << std::setw(6) 
            << stats.failed << colors::RESET << "\n";
        
        if (!stats.failed_indices.empty()) {
            out << "Failed queries:     " << colors::WARNING;
            for (size_t i = 0; i < stats.failed_indices.size(); ++i) {
                if (i > 0) out << ", ";
                out << stats.failed_indices[i];
            }
            out << colors::RESET << "\n";
        }
        
        if (stats.successful > 0) {
            out << "\n" << colors::INFO << "Performance Metrics:" << colors::RESET << "\n";
            out << "  Total parse time:   " << colors::BOLD 
                << std::setw(8) << stats.total_parse_time.count() << " us" << colors::RESET << "\n";
            out << "  Average parse time: " << colors::BOLD 
                << std::setw(8) << (stats.total_parse_time.count() / stats.successful) 
                << " us" << colors::RESET << "\n";
            out << "  Total nodes:        " << colors::BOLD 
                << std::setw(8) << stats.total_nodes << colors::RESET << "\n";
            out << "  Max tree depth:     " << colors::BOLD 
                << std::setw(8) << stats.max_depth << colors::RESET << "\n";
        }
        
        if (stats.failed == 0) {
            out << "\n" << colors::SUCCESS << "✓ All queries parsed successfully!" 
                << colors::RESET << "\n";
        }
    } else {
        out << "\n" << std::string(60, '=') << "\n";
        out << "                          SUMMARY\n";
        out << std::string(60, '=') << "\n";
        
        out << "Total queries:     " << std::setw(6) << stats.total_queries << "\n";
        out << "Successful:        " << std::setw(6) << stats.successful << "\n";
        out << "Failed:            " << std::setw(6) << stats.failed << "\n";
        
        if (!stats.failed_indices.empty()) {
            out << "Failed queries:    ";
            for (size_t i = 0; i < stats.failed_indices.size(); ++i) {
                if (i > 0) out << " ";
                out << stats.failed_indices[i];
            }
            out << "\n";
        }
        
        if (stats.failed == 0) {
            out << "\n✓ All queries parsed successfully!\n";
        }
        
        out << std::string(60, '=') << "\n";
    }
}

// Print beautiful header
void print_header(const DisplayConfig& config, std::ostream& out = std::cout) {
    if (config.use_colors && !config.simple_mode) {
        out << "\n" << colors::BG_PURPLE << colors::BOLD 
            << "+===========================================================+" 
            << colors::RESET << "\n";
        out << colors::BG_PURPLE << colors::BOLD 
            << "|       DB25 SQL Parser - Enhanced AST Viewer v2.0         |" 
            << colors::RESET << "\n";
        out << colors::BG_PURPLE << colors::BOLD 
            << "+===========================================================+" 
            << colors::RESET << "\n\n";
    } else {
        out << "\n";
        out << "==============================================================\n";
        out << "          DB25 SQL Parser - Enhanced AST Viewer v2.0\n";
        out << "==============================================================\n\n";
    }
}

// Print usage information
void print_usage(const char* program_name) {
    DisplayConfig config;
    print_header(config);
    
    std::cout << colors::BOLD << "Usage:" << colors::RESET << "\n\n";
    
    std::cout << "  " << colors::INFO << "# From pipe:" << colors::RESET << "\n";
    std::cout << "  echo \"SELECT * FROM users\" | " << program_name << "\n\n";
    
    std::cout << "  " << colors::INFO << "# Single query from file:" << colors::RESET << "\n";
    std::cout << "  " << program_name << " --file queries.sqls --index 3\n\n";
    
    std::cout << "  " << colors::INFO << "# All queries from file:" << colors::RESET << "\n";
    std::cout << "  " << program_name << " --file queries.sqls --all\n\n";
    
    std::cout << "  " << colors::INFO << "# With output file:" << colors::RESET << "\n";
    std::cout << "  " << program_name << " --file queries.sqls --all --output results.txt\n\n";
    
    std::cout << colors::BOLD << "Options:" << colors::RESET << "\n";
    std::cout << "  --file <path>     Read SQL from .sqls file\n";
    std::cout << "  --index <n>       Parse query at index n (1-based)\n";
    std::cout << "  --all             Parse all queries in file\n";
    std::cout << "  --output <path>   Write output to file\n";
    std::cout << "  --stats           Show detailed statistics\n";
    std::cout << "  --compact         Compact output (no SQL echo)\n";
    std::cout << "  --no-progress     Hide progress bar\n";
    std::cout << "  --no-color        Disable colors\n";
    std::cout << "  --simple          Simple ASCII output\n";
    std::cout << "  --help            Show this help message\n\n";
    
    std::cout << colors::BOLD << "Color Legend:" << colors::RESET << "\n";
    std::cout << "  " << colors::STMT_COLOR << "###" << colors::RESET 
              << " Statements   " << colors::CLAUSE_COLOR << "###" << colors::RESET 
              << " Clauses   " << colors::EXPR_COLOR << "###" << colors::RESET 
              << " Expressions\n";
    std::cout << "  " << colors::REF_COLOR << "###" << colors::RESET 
              << " References   " << colors::LITERAL_COLOR << "###" << colors::RESET 
              << " Literals  " << colors::FUNC_COLOR << "###" << colors::RESET 
              << " Functions\n";
    std::cout << "  " << colors::JOIN_COLOR << "###" << colors::RESET 
              << " Joins        " << colors::OPERATOR_COLOR << "###" << colors::RESET 
              << " Operators " << colors::STAR_COLOR << "###" << colors::RESET 
              << " Wildcards\n\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    DisplayConfig config;
    std::string input_file;
    std::string output_file;
    int query_index = -1;
    bool parse_all = false;
    bool from_stdin = false;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--file" && i + 1 < argc) {
            input_file = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg == "--index" && i + 1 < argc) {
            query_index = std::stoi(argv[++i]);
        } else if (arg == "--all") {
            parse_all = true;
        } else if (arg == "--stats") {
            config.show_stats = true;
        } else if (arg == "--compact") {
            config.compact_output = true;
        } else if (arg == "--no-progress") {
            config.show_progress = false;
        } else if (arg == "--no-color") {
            config.use_colors = false;
            config.simple_mode = true;
        } else if (arg == "--simple") {
            config.simple_mode = true;
            config.box_style = box::ascii;
        } else if (arg == "--heavy") {
            config.box_style = box::heavy;
        }
    }
    
    // Determine input source
    if (input_file.empty() && !isatty(STDIN_FILENO)) {
        from_stdin = true;
    }
    
    // Setup output stream
    std::ofstream out_file;
    std::ostream* out = &std::cout;
    if (!output_file.empty()) {
        out_file.open(output_file);
        if (!out_file.is_open()) {
            std::cerr << colors::ERROR << "Error: Cannot open output file: " 
                      << output_file << colors::RESET << "\n";
            return 1;
        }
        out = &out_file;
        // Disable colors for file output
        config.use_colors = false;
        config.simple_mode = true;
    }
    
    ParseStats stats;
    
    // Main processing (no exceptions needed)
    {
        if (from_stdin) {
            // Read from stdin
            print_header(config, *out);
            
            std::string sql;
            std::string line;
            while (std::getline(std::cin, line)) {
                if (!sql.empty()) sql += " ";
                sql += line;
            }
            
            if (!sql.empty()) {
                stats.total_queries = 1;
                process_query(sql, 0, config, stats, *out);
            }
            
        } else if (!input_file.empty()) {
            // Load queries from file
            auto queries = load_queries_from_file(input_file);
            
            if (queries.empty()) {
                std::cerr << colors::ERROR << "Error: No queries found in file: " 
                          << input_file << colors::RESET << "\n";
                return 1;
            }
            
            print_header(config, *out);
            
            if (config.use_colors && !config.simple_mode) {
                *out << colors::DIM << "Loaded " << colors::BOLD << queries.size() 
                     << colors::RESET << colors::DIM << " queries from " 
                     << input_file << colors::RESET << "\n";
            } else {
                *out << "Loaded " << queries.size() << " queries from " << input_file << "\n";
            }
            
            stats.total_queries = queries.size();
            
            if (query_index > 0) {
                // Parse single query
                if (query_index > static_cast<int>(queries.size())) {
                    std::cerr << colors::ERROR << "Error: Query index " << query_index 
                              << " out of range (1-" << queries.size() << ")" 
                              << colors::RESET << "\n";
                    return 1;
                }
                
                process_query(queries[query_index - 1], query_index, config, stats, *out);
                
            } else if (parse_all) {
                // Parse all queries
                for (size_t i = 0; i < queries.size(); ++i) {
                    if (config.show_progress && !output_file.empty()) {
                        print_progress(i + 1, queries.size(), config);
                    }
                    
                    process_query(queries[i], i + 1, config, stats, *out);
                    
                    // Add separator between queries
                    if (i < queries.size() - 1) {
                        if (config.use_colors && !config.simple_mode) {
                            *out << "\n" << colors::DIM 
                                 << std::string(60, '=') 
                                 << colors::RESET << "\n";
                        } else {
                            *out << "\n" << std::string(60, '-') << "\n";
                        }
                    }
                }
                
                if (config.show_progress && !output_file.empty()) {
                    std::cout << "\n";  // Clear progress bar
                }
                
                // Print summary
                print_summary(stats, config, *out);
                
            } else {
                std::cerr << colors::ERROR 
                          << "Error: Must specify --index <n> or --all for file input" 
                          << colors::RESET << "\n";
                return 1;
            }
            
        } else {
            print_usage(argv[0]);
            return 1;
        }
        
    } // End of main processing
    
    // Success message for file output
    if (!output_file.empty()) {
        std::cout << colors::SUCCESS << "✓ Output saved to: " << output_file 
                  << colors::RESET << "\n";
    }
    
    return (stats.failed > 0) ? 1 : 0;
}