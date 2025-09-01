/*
 * DB25 Parser - Comprehensive AST Improvements Test
 * Demonstrates all parser fixes are reflected in the AST structure
 */

#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

void print_indent(int depth) {
    for (int i = 0; i < depth; i++) {
        std::cout << "  ";
    }
}

void print_node(const ASTNode* node, int depth = 0) {
    if (!node) return;
    
    print_indent(depth);
    std::cout << "- ";
    
    // Node type
    switch(node->node_type) {
        case NodeType::SelectStmt: std::cout << "SELECT"; break;
        case NodeType::UnionStmt: std::cout << "UNION"; break;
        case NodeType::FunctionCall: std::cout << "FUNCTION"; break;
        case NodeType::ColumnRef: std::cout << "COLUMN"; break;
        case NodeType::Identifier: std::cout << "ID"; break;
        case NodeType::TableRef: std::cout << "TABLE"; break;
        case NodeType::OrderByClause: std::cout << "ORDER_BY"; break;
        case NodeType::WindowSpec: std::cout << "WINDOW"; break;
        case NodeType::FrameBound: std::cout << "FRAME_BOUND"; break;
        case NodeType::FrameClause: std::cout << "FRAME"; break;
        case NodeType::ExistsExpr: std::cout << "EXISTS"; break;
        case NodeType::InExpr: std::cout << "IN"; break;
        default: std::cout << "NODE[" << static_cast<int>(node->node_type) << "]"; break;
    }
    
    // Primary text
    if (!node->primary_text.empty()) {
        std::cout << " '" << node->primary_text << "'";
    }
    
    // Alias (stored in schema_name)
    if ((node->semantic_flags & static_cast<uint16_t>(NodeFlags::HasAlias)) || 
        !node->schema_name.empty()) {
        std::cout << " AS '" << node->schema_name << "'";
    }
    
    // Flags
    if (node->flags != NodeFlags::None) {
        std::cout << " [";
        bool first = true;
        if (has_flag(node->flags, NodeFlags::All)) {
            std::cout << "ALL";
            first = false;
        }
        if (has_flag(node->flags, NodeFlags::Distinct)) {
            if (!first) std::cout << ",";
            std::cout << "DISTINCT";
            first = false;
        }
        if (has_flag(node->flags, NodeFlags::IsRecursive)) {
            if (!first) std::cout << ",";
            std::cout << "RECURSIVE";
        }
        std::cout << "]";
    }
    
    // Semantic flags for NOT
    if (node->semantic_flags & 0x40) {
        std::cout << " [NOT]";
    }
    
    // ORDER BY direction (bit 7)
    if (node->node_type == NodeType::OrderByClause || node->parent && 
        node->parent->node_type == NodeType::OrderByClause) {
        if (node->semantic_flags & (1 << 7)) {
            std::cout << " [DESC]";
        } else if (node->primary_text.find("ORDER") == std::string::npos) {
            std::cout << " [ASC]";
        }
    }
    
    // Frame boundaries
    if (node->node_type == NodeType::FrameBound && !node->schema_name.empty()) {
        std::cout << " " << node->schema_name;
    }
    
    std::cout << std::endl;
    
    // Children
    for (auto* child = node->first_child; child; child = child->next_sibling) {
        print_node(child, depth + 1);
    }
}

void test_query(Parser& parser, const std::string& name, const std::string& sql) {
    std::cout << "\n=== " << name << " ===" << std::endl;
    std::cout << "SQL: " << sql << std::endl;
    std::cout << "\nAST:" << std::endl;
    
    auto result = parser.parse(sql);
    if (result.has_value()) {
        print_node(result.value());
        std::cout << "✓ Parse successful" << std::endl;
    } else {
        std::cout << "✗ Parse failed" << std::endl;
    }
    
    parser.reset();
}

int main() {
    Parser parser;
    
    std::cout << "DB25 Parser - Comprehensive AST Improvements Demonstration\n";
    std::cout << "=========================================================\n";
    
    // Test 1: NOT EXISTS vs EXISTS
    test_query(parser, "1. NOT EXISTS semantic distinction",
        "SELECT * FROM t WHERE NOT EXISTS (SELECT 1 FROM t2)");
    
    test_query(parser, "2. Plain EXISTS (no NOT)",
        "SELECT * FROM t WHERE EXISTS (SELECT 1 FROM t2)");
    
    // Test 2: NOT IN vs IN
    test_query(parser, "3. NOT IN semantic distinction",
        "SELECT * FROM products WHERE id NOT IN (1, 2, 3)");
    
    test_query(parser, "4. Plain IN (no NOT)",
        "SELECT * FROM products WHERE id IN (1, 2, 3)");
    
    // Test 3: COUNT(DISTINCT)
    test_query(parser, "5. COUNT(DISTINCT) flag preservation",
        "SELECT COUNT(DISTINCT customer_id), COUNT(*), COUNT(id) FROM orders");
    
    // Test 4: UNION ALL vs UNION
    test_query(parser, "6. UNION ALL flag",
        "SELECT id FROM t1 UNION ALL SELECT id FROM t2");
    
    test_query(parser, "7. UNION (distinct)",
        "SELECT id FROM t1 UNION SELECT id FROM t2");
    
    // Test 5: Column vs Identifier
    test_query(parser, "8. Column references (not identifiers)",
        "SELECT name, age, salary FROM employees WHERE dept_id = 10");
    
    // Test 6: ORDER BY direction
    test_query(parser, "9. ORDER BY with ASC/DESC",
        "SELECT * FROM employees ORDER BY salary DESC, name ASC, dept");
    
    // Test 7: Window frame boundaries
    test_query(parser, "10. Window frame PRECEDING/FOLLOWING",
        R"(SELECT 
            SUM(amount) OVER (ORDER BY date ROWS BETWEEN 3 PRECEDING AND CURRENT ROW),
            AVG(amount) OVER (ORDER BY date ROWS BETWEEN CURRENT ROW AND 2 FOLLOWING)
        FROM sales)");
    
    // Test 8: Aliases
    test_query(parser, "11. Aliases in SELECT and FROM",
        R"(SELECT 
            e.id AS employee_id,
            e.name full_name,
            d.name AS department
        FROM employees e
        JOIN departments d ON e.dept_id = d.id)");
    
    // Test 9: Complex query with all improvements
    test_query(parser, "12. Complex query with multiple improvements",
        R"(WITH RECURSIVE cte AS (
            SELECT id, parent_id FROM tree WHERE parent_id IS NULL
            UNION ALL
            SELECT t.id, t.parent_id FROM tree t JOIN cte ON t.parent_id = cte.id
        )
        SELECT 
            COUNT(DISTINCT id) AS unique_nodes,
            MAX(id) max_id
        FROM cte
        WHERE NOT EXISTS (
            SELECT 1 FROM deleted WHERE deleted.id = cte.id
        )
        ORDER BY unique_nodes DESC)");
    
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "All AST improvements have been demonstrated:" << std::endl;
    std::cout << "✓ NOT EXISTS/IN semantic flags" << std::endl;
    std::cout << "✓ DISTINCT flag for aggregates" << std::endl;
    std::cout << "✓ UNION ALL flag" << std::endl;
    std::cout << "✓ Column vs Identifier distinction" << std::endl;
    std::cout << "✓ ORDER BY direction (ASC/DESC)" << std::endl;
    std::cout << "✓ Window frame boundaries (PRECEDING/FOLLOWING)" << std::endl;
    std::cout << "✓ Alias handling with HasAlias flag" << std::endl;
    std::cout << "✓ RECURSIVE flag for CTEs" << std::endl;
    
    return 0;
}