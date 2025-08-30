#include <iostream>
#include <string>
#include <vector>
#include "include/db25/parser/parser.hpp"
#include "include/db25/ast/ast_node.hpp"

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

void collect_identifiers(ASTNode* node, std::vector<ASTNode*>& identifiers) {
    if (!node) return;
    
    if (node->node_type == NodeType::Identifier) {
        identifiers.push_back(node);
    }
    
    // Traverse children
    ASTNode* child = node->first_child;
    while (child) {
        collect_identifiers(child, identifiers);
        child = child->next_sibling;
    }
}

std::string get_context_name(uint8_t context_hint) {
    switch (context_hint) {
        case 0: return "UNKNOWN";
        case 1: return "SELECT_LIST";
        case 2: return "FROM_CLAUSE";
        case 3: return "WHERE_CLAUSE";
        case 4: return "GROUP_BY_CLAUSE";
        case 5: return "HAVING_CLAUSE";
        case 6: return "ORDER_BY_CLAUSE";
        case 7: return "JOIN_CONDITION";
        case 8: return "CASE_EXPRESSION";
        case 9: return "FUNCTION_ARG";
        case 10: return "SUBQUERY";
        default: return "UNDEFINED";
    }
}

int main() {
    Parser parser;
    
    // Test queries with identifiers in different contexts
    std::vector<std::string> test_queries = {
        "SELECT employee_id, department FROM employees WHERE salary > 50000",
        "SELECT name FROM users u WHERE u.age IN (SELECT age FROM profiles)",
        "SELECT COUNT(id) FROM orders GROUP BY customer_id HAVING total > 100",
        "SELECT * FROM products p JOIN categories c ON p.category_id = c.id",
        "SELECT CASE WHEN status = 'active' THEN 1 ELSE 0 END FROM accounts",
        "SELECT name FROM employees ORDER BY hire_date DESC"
    };
    
    for (const auto& sql : test_queries) {
        std::cout << "\n=== Testing: " << sql << " ===" << std::endl;
        
        auto result = parser.parse(sql);
        if (result) {
            auto* ast = result.value();
            
            // Collect all identifier nodes
            std::vector<ASTNode*> identifiers;
            collect_identifiers(ast, identifiers);
            
            // Print context hints for each identifier
            std::cout << "Identifiers with context hints:" << std::endl;
            for (auto* id : identifiers) {
                // Extract context hint from upper byte of semantic_flags
                uint8_t context_hint = (id->semantic_flags >> 8) & 0xFF;
                std::cout << "  - '" << id->primary_text << "' in context: " 
                         << get_context_name(context_hint) 
                         << " (hint=" << static_cast<int>(context_hint) << ")" << std::endl;
            }
        } else {
            std::cout << "Parse error: " << result.error().message << std::endl;
        }
    }
    
    std::cout << "\n=== Context Hint Test Complete ===" << std::endl;
    
    return 0;
}