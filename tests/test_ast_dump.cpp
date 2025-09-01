// Comprehensive AST Dump Test - Combines all visual dump functionality
#include "db25/parser/parser.hpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <cstring>

// ANSI color codes for terminal output
namespace colors {
    const char* RESET = "\033[0m";
    const char* BOLD = "\033[1m";
    const char* DIM = "\033[2m";
    const char* ITALIC = "\033[3m";
    
    // Statement colors
    const char* STMT_COLOR = "\033[38;5;196m";      // Red
    const char* CLAUSE_COLOR = "\033[38;5;214m";    // Orange
    const char* EXPR_COLOR = "\033[38;5;226m";      // Yellow
    const char* LITERAL_COLOR = "\033[38;5;46m";    // Green
    const char* REF_COLOR = "\033[38;5;51m";        // Cyan
    const char* JOIN_COLOR = "\033[38;5;201m";      // Magenta
    const char* FUNC_COLOR = "\033[38;5;99m";       // Purple
    const char* WINDOW_COLOR = "\033[38;5;165m";    // Pink
    const char* CTE_COLOR = "\033[38;5;208m";       // Dark Orange
    const char* UNKNOWN_COLOR = "\033[38;5;252m";   // Gray
}

// Get complete node type name
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
        case NodeType::ValuesStmt: return "VALUES STMT";
        case NodeType::TransactionStmt: return "TRANSACTION";
        case NodeType::SetStmt: return "SET";
        case NodeType::ExplainStmt: return "EXPLAIN";
        case NodeType::VacuumStmt: return "VACUUM";
        case NodeType::AnalyzeStmt: return "ANALYZE";
        case NodeType::AttachStmt: return "ATTACH";
        case NodeType::DetachStmt: return "DETACH";
        case NodeType::ReindexStmt: return "REINDEX";
        case NodeType::PragmaStmt: return "PRAGMA";
        
        // Clauses
        case NodeType::SelectList: return "SELECT LIST";
        case NodeType::FromClause: return "FROM";
        case NodeType::WhereClause: return "WHERE";
        case NodeType::GroupByClause: return "GROUP BY";
        case NodeType::HavingClause: return "HAVING";
        case NodeType::OrderByClause: return "ORDER BY";
        case NodeType::LimitClause: return "LIMIT";
        case NodeType::ValuesClause: return "VALUES";
        case NodeType::SetClause: return "SET";
        case NodeType::ReturningClause: return "RETURNING";
        case NodeType::OnConflictClause: return "ON CONFLICT";
        case NodeType::UsingClause: return "USING";
        case NodeType::PartitionByClause: return "PARTITION BY";
        
        // Expressions
        case NodeType::BinaryExpr: return "BINARY OP";
        case NodeType::UnaryExpr: return "UNARY OP";
        case NodeType::CaseExpr: return "CASE";
        case NodeType::CastExpr: return "CAST";
        case NodeType::RowExpr: return "ROW";
        case NodeType::InExpr: return "IN";
        case NodeType::ExistsExpr: return "EXISTS";
        case NodeType::BetweenExpr: return "BETWEEN";
        case NodeType::SubqueryExpr: return "SUBQUERY";
        
        // References
        case NodeType::TableRef: return "TABLE";
        case NodeType::ColumnRef: return "COLUMN";
        case NodeType::Identifier: return "IDENT";
        case NodeType::AliasRef: return "ALIAS";
        case NodeType::SchemaRef: return "SCHEMA";
        case NodeType::Star: return "*";
        
        // Joins
        case NodeType::JoinClause: return "JOIN";
        case NodeType::InnerJoin: return "INNER JOIN";
        case NodeType::LeftJoin: return "LEFT JOIN";
        case NodeType::RightJoin: return "RIGHT JOIN";
        case NodeType::FullJoin: return "FULL JOIN";
        case NodeType::CrossJoin: return "CROSS JOIN";
        case NodeType::LateralJoin: return "LATERAL JOIN";
        
        // Functions
        case NodeType::FunctionCall: return "FUNCTION";
        case NodeType::WindowFunction: return "WINDOW FUNC";
        case NodeType::WindowSpec: return "WINDOW";
        case NodeType::FrameClause: return "FRAME";
        
        // CTEs
        case NodeType::WithClause: return "WITH";
        case NodeType::CTEClause: return "CTE CLAUSE";
        case NodeType::CTEDefinition: return "CTE";
        case NodeType::Subquery: return "SUBQUERY";
        
        // Literals
        case NodeType::IntegerLiteral: return "INT";
        case NodeType::FloatLiteral: return "FLOAT";
        case NodeType::StringLiteral: return "STRING";
        case NodeType::BooleanLiteral: return "BOOLEAN";
        case NodeType::NullLiteral: return "NULL";
        case NodeType::DateTimeLiteral: return "DATETIME";
        
        // Data types
        case NodeType::DataTypeNode: return "DATA TYPE";
        
        // DDL specific
        case NodeType::ColumnDefinition: return "COLUMN DEF";
        case NodeType::ColumnConstraint: return "COL CONSTRAINT";
        case NodeType::TableConstraint: return "TABLE CONSTRAINT";
        case NodeType::PrimaryKeyConstraint: return "PRIMARY KEY";
        case NodeType::ForeignKeyConstraint: return "FOREIGN KEY";
        case NodeType::UniqueConstraint: return "UNIQUE";
        case NodeType::CheckConstraint: return "CHECK";
        case NodeType::DefaultClause: return "DEFAULT";
        case NodeType::AlterTableAction: return "ALTER ACTION";
        
        // Set operations
        case NodeType::UnionStmt: return "UNION";
        case NodeType::IntersectStmt: return "INTERSECT";
        case NodeType::ExceptStmt: return "EXCEPT";
        
        // Special  
        case NodeType::GroupingElement: return "GROUPING";
        case NodeType::ColumnList: return "COLUMN LIST";
        case NodeType::WindowFrame: return "WINDOW FRAME";
        case NodeType::FrameBound: return "FRAME BOUND";
        case NodeType::IntervalLiteral: return "INTERVAL";
        case NodeType::ArrayConstructor: return "ARRAY";
        case NodeType::RowConstructor: return "ROW CONSTRUCTOR";
        case NodeType::JsonLiteral: return "JSON";
        case NodeType::CollateClause: return "COLLATE";
        case NodeType::SearchClause: return "SEARCH";
        case NodeType::CycleClause: return "CYCLE";
        case NodeType::IsolationLevel: return "ISOLATION";
        case NodeType::TransactionMode: return "TRANS MODE";
        case NodeType::Parameter: return "PARAMETER";
        case NodeType::Comment: return "COMMENT";
        
        // Additional expression types (not already mapped)
        case NodeType::LikeExpr: return "LIKE";
        case NodeType::IsNullExpr: return "IS NULL";
        case NodeType::WindowExpr: return "WINDOW EXPR";
        
        // DDL statement types (not already mapped)
        case NodeType::CreateSchemaStmt: return "CREATE SCHEMA";
        case NodeType::CreateFunctionStmt: return "CREATE FUNCTION";
        case NodeType::CreateProcedureStmt: return "CREATE PROCEDURE";
        case NodeType::AlterSchemaStmt: return "ALTER SCHEMA";
        case NodeType::AlterIndexStmt: return "ALTER INDEX";
        case NodeType::AlterViewStmt: return "ALTER VIEW";
        case NodeType::CreateTriggerStmt: return "CREATE TRIGGER";
        case NodeType::BeginStmt: return "BEGIN";
        case NodeType::CommitStmt: return "COMMIT";
        case NodeType::RollbackStmt: return "ROLLBACK";
        case NodeType::SavepointStmt: return "SAVEPOINT";
        case NodeType::ReleaseSavepointStmt: return "RELEASE SAVEPOINT";
        
        // Reference types are already mapped above
        
        // DDL components
        case NodeType::IndexColumn: return "INDEX COLUMN";
        
        default: {
            static char buf[32];
            snprintf(buf, sizeof(buf), "NODE_%d", static_cast<int>(type));
            return buf;
        }
    }
}

// Get color for node type
const char* get_node_color(db25::ast::NodeType type) {
    using namespace db25::ast;
    
    // Statements
    if (type >= NodeType::SelectStmt && type <= NodeType::PragmaStmt) {
        return colors::STMT_COLOR;
    }
    
    // Clauses
    if (type >= NodeType::SelectList && type <= NodeType::PartitionByClause) {
        return colors::CLAUSE_COLOR;
    }
    
    // Expressions
    if (type >= NodeType::BinaryExpr && type <= NodeType::WindowExpr) {
        return colors::EXPR_COLOR;
    }
    
    // References
    if (type >= NodeType::TableRef && type <= NodeType::Star) {
        return colors::REF_COLOR;
    }
    
    // Joins
    if (type >= NodeType::InnerJoin && type <= NodeType::LateralJoin) {
        return colors::JOIN_COLOR;
    }
    
    // Functions
    if (type >= NodeType::FunctionCall && type <= NodeType::FrameClause) {
        return colors::FUNC_COLOR;
    }
    
    // CTEs
    if (type == NodeType::WithClause || type == NodeType::CTEClause || type == NodeType::CTEDefinition) {
        return colors::CTE_COLOR;
    }
    
    // Literals
    if (type >= NodeType::IntegerLiteral && type <= NodeType::DateTimeLiteral) {
        return colors::LITERAL_COLOR;
    }
    
    // Window functions
    if (type == NodeType::WindowFunction || type == NodeType::WindowSpec) {
        return colors::WINDOW_COLOR;
    }
    
    return colors::UNKNOWN_COLOR;
}

// Count total nodes in AST
int count_nodes(const db25::ast::ASTNode* node) {
    if (!node) return 0;
    int count = 1;
    db25::ast::ASTNode* child = node->first_child;
    while (child) {
        count += count_nodes(child);
        child = child->next_sibling;
    }
    return count;
}

// Dump tree to stream with colors (for terminal and file)
void dump_tree(std::ostream& out, const db25::ast::ASTNode* node, 
               const std::string& prefix = "", bool is_last = true, 
               int depth = 0, bool use_colors = true) {
    if (!node) return;
    
    // Current node branch
    out << prefix;
    if (depth > 0) {
        out << (is_last ? "└── " : "├── ");
    }
    
    // Node info with optional color
    const char* color = use_colors ? get_node_color(node->node_type) : "";
    const char* bold = use_colors ? colors::BOLD : "";
    const char* reset = use_colors ? colors::RESET : "";
    const char* dim = use_colors ? colors::DIM : "";
    const char* italic = use_colors ? colors::ITALIC : "";
    
    out << color << bold << get_node_type_name(node->node_type) << reset;
    
    // Show node value if present
    if (node->primary_text.data() && !node->primary_text.empty()) {
        std::string text(node->primary_text.data(), node->primary_text.size());
        out << ": " << italic << text << reset;
    }
    
    // Show alias if present
    if (node->schema_name.data() && !node->schema_name.empty()) {
        std::string alias(node->schema_name.data(), node->schema_name.size());
        out << " [" << alias << "]";
    }
    
    // Node metadata
    out << dim << " [#" << node->node_id;
    if (node->child_count > 0) {
        out << ", " << node->child_count << " children";
    }
    if (static_cast<uint32_t>(node->flags)) {
        out << ", flags=0x" << std::hex << static_cast<uint32_t>(node->flags) << std::dec;
    }
    if (node->semantic_flags) {
        out << ", sem=0x" << std::hex << static_cast<uint32_t>(node->semantic_flags) << std::dec;
    }
    out << "]" << reset << "\n";
    
    // Update prefix for children
    std::string child_prefix = prefix;
    if (depth > 0) {
        child_prefix += (is_last ? "    " : "│   ");
    }
    
    // Dump children
    db25::ast::ASTNode* child = node->first_child;
    while (child) {
        bool is_last_child = (child->next_sibling == nullptr);
        dump_tree(out, child, child_prefix, is_last_child, depth + 1, use_colors);
        child = child->next_sibling;
    }
}

struct TestQuery {
    const char* name;
    const char* sql;
    const char* category;
};

int main(int argc, char* argv[]) {
    // Parse command line arguments
    bool save_to_file = false;
    bool verbose = false;
    std::string output_dir = "/var/folders/d0/c79v8ks53p35mm3b6d4r5yz80000gn/T/db25_ast_dumps";
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--save") == 0 || strcmp(argv[i], "-s") == 0) {
            save_to_file = true;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--output") == 0 || strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                output_dir = argv[++i];
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  -s, --save        Save AST dumps to files\n";
            std::cout << "  -v, --verbose     Show verbose output\n";
            std::cout << "  -o, --output DIR  Output directory (default: /tmp/db25_ast_dumps)\n";
            std::cout << "  -h, --help        Show this help message\n";
            return 0;
        }
    }
    
    // Header
    std::cout << colors::BOLD << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║         DB25 Parser - Comprehensive AST Dump Test           ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << colors::RESET << "\n\n";
    
    if (save_to_file) {
        std::filesystem::create_directories(output_dir);
        std::cout << colors::BOLD << "Output Directory: " << colors::RESET 
                  << "\"" << output_dir << "\"\n\n";
    }
    
    // Test queries organized by category
    std::vector<TestQuery> queries = {
        // Basic DQL
        {"simple_select", "SELECT * FROM users", "DQL"},
        {"select_with_where", "SELECT id, name FROM users WHERE age > 18", "DQL"},
        {"complex_join", "SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id WHERE o.status = 'active'", "DQL"},
        
        // DML
        {"simple_insert", "INSERT INTO users (name, email) VALUES ('John', 'john@example.com')", "DML"},
        {"insert_select", "INSERT INTO archive SELECT * FROM orders WHERE date < '2023-01-01'", "DML"},
        {"update_set", "UPDATE users SET status = 'active' WHERE id = 123", "DML"},
        {"delete_where", "DELETE FROM sessions WHERE expired < NOW()", "DML"},
        
        // DDL
        {"create_table", "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(100) NOT NULL, email VARCHAR(255) UNIQUE)", "DDL"},
        {"alter_table", "ALTER TABLE users ADD COLUMN age INT DEFAULT 0", "DDL"},
        {"drop_table", "DROP TABLE IF EXISTS temp_data CASCADE", "DDL"},
        
        // Advanced
        {"window_function", "SELECT name, RANK() OVER (PARTITION BY dept ORDER BY salary DESC) FROM employees", "Advanced"},
        {"recursive_cte", "WITH RECURSIVE nums(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM nums WHERE n < 10) SELECT * FROM nums", "Advanced"},
        {"grouping_sets", "SELECT dept, job, COUNT(*) FROM employees GROUP BY GROUPING SETS ((dept), (job), ())", "Advanced"},
        
        // Complex
        {"nested_subquery", "SELECT * FROM (SELECT * FROM (SELECT * FROM users WHERE active = true) t1 WHERE age > 21) t2", "Complex"},
        {"multiple_joins", "SELECT * FROM a JOIN b ON a.id = b.a_id LEFT JOIN c ON b.id = c.b_id RIGHT JOIN d ON c.id = d.c_id", "Complex"},
        {"case_expression", "SELECT CASE WHEN score >= 90 THEN 'A' WHEN score >= 80 THEN 'B' ELSE 'F' END as grade FROM students", "Complex"},
    };
    
    db25::parser::Parser parser;
    int successful = 0;
    int failed = 0;
    std::string current_category;
    
    for (const auto& query : queries) {
        // Print category header if changed
        if (current_category != query.category) {
            current_category = query.category;
            std::cout << "\n" << colors::BOLD << "═══ " << current_category 
                      << " Queries ═══" << colors::RESET << "\n";
        }
        
        std::cout << "\n" << colors::BOLD << "Query: " << colors::RESET << query.name;
        if (verbose) {
            std::cout << "\n" << colors::DIM << "SQL: " << query.sql << colors::RESET;
        }
        std::cout << "\n";
        
        // Parse the query
        auto start = std::chrono::high_resolution_clock::now();
        auto result = parser.parse(query.sql);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        if (result) {
            successful++;
            int node_count = count_nodes(result.value());
            
            std::cout << colors::DIM << "  ✓ Parsed in " << duration << "μs, " 
                      << node_count << " nodes" << colors::RESET << "\n";
            
            // Dump to console if verbose
            if (verbose) {
                std::cout << "────────────────────────────────────────\n";
                dump_tree(std::cout, result.value(), "", true, 0, true);
            }
            
            // Save to file if requested
            if (save_to_file) {
                std::string filename = output_dir + "/" + query.name + "_ast.txt";
                std::ofstream file(filename);
                
                file << "Query: " << query.name << "\n";
                file << "Category: " << query.category << "\n";
                file << "Parse Time: " << duration << " microseconds\n";
                file << "Node Count: " << node_count << "\n";
                file << "SQL:\n" << query.sql << "\n\n";
                file << "AST STRUCTURE:\n";
                file << "═══════════════════════════════════════════════════════════\n\n";
                
                dump_tree(file, result.value(), "", true, 0, false);  // No colors in file
                file.close();
                
                std::cout << colors::DIM << "  → Saved to: " << filename << colors::RESET << "\n";
            }
        } else {
            failed++;
            std::cout << colors::STMT_COLOR << "  ✗ ERROR: " << result.error().message 
                      << colors::RESET << "\n";
            
            if (save_to_file) {
                std::string filename = output_dir + "/" + query.name + "_error.txt";
                std::ofstream file(filename);
                file << "Query: " << query.name << "\n";
                file << "Category: " << query.category << "\n";
                file << "SQL:\n" << query.sql << "\n\n";
                file << "PARSE ERROR:\n";
                file << "Line " << result.error().line << ", Column " << result.error().column << "\n";
                file << "Message: " << result.error().message << "\n";
                file.close();
            }
        }
    }
    
    // Summary
    std::cout << "\n" << colors::BOLD;
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "SUMMARY: " << successful << " successful, " << failed << " failed\n";
    if (save_to_file) {
        std::cout << "Files saved to: " << output_dir << "\n";
    }
    std::cout << "═══════════════════════════════════════════════════════════" 
              << colors::RESET << "\n\n";
    
    return failed > 0 ? 1 : 0;
}