/*
 * DB25 Parser - Complex Query AST File Dumper
 * Dumps ASTs for complex queries to a text file
 */

#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class ASTFileDumper {
private:
    std::ofstream& output;
    int indent_level = 0;
    const int INDENT_WIDTH = 2;
    
    std::string getNodeTypeName(NodeType type) {
        switch(type) {
            case NodeType::SelectStmt: return "SELECT";
            case NodeType::InsertStmt: return "INSERT";
            case NodeType::UpdateStmt: return "UPDATE";
            case NodeType::DeleteStmt: return "DELETE";
            case NodeType::CreateTableStmt: return "CREATE_TABLE";
            case NodeType::CreateIndexStmt: return "CREATE_INDEX";
            case NodeType::CreateViewStmt: return "CREATE_VIEW";
            case NodeType::DropStmt: return "DROP";
            case NodeType::AlterTableStmt: return "ALTER_TABLE";
            case NodeType::TruncateStmt: return "TRUNCATE";
            
            case NodeType::SelectList: return "SELECT_LIST";
            case NodeType::FromClause: return "FROM";
            case NodeType::WhereClause: return "WHERE";
            case NodeType::GroupByClause: return "GROUP_BY";
            case NodeType::HavingClause: return "HAVING";
            case NodeType::OrderByClause: return "ORDER_BY";
            case NodeType::LimitClause: return "LIMIT";
            case NodeType::JoinClause: return "JOIN";
            case NodeType::WithClause: return "WITH";
            case NodeType::CTEDefinition: return "CTE";
            
            case NodeType::BinaryExpr: return "BINARY_OP";
            case NodeType::UnaryExpr: return "UNARY_OP";
            case NodeType::FunctionCall: return "FUNCTION";
            case NodeType::CaseExpr: return "CASE";
            case NodeType::CastExpr: return "CAST";
            case NodeType::SubqueryExpr: return "SUBQUERY";
            case NodeType::ExistsExpr: return "EXISTS";
            case NodeType::InExpr: return "IN";
            case NodeType::BetweenExpr: return "BETWEEN";
            
            case NodeType::TableRef: return "TABLE";
            case NodeType::ColumnRef: return "COLUMN";
            case NodeType::AliasRef: return "ALIAS";
            
            case NodeType::IntegerLiteral: return "INT";
            case NodeType::FloatLiteral: return "FLOAT";
            case NodeType::StringLiteral: return "STRING";
            case NodeType::BooleanLiteral: return "BOOL";
            case NodeType::NullLiteral: return "NULL";
            
            case NodeType::Identifier: return "ID";
            case NodeType::Star: return "STAR";
            case NodeType::Parameter: return "PARAM";
            
            case NodeType::WindowSpec: return "WINDOW";
            case NodeType::PartitionByClause: return "PARTITION_BY";
            case NodeType::FrameClause: return "FRAME";
            
            case NodeType::UnionStmt: return "UNION";
            case NodeType::IntersectStmt: return "INTERSECT";
            case NodeType::ExceptStmt: return "EXCEPT";
            
            case NodeType::LeftJoin: return "LEFT_JOIN";
            case NodeType::RightJoin: return "RIGHT_JOIN";
            case NodeType::InnerJoin: return "INNER_JOIN";
            case NodeType::CrossJoin: return "CROSS_JOIN";
            case NodeType::FullJoin: return "FULL_JOIN";
            
            default: return "NODE";
        }
    }
    
    void printIndent() {
        for (int i = 0; i < indent_level; i++) {
            if (i == indent_level - 1) {
                output << "├─ ";
            } else {
                output << "│  ";
            }
        }
    }
    
    void printNode(const ASTNode* node, bool is_last = false) {
        if (!node) return;
        
        printIndent();
        
        // Node type
        output << getNodeTypeName(node->node_type);
        
        // Node value if present
        if (!node->primary_text.empty()) {
            output << " [" << node->primary_text << "]";
        }
        
        // Node metadata
        output << " (id:" << node->node_id;
        
        if (node->child_count > 0) {
            output << ", children:" << node->child_count;
        }
        
        // Source position
        if (node->source_start > 0 || node->source_end > 0) {
            output << ", pos:" << node->source_start << "-" << node->source_end;
        }
        
        // Window function indicator
        if (node->node_type == NodeType::FunctionCall && (node->semantic_flags & (1 << 8))) {
            output << ", WINDOW";
        }
        
        // Flags
        if (node->flags != NodeFlags::None) {
            output << ", flags:" << static_cast<int>(node->flags);
        }
        
        output << ")" << std::endl;
        
        // Process children
        indent_level++;
        for (auto* child = node->first_child; child; child = child->next_sibling) {
            printNode(child, child->next_sibling == nullptr);
        }
        indent_level--;
    }
    
public:
    explicit ASTFileDumper(std::ofstream& out) : output(out) {}
    
    void dumpQuery(const std::string& title, const std::string& sql, const ASTNode* root, 
                   size_t memory_used, size_t node_count, bool success) {
        output << "================================================================================\n";
        output << title << "\n";
        output << "--------------------------------------------------------------------------------\n";
        output << "SQL Query:\n";
        output << sql << "\n";
        output << "--------------------------------------------------------------------------------\n";
        
        if (success && root) {
            output << "AST Structure:\n";
            printNode(root);
            output << "--------------------------------------------------------------------------------\n";
            output << "Statistics:\n";
            output << "  Memory used: " << memory_used << " bytes\n";
            output << "  Nodes created: " << node_count << "\n";
            output << "  Parse result: SUCCESS\n";
        } else {
            output << "Parse result: FAILED\n";
        }
        
        output << "================================================================================\n\n";
    }
    
    void dumpSummary(int successful, int failed) {
        output << "\n";
        output << "================================================================================\n";
        output << "FINAL SUMMARY\n";
        output << "================================================================================\n";
        output << "Total queries tested: " << (successful + failed) << "\n";
        output << "Successful parses: " << successful << "\n";
        output << "Failed parses: " << failed << "\n";
        output << "Success rate: " << (successful * 100.0 / (successful + failed)) << "%\n";
        output << "================================================================================\n";
    }
};

// Complex queries that showcase our parser's capabilities
std::vector<std::pair<std::string, std::string>> complex_queries = {
    {"1. Complex CTE with Window Functions", R"(
WITH RECURSIVE sales_hierarchy AS (
    SELECT 
        e.employee_id,
        e.manager_id,
        e.name,
        0 AS level,
        CAST(e.employee_id AS VARCHAR) AS path
    FROM employees e
    WHERE e.manager_id IS NULL
    
    UNION ALL
    
    SELECT 
        e.employee_id,
        e.manager_id,
        e.name,
        sh.level + 1,
        sh.path || '/' || CAST(e.employee_id AS VARCHAR)
    FROM employees e
    JOIN sales_hierarchy sh ON e.manager_id = sh.employee_id
),
ranked_sales AS (
    SELECT 
        sh.employee_id,
        sh.name,
        sh.level,
        sh.path,
        SUM(s.amount) AS total_sales,
        ROW_NUMBER() OVER (PARTITION BY sh.level ORDER BY SUM(s.amount) DESC) AS rank_in_level,
        RANK() OVER (ORDER BY SUM(s.amount) DESC) AS overall_rank,
        DENSE_RANK() OVER (PARTITION BY sh.level ORDER BY SUM(s.amount) DESC) AS dense_rank_in_level
    FROM sales_hierarchy sh
    LEFT JOIN sales s ON sh.employee_id = s.employee_id
    GROUP BY sh.employee_id, sh.name, sh.level, sh.path
)
SELECT * FROM ranked_sales 
WHERE rank_in_level <= 3 
ORDER BY level, rank_in_level
)"},

    {"2. Multi-level Subqueries with Aggregations", R"(
SELECT 
    d.department_name,
    d.location,
    dept_stats.avg_salary,
    dept_stats.total_employees,
    dept_stats.top_performer,
    company_avg.avg_salary AS company_avg_salary,
    CASE 
        WHEN dept_stats.avg_salary > company_avg.avg_salary * 1.2 THEN 'High Paying'
        WHEN dept_stats.avg_salary < company_avg.avg_salary * 0.8 THEN 'Low Paying'
        ELSE 'Average'
    END AS salary_category
FROM departments d
JOIN (
    SELECT 
        e.department_id,
        AVG(e.salary) AS avg_salary,
        COUNT(*) AS total_employees,
        MAX(CASE WHEN e.performance_score = max_score.max_perf THEN e.name END) AS top_performer
    FROM employees e
    JOIN (
        SELECT department_id, MAX(performance_score) AS max_perf
        FROM employees
        GROUP BY department_id
    ) max_score ON e.department_id = max_score.department_id
    WHERE e.active = TRUE
    GROUP BY e.department_id
) dept_stats ON d.department_id = dept_stats.department_id
CROSS JOIN (
    SELECT AVG(salary) AS avg_salary
    FROM employees
    WHERE active = TRUE
) company_avg
WHERE dept_stats.total_employees > 5
ORDER BY dept_stats.avg_salary DESC
)"},

    {"3. Complex JOIN with Multiple Window Functions", R"(
SELECT 
    o.order_id,
    o.order_date,
    c.customer_name,
    p.product_name,
    od.quantity * od.unit_price AS line_total,
    SUM(od.quantity * od.unit_price) OVER (PARTITION BY o.order_id) AS order_total,
    ROW_NUMBER() OVER (PARTITION BY c.customer_id ORDER BY o.order_date) AS customer_order_seq,
    RANK() OVER (PARTITION BY p.category ORDER BY od.quantity DESC) AS category_quantity_rank,
    LAG(od.quantity, 1, 0) OVER (PARTITION BY c.customer_id, p.product_id ORDER BY o.order_date) AS prev_quantity,
    LEAD(od.quantity, 1, 0) OVER (PARTITION BY c.customer_id, p.product_id ORDER BY o.order_date) AS next_quantity
FROM orders o
INNER JOIN customers c ON o.customer_id = c.customer_id
INNER JOIN order_details od ON o.order_id = od.order_id
INNER JOIN products p ON od.product_id = p.product_id
WHERE o.order_date >= '2024-01-01'
ORDER BY o.order_date DESC
)"},

    {"4. Nested CASE with Complex Conditions", R"(
SELECT 
    e.employee_id,
    e.name,
    e.department,
    e.salary,
    e.performance_score,
    CASE 
        WHEN e.performance_score >= 90 THEN
            CASE 
                WHEN e.years_of_service >= 10 THEN 'Senior Star'
                WHEN e.years_of_service >= 5 THEN 'Experienced Star'
                ELSE 'Rising Star'
            END
        WHEN e.performance_score >= 70 THEN
            CASE 
                WHEN e.salary > (SELECT AVG(salary) * 1.5 FROM employees WHERE department = e.department) 
                    THEN 'Overpaid Average'
                ELSE 'Fairly Compensated'
            END
        ELSE 'Needs Improvement'
    END AS employee_category
FROM employees e
WHERE e.active = TRUE
ORDER BY e.performance_score DESC
)"},

    {"5. UNION with Aggregations", R"(
SELECT 'Sales' AS department, COUNT(*) AS employee_count, AVG(salary) AS avg_salary
FROM employees
WHERE department = 'Sales'

UNION ALL

SELECT 'Engineering' AS department, COUNT(*) AS employee_count, AVG(salary) AS avg_salary
FROM employees
WHERE department = 'Engineering'

UNION ALL

SELECT 'Marketing' AS department, COUNT(*) AS employee_count, AVG(salary) AS avg_salary
FROM employees
WHERE department = 'Marketing'

ORDER BY avg_salary DESC
)"},

    {"6. Complex HAVING with Subqueries", R"(
SELECT 
    c.customer_id,
    c.customer_name,
    COUNT(DISTINCT o.order_id) AS order_count,
    SUM(od.quantity * od.unit_price) AS total_spent,
    AVG(od.quantity * od.unit_price) AS avg_order_value
FROM customers c
JOIN orders o ON c.customer_id = o.customer_id
JOIN order_details od ON o.order_id = od.order_id
GROUP BY c.customer_id, c.customer_name
HAVING COUNT(DISTINCT o.order_id) >= 5
    AND SUM(od.quantity * od.unit_price) > (
        SELECT AVG(customer_total) * 2
        FROM (
            SELECT SUM(od2.quantity * od2.unit_price) AS customer_total
            FROM customers c2
            JOIN orders o2 ON c2.customer_id = o2.customer_id
            JOIN order_details od2 ON o2.order_id = od2.order_id
            GROUP BY c2.customer_id
        ) customer_totals
    )
ORDER BY total_spent DESC
)"},

    {"7. EXISTS and NOT EXISTS", R"(
SELECT 
    c.customer_id,
    c.customer_name,
    c.country
FROM customers c
WHERE EXISTS (
    SELECT 1
    FROM orders o
    WHERE o.customer_id = c.customer_id
        AND o.order_date >= '2024-01-01'
)
AND NOT EXISTS (
    SELECT 1
    FROM customer_complaints cc
    WHERE cc.customer_id = c.customer_id
        AND cc.status = 'Open'
)
ORDER BY c.customer_name
)"},

    {"8. Complex String Operations", R"(
SELECT 
    customer_id,
    UPPER(first_name) || ' ' || UPPER(last_name) AS full_name,
    LOWER(email) AS email_lower,
    CASE
        WHEN email LIKE '%@gmail.com' THEN 'Gmail'
        WHEN email LIKE '%@yahoo.com' THEN 'Yahoo'
        WHEN email LIKE '%@outlook.com' THEN 'Outlook'
        ELSE 'Other'
    END AS email_provider,
    LENGTH(phone) AS phone_length,
    SUBSTRING(phone FROM 1 FOR 3) AS area_code
FROM customers
WHERE email IS NOT NULL
    AND phone IS NOT NULL
ORDER BY full_name
)"},

    {"9. Self-join for Hierarchical Data", R"(
SELECT 
    e1.employee_id,
    e1.name AS employee_name,
    e1.title AS employee_title,
    e2.name AS manager_name,
    e2.title AS manager_title,
    e3.name AS skip_manager_name,
    COUNT(e4.employee_id) AS direct_reports
FROM employees e1
LEFT JOIN employees e2 ON e1.manager_id = e2.employee_id
LEFT JOIN employees e3 ON e2.manager_id = e3.employee_id
LEFT JOIN employees e4 ON e4.manager_id = e1.employee_id
WHERE e1.active = TRUE
GROUP BY e1.employee_id, e1.name, e1.title, e2.name, e2.title, e3.name
HAVING COUNT(e4.employee_id) > 0
ORDER BY direct_reports DESC
)"},

    {"10. COALESCE and NULLIF", R"(
SELECT 
    employee_id,
    COALESCE(preferred_name, first_name, 'Unknown') AS display_name,
    COALESCE(work_email, personal_email, 'no-email@company.com') AS contact_email,
    COALESCE(salary, 50000) AS effective_salary,
    NULLIF(bonus, 0) AS actual_bonus,
    GREATEST(salary, hourly_rate * 2080, 0) AS max_compensation,
    LEAST(years_experience, 30) AS capped_experience
FROM employees
WHERE COALESCE(termination_date, CURRENT_DATE + 1) > CURRENT_DATE
ORDER BY effective_salary DESC
)"}, 

    {"11. Window Functions with Frame Specifications", R"(
SELECT 
    date,
    sales_amount,
    SUM(sales_amount) OVER (
        ORDER BY date 
        ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
    ) AS running_total,
    AVG(sales_amount) OVER (
        ORDER BY date 
        ROWS BETWEEN 6 PRECEDING AND CURRENT ROW
    ) AS moving_avg_7_days,
    SUM(sales_amount) OVER (
        ORDER BY date 
        ROWS BETWEEN CURRENT ROW AND 6 FOLLOWING
    ) AS next_7_days_total,
    COUNT(*) OVER (
        ORDER BY date 
        ROWS BETWEEN 2 PRECEDING AND 2 FOLLOWING
    ) AS window_count
FROM daily_sales
WHERE date >= '2024-01-01'
ORDER BY date
)"},

    {"12. Complex CTE Chain", R"(
WITH base_data AS (
    SELECT 
        product_id,
        category_id,
        product_name,
        unit_price,
        units_in_stock
    FROM products
    WHERE discontinued = FALSE
),
category_stats AS (
    SELECT 
        category_id,
        COUNT(*) AS product_count,
        AVG(unit_price) AS avg_price,
        SUM(units_in_stock * unit_price) AS total_value
    FROM base_data
    GROUP BY category_id
),
ranked_products AS (
    SELECT 
        b.*,
        c.product_count,
        c.avg_price AS category_avg_price,
        b.unit_price / c.avg_price AS price_index,
        ROW_NUMBER() OVER (PARTITION BY b.category_id ORDER BY b.unit_price DESC) AS price_rank
    FROM base_data b
    JOIN category_stats c ON b.category_id = c.category_id
)
SELECT * FROM ranked_products
WHERE price_rank <= 5
ORDER BY category_id, price_rank
)"},

    {"13. INTERSECT and EXCEPT Operations", R"(
SELECT customer_id FROM customers WHERE country = 'USA'
INTERSECT
SELECT customer_id FROM orders WHERE order_date >= '2024-01-01'
EXCEPT
SELECT customer_id FROM customer_complaints WHERE status = 'Open'
)"},

    {"14. Percentile Window Functions", R"(
SELECT 
    employee_id,
    department,
    salary,
    PERCENT_RANK() OVER (ORDER BY salary) AS salary_percentile,
    CUME_DIST() OVER (ORDER BY salary) AS cumulative_distribution,
    NTILE(4) OVER (ORDER BY salary) AS salary_quartile,
    NTILE(10) OVER (PARTITION BY department ORDER BY salary) AS dept_salary_decile,
    FIRST_VALUE(salary) OVER (PARTITION BY department ORDER BY salary DESC) AS dept_max_salary,
    LAST_VALUE(salary) OVER (PARTITION BY department ORDER BY salary DESC 
        ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING) AS dept_min_salary,
    NTH_VALUE(salary, 2) OVER (PARTITION BY department ORDER BY salary DESC
        ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING) AS second_highest_salary
FROM employees
WHERE active = TRUE
ORDER BY department, salary DESC
)"},

    {"15. Complex IN and NOT IN", R"(
SELECT 
    p.product_id,
    p.product_name,
    p.category_id,
    p.supplier_id
FROM products p
WHERE p.category_id IN (
    SELECT category_id
    FROM categories
    WHERE category_name IN ('Electronics', 'Computers', 'Software')
)
AND p.supplier_id NOT IN (
    SELECT supplier_id
    FROM suppliers
    WHERE country NOT IN ('USA', 'Canada', 'Mexico')
        OR rating < 3
)
AND p.product_id IN (
    SELECT product_id
    FROM order_details od
    JOIN orders o ON od.order_id = o.order_id
    WHERE o.order_date >= '2024-01-01'
    GROUP BY product_id
    HAVING SUM(quantity) > 100
)
ORDER BY p.product_name
)"},

    {"16. BETWEEN and LIKE Patterns", R"(
SELECT 
    customer_id,
    customer_name,
    order_date,
    total_amount
FROM orders
WHERE order_date BETWEEN '2024-01-01' AND '2024-12-31'
    AND total_amount BETWEEN 1000 AND 10000
    AND customer_name LIKE 'A%'
    AND customer_name NOT LIKE '%Test%'
    AND shipping_address LIKE '%New York%'
ORDER BY order_date DESC
)"},

    {"17. Multiple Aggregation Functions", R"(
SELECT 
    category,
    COUNT(*) AS product_count,
    COUNT(DISTINCT supplier_id) AS supplier_count,
    SUM(units_in_stock) AS total_units,
    AVG(unit_price) AS avg_price,
    MIN(unit_price) AS min_price,
    MAX(unit_price) AS max_price,
    STDDEV(unit_price) AS price_stddev,
    VAR_POP(unit_price) AS price_variance,
    SUM(units_in_stock * unit_price) AS total_value
FROM products
WHERE discontinued = FALSE
GROUP BY category
HAVING COUNT(*) >= 5
    AND AVG(unit_price) > 10
ORDER BY total_value DESC
)"},

    {"18. Nested Window Functions", R"(
SELECT 
    sub.*,
    SUM(monthly_revenue) OVER (
        PARTITION BY customer_id 
        ORDER BY order_month 
        ROWS BETWEEN 2 PRECEDING AND CURRENT ROW
    ) AS three_month_revenue
FROM (
    SELECT 
        customer_id,
        DATE_TRUNC('month', order_date) AS order_month,
        SUM(total_amount) AS monthly_revenue,
        COUNT(*) AS monthly_orders,
        ROW_NUMBER() OVER (PARTITION BY customer_id ORDER BY DATE_TRUNC('month', order_date)) AS month_seq
    FROM orders
    GROUP BY customer_id, DATE_TRUNC('month', order_date)
) sub
WHERE monthly_revenue > 1000
ORDER BY customer_id, order_month
)"},

    {"19. CAST Operations", R"(
SELECT 
    order_id,
    CAST(order_id AS VARCHAR) AS order_id_string,
    CAST(total_amount AS INTEGER) AS amount_int,
    CAST(quantity AS DECIMAL(10,2)) AS quantity_decimal,
    CAST(order_date AS VARCHAR) || ' - Order' AS order_label,
    CAST(CAST(total_amount AS INTEGER) AS VARCHAR) AS amount_string
FROM orders
WHERE CAST(total_amount AS INTEGER) > 1000
ORDER BY order_id
)"},

    {"20. Full Complex Query", R"(
WITH RECURSIVE category_tree AS (
    SELECT category_id, parent_id, name, 0 AS level
    FROM categories
    WHERE parent_id IS NULL
    UNION ALL
    SELECT c.category_id, c.parent_id, c.name, ct.level + 1
    FROM categories c
    JOIN category_tree ct ON c.parent_id = ct.category_id
)
SELECT 
    p.product_id,
    p.product_name,
    ct.name AS category_name,
    ct.level AS category_level,
    s.supplier_name,
    p.unit_price,
    p.units_in_stock,
    p.units_in_stock * p.unit_price AS inventory_value,
    COUNT(od.order_id) AS times_ordered,
    SUM(od.quantity) AS total_quantity_sold,
    AVG(od.quantity) AS avg_quantity_per_order,
    ROW_NUMBER() OVER (PARTITION BY ct.category_id ORDER BY SUM(od.quantity) DESC) AS category_sales_rank,
    RANK() OVER (ORDER BY SUM(od.quantity * od.unit_price) DESC) AS overall_revenue_rank,
    SUM(SUM(od.quantity * od.unit_price)) OVER (
        PARTITION BY ct.category_id 
        ORDER BY SUM(od.quantity * od.unit_price) DESC
        ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
    ) AS category_cumulative_revenue,
    CASE
        WHEN p.units_in_stock = 0 THEN 'Out of Stock'
        WHEN p.units_in_stock < p.reorder_level THEN 'Low Stock'
        WHEN p.units_in_stock > p.reorder_level * 3 THEN 'Overstock'
        ELSE 'Normal'
    END AS stock_status
FROM products p
JOIN category_tree ct ON p.category_id = ct.category_id
JOIN suppliers s ON p.supplier_id = s.supplier_id
LEFT JOIN order_details od ON p.product_id = od.product_id
LEFT JOIN orders o ON od.order_id = o.order_id AND o.order_date >= '2024-01-01'
WHERE p.discontinued = FALSE
    AND s.active = TRUE
GROUP BY p.product_id, p.product_name, ct.category_id, ct.name, ct.level, 
         s.supplier_name, p.unit_price, p.units_in_stock, p.reorder_level
HAVING COUNT(od.order_id) > 0 OR p.units_in_stock > 0
ORDER BY overall_revenue_rank, category_sales_rank
)"}
};

int main() {
    // Open output file
    std::ofstream output_file("complex_queries_ast_dump.txt");
    if (!output_file.is_open()) {
        std::cerr << "Error: Could not open output file" << std::endl;
        return 1;
    }
    
    // Write header
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    output_file << "================================================================================\n";
    output_file << "DB25 Parser - Complex Query AST Dump\n";
    output_file << "Generated: " << std::ctime(&time_t);
    output_file << "================================================================================\n\n";
    
    Parser parser;
    ASTFileDumper dumper(output_file);
    
    int successful = 0;
    int failed = 0;
    
    std::cout << "Generating AST dump file..." << std::endl;
    
    for (size_t i = 0; i < complex_queries.size(); ++i) {
        const auto& [title, sql] = complex_queries[i];
        
        std::cout << "Processing query " << (i + 1) << "/" << complex_queries.size() 
                  << ": " << title << std::endl;
        
        auto result = parser.parse(sql);
        
        if (result.has_value()) {
            successful++;
            dumper.dumpQuery(title, sql, result.value(), 
                           parser.get_memory_used(), 
                           parser.get_node_count(), true);
        } else {
            failed++;
            dumper.dumpQuery(title, sql, nullptr, 0, 0, false);
        }
        
        parser.reset(); // Reset for next query
    }
    
    // Write summary
    dumper.dumpSummary(successful, failed);
    
    output_file.close();
    
    std::cout << "\n";
    std::cout << "================================================================================\n";
    std::cout << "AST dump complete!\n";
    std::cout << "Output file: complex_queries_ast_dump.txt\n";
    std::cout << "Successful parses: " << successful << "/" << complex_queries.size() << "\n";
    std::cout << "Failed parses: " << failed << "/" << complex_queries.size() << "\n";
    std::cout << "================================================================================\n";
    
    return 0;
}