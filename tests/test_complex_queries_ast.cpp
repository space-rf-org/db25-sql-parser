/*
 * DB25 Parser - Complex Query AST Dumper
 * Generates and visualizes ASTs for the most complex queries our parser supports
 */

#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <sstream>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

// ANSI color codes for terminal output
namespace colors {
    const char* RESET = "\033[0m";
    const char* BOLD = "\033[1m";
    
    // Node type colors
    const char* STMT = "\033[1;35m";      // Magenta for statements
    const char* CLAUSE = "\033[1;36m";    // Cyan for clauses
    const char* EXPR = "\033[1;33m";      // Yellow for expressions
    const char* FUNC = "\033[1;32m";      // Green for functions
    const char* LITERAL = "\033[1;31m";   // Red for literals
    const char* REF = "\033[1;34m";       // Blue for references
    const char* OP = "\033[1;37m";        // White for operators
    const char* KEYWORD = "\033[1;95m";   // Light magenta for keywords
    
    // Structure colors
    const char* TREE = "\033[2;37m";      // Dim white for tree structure
    const char* ID = "\033[2;93m";        // Dim yellow for IDs
    const char* COUNT = "\033[2;96m";     // Dim cyan for counts
}

class ColoredASTDumper {
private:
    int indent_level = 0;
    const int INDENT_WIDTH = 2;
    
    const char* getNodeColor(NodeType type) {
        // Statements
        if (type >= NodeType::SelectStmt && type <= NodeType::ExplainStmt) {
            return colors::STMT;
        }
        // Clauses
        if (type >= NodeType::SelectList && type <= NodeType::ReturningClause) {
            return colors::CLAUSE;
        }
        // Expressions
        if (type >= NodeType::BinaryExpr && type <= NodeType::WindowExpr) {
            return colors::EXPR;
        }
        // Functions
        if (type == NodeType::FunctionCall || type == NodeType::FunctionExpr) {
            return colors::FUNC;
        }
        // Literals
        if (type >= NodeType::IntegerLiteral && type <= NodeType::DateTimeLiteral) {
            return colors::LITERAL;
        }
        // References
        if (type == NodeType::TableRef || type == NodeType::ColumnRef || 
            type == NodeType::AliasRef || type == NodeType::SchemaRef) {
            return colors::REF;
        }
        // Identifiers
        if (type == NodeType::Identifier) {
            return colors::REF;
        }
        return colors::RESET;
    }
    
    std::string getNodeTypeName(NodeType type) {
        switch(type) {
            case NodeType::SelectStmt: return "SELECT";
            case NodeType::InsertStmt: return "INSERT";
            case NodeType::UpdateStmt: return "UPDATE";
            case NodeType::DeleteStmt: return "DELETE";
            case NodeType::CreateTableStmt: return "CREATE TABLE";
            case NodeType::CreateIndexStmt: return "CREATE INDEX";
            case NodeType::CreateViewStmt: return "CREATE VIEW";
            case NodeType::DropStmt: return "DROP";
            case NodeType::AlterTableStmt: return "ALTER TABLE";
            case NodeType::TruncateStmt: return "TRUNCATE";
            
            case NodeType::SelectList: return "SELECT_LIST";
            case NodeType::FromClause: return "FROM";
            case NodeType::WhereClause: return "WHERE";
            case NodeType::GroupByClause: return "GROUP BY";
            case NodeType::HavingClause: return "HAVING";
            case NodeType::OrderByClause: return "ORDER BY";
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
            case NodeType::Star: return "*";
            case NodeType::Parameter: return "PARAM";
            
            case NodeType::WindowSpec: return "WINDOW";
            case NodeType::PartitionByClause: return "PARTITION BY";
            case NodeType::FrameClause: return "FRAME";
            
            default: return "NODE";
        }
    }
    
    void printIndent() {
        std::cout << colors::TREE;
        for (int i = 0; i < indent_level; i++) {
            if (i == indent_level - 1) {
                std::cout << "├─";
            } else {
                std::cout << "│ ";
            }
        }
        std::cout << colors::RESET;
    }
    
    void printNode(const ASTNode* node, bool is_last = false) {
        if (!node) return;
        
        printIndent();
        
        // Node type with color
        std::cout << getNodeColor(node->node_type) 
                  << getNodeTypeName(node->node_type) 
                  << colors::RESET;
        
        // Node value if present
        if (!node->primary_text.empty()) {
            std::cout << " " << colors::BOLD << "[" 
                      << node->primary_text << "]" << colors::RESET;
        }
        
        // Node metadata
        std::cout << " " << colors::ID << "(id:" << node->node_id << ")" << colors::RESET;
        
        if (node->child_count > 0) {
            std::cout << " " << colors::COUNT << "{" << node->child_count << " children}" << colors::RESET;
        }
        
        // Window function indicator
        if (node->node_type == NodeType::FunctionCall && (node->semantic_flags & (1 << 8))) {
            std::cout << " " << colors::KEYWORD << "[WINDOW]" << colors::RESET;
        }
        
        std::cout << std::endl;
        
        // Process children
        indent_level++;
        for (auto* child = node->first_child; child; child = child->next_sibling) {
            printNode(child, child->next_sibling == nullptr);
        }
        indent_level--;
    }
    
public:
    void dump(const ASTNode* root) {
        if (!root) {
            std::cout << colors::LITERAL << "NULL AST" << colors::RESET << std::endl;
            return;
        }
        
        std::cout << colors::BOLD << "\n════════════════════════════════════════════════════════" << colors::RESET << std::endl;
        printNode(root);
        std::cout << colors::BOLD << "════════════════════════════════════════════════════════\n" << colors::RESET << std::endl;
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
    c.customer_segment,
    p.product_name,
    p.category,
    od.quantity,
    od.unit_price,
    od.quantity * od.unit_price AS line_total,
    SUM(od.quantity * od.unit_price) OVER (PARTITION BY o.order_id) AS order_total,
    SUM(od.quantity * od.unit_price) OVER (
        PARTITION BY c.customer_id 
        ORDER BY o.order_date 
        ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
    ) AS customer_running_total,
    AVG(od.quantity * od.unit_price) OVER (
        PARTITION BY p.category 
        ORDER BY o.order_date 
        ROWS BETWEEN 30 PRECEDING AND CURRENT ROW
    ) AS category_30day_avg,
    LAG(od.quantity, 1, 0) OVER (
        PARTITION BY c.customer_id, p.product_id 
        ORDER BY o.order_date
    ) AS prev_order_quantity,
    LEAD(od.quantity, 1, 0) OVER (
        PARTITION BY c.customer_id, p.product_id 
        ORDER BY o.order_date
    ) AS next_order_quantity,
    FIRST_VALUE(o.order_date) OVER (
        PARTITION BY c.customer_id, p.category 
        ORDER BY o.order_date
    ) AS first_category_purchase,
    COUNT(*) OVER (PARTITION BY c.customer_segment) AS segment_order_count
FROM orders o
INNER JOIN customers c ON o.customer_id = c.customer_id
INNER JOIN order_details od ON o.order_id = od.order_id
INNER JOIN products p ON od.product_id = p.product_id
LEFT JOIN suppliers s ON p.supplier_id = s.supplier_id
WHERE o.order_date >= '2024-01-01'
    AND o.status IN ('Completed', 'Shipped', 'Delivered')
    AND c.country = 'USA'
ORDER BY o.order_date DESC, c.customer_name, p.product_name
)"},

    {"4. Nested CASE with Complex Conditions", R"(
SELECT 
    e.employee_id,
    e.name,
    e.department,
    e.salary,
    e.years_of_service,
    e.performance_score,
    CASE 
        WHEN e.performance_score >= 90 THEN
            CASE 
                WHEN e.years_of_service >= 10 THEN 'Senior Star Performer'
                WHEN e.years_of_service >= 5 THEN 'Experienced Star Performer'
                ELSE 'Rising Star'
            END
        WHEN e.performance_score >= 70 THEN
            CASE 
                WHEN e.salary > (SELECT AVG(salary) * 1.5 FROM employees WHERE department = e.department) 
                    THEN 'Overpaid Average Performer'
                WHEN e.salary < (SELECT AVG(salary) * 0.8 FROM employees WHERE department = e.department)
                    THEN 'Underpaid Average Performer'
                ELSE 'Fairly Compensated'
            END
        WHEN e.performance_score >= 50 THEN
            CASE
                WHEN EXISTS (
                    SELECT 1 FROM training_completed tc 
                    WHERE tc.employee_id = e.employee_id 
                    AND tc.completion_date > CURRENT_DATE - 90
                ) THEN 'Improving'
                ELSE 'Needs Training'
            END
        ELSE 'Performance Improvement Required'
    END AS employee_category,
    CASE
        WHEN e.department IN ('Sales', 'Marketing') AND e.performance_score > 80 
            THEN e.salary * 0.15
        WHEN e.department = 'Engineering' AND e.performance_score > 85
            THEN e.salary * 0.20
        WHEN e.department = 'Support' AND e.performance_score > 75
            THEN e.salary * 0.10
        ELSE e.salary * 0.05
    END AS suggested_bonus
FROM employees e
WHERE e.active = TRUE
ORDER BY e.performance_score DESC, e.salary DESC
)"},

    {"5. Multiple CTEs with Set Operations", R"(
WITH high_value_customers AS (
    SELECT 
        customer_id,
        SUM(order_total) AS total_spent,
        COUNT(*) AS order_count,
        AVG(order_total) AS avg_order_value
    FROM orders
    WHERE order_date >= '2024-01-01'
    GROUP BY customer_id
    HAVING SUM(order_total) > 10000
),
frequent_customers AS (
    SELECT 
        customer_id,
        COUNT(*) AS order_count,
        MAX(order_date) AS last_order_date
    FROM orders
    WHERE order_date >= '2024-01-01'
    GROUP BY customer_id
    HAVING COUNT(*) >= 10
),
vip_customers AS (
    SELECT hv.customer_id
    FROM high_value_customers hv
    
    INTERSECT
    
    SELECT fc.customer_id
    FROM frequent_customers fc
),
at_risk_customers AS (
    SELECT customer_id
    FROM high_value_customers
    WHERE customer_id NOT IN (
        SELECT customer_id 
        FROM orders 
        WHERE order_date > CURRENT_DATE - 30
    )
    
    UNION
    
    SELECT customer_id
    FROM frequent_customers
    WHERE last_order_date < CURRENT_DATE - 60
)
SELECT 
    c.customer_id,
    c.customer_name,
    c.email,
    CASE
        WHEN vc.customer_id IS NOT NULL THEN 'VIP'
        WHEN ar.customer_id IS NOT NULL THEN 'At Risk'
        WHEN hv.customer_id IS NOT NULL THEN 'High Value'
        WHEN fc.customer_id IS NOT NULL THEN 'Frequent'
        ELSE 'Regular'
    END AS customer_segment,
    COALESCE(hv.total_spent, 0) AS total_spent,
    COALESCE(hv.order_count, 0) AS high_value_orders,
    COALESCE(fc.order_count, 0) AS total_orders
FROM customers c
LEFT JOIN vip_customers vc ON c.customer_id = vc.customer_id
LEFT JOIN at_risk_customers ar ON c.customer_id = ar.customer_id
LEFT JOIN high_value_customers hv ON c.customer_id = hv.customer_id
LEFT JOIN frequent_customers fc ON c.customer_id = fc.customer_id
WHERE c.active = TRUE
ORDER BY 
    CASE
        WHEN vc.customer_id IS NOT NULL THEN 1
        WHEN ar.customer_id IS NOT NULL THEN 2
        WHEN hv.customer_id IS NOT NULL THEN 3
        WHEN fc.customer_id IS NOT NULL THEN 4
        ELSE 5
    END,
    hv.total_spent DESC NULLS LAST
)"},

    {"6. Complex Aggregations with HAVING", R"(
SELECT 
    p.category,
    p.subcategory,
    s.supplier_name,
    COUNT(DISTINCT p.product_id) AS product_count,
    COUNT(DISTINCT o.order_id) AS order_count,
    SUM(od.quantity) AS total_quantity_sold,
    SUM(od.quantity * od.unit_price) AS total_revenue,
    AVG(od.quantity * od.unit_price) AS avg_order_value,
    MIN(od.unit_price) AS min_price,
    MAX(od.unit_price) AS max_price,
    STDDEV(od.quantity) AS quantity_stddev,
    VAR_POP(od.unit_price) AS price_variance
FROM products p
JOIN suppliers s ON p.supplier_id = s.supplier_id
JOIN order_details od ON p.product_id = od.product_id
JOIN orders o ON od.order_id = o.order_id
WHERE o.order_date BETWEEN '2024-01-01' AND '2024-12-31'
    AND p.discontinued = FALSE
    AND s.country IN ('USA', 'Canada', 'Mexico')
GROUP BY p.category, p.subcategory, s.supplier_name
HAVING COUNT(DISTINCT o.order_id) >= 100
    AND SUM(od.quantity * od.unit_price) > 50000
    AND AVG(od.quantity * od.unit_price) > (
        SELECT AVG(quantity * unit_price) * 1.2
        FROM order_details
    )
ORDER BY total_revenue DESC, product_count DESC
)"},

    {"7. Correlated Subqueries with EXISTS", R"(
SELECT 
    c.customer_id,
    c.customer_name,
    c.country,
    c.credit_limit,
    (
        SELECT COUNT(*)
        FROM orders o
        WHERE o.customer_id = c.customer_id
            AND o.order_date >= CURRENT_DATE - 365
    ) AS orders_last_year,
    (
        SELECT SUM(od.quantity * od.unit_price)
        FROM orders o
        JOIN order_details od ON o.order_id = od.order_id
        WHERE o.customer_id = c.customer_id
            AND o.order_date >= CURRENT_DATE - 30
    ) AS revenue_last_30_days,
    EXISTS (
        SELECT 1
        FROM orders o
        WHERE o.customer_id = c.customer_id
            AND o.status = 'Pending'
            AND o.order_date < CURRENT_DATE - 7
    ) AS has_overdue_orders,
    EXISTS (
        SELECT 1
        FROM customer_complaints cc
        WHERE cc.customer_id = c.customer_id
            AND cc.status = 'Open'
            AND cc.severity = 'High'
    ) AS has_open_complaints,
    CASE
        WHEN EXISTS (
            SELECT 1
            FROM payments p
            WHERE p.customer_id = c.customer_id
                AND p.payment_date < p.due_date - 30
        ) THEN 'Early Payer'
        WHEN EXISTS (
            SELECT 1
            FROM payments p
            WHERE p.customer_id = c.customer_id
                AND p.payment_date > p.due_date + 30
        ) THEN 'Late Payer'
        ELSE 'Regular Payer'
    END AS payment_behavior
FROM customers c
WHERE c.active = TRUE
    AND (
        EXISTS (
            SELECT 1
            FROM orders o
            WHERE o.customer_id = c.customer_id
                AND o.order_date >= CURRENT_DATE - 90
        )
        OR c.credit_limit > 100000
    )
ORDER BY revenue_last_30_days DESC NULLS LAST
)"},

    {"8. Advanced Window Functions with Percentiles", R"(
SELECT 
    e.employee_id,
    e.name,
    e.department,
    e.salary,
    e.performance_score,
    ROW_NUMBER() OVER (ORDER BY e.salary DESC) AS salary_rank,
    RANK() OVER (PARTITION BY e.department ORDER BY e.performance_score DESC) AS dept_performance_rank,
    DENSE_RANK() OVER (ORDER BY e.performance_score DESC) AS company_performance_dense_rank,
    PERCENT_RANK() OVER (PARTITION BY e.department ORDER BY e.salary) AS dept_salary_percentile,
    CUME_DIST() OVER (ORDER BY e.salary) AS salary_cumulative_dist,
    NTILE(4) OVER (ORDER BY e.salary) AS salary_quartile,
    NTILE(10) OVER (PARTITION BY e.department ORDER BY e.performance_score) AS dept_performance_decile,
    e.salary - LAG(e.salary, 1) OVER (PARTITION BY e.department ORDER BY e.salary) AS salary_gap_to_lower,
    LEAD(e.salary, 1) OVER (PARTITION BY e.department ORDER BY e.salary) - e.salary AS salary_gap_to_higher,
    FIRST_VALUE(e.name) OVER (
        PARTITION BY e.department 
        ORDER BY e.salary DESC
        ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING
    ) AS highest_paid_in_dept,
    LAST_VALUE(e.name) OVER (
        PARTITION BY e.department 
        ORDER BY e.salary DESC
        ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING
    ) AS lowest_paid_in_dept,
    NTH_VALUE(e.salary, 2) OVER (
        PARTITION BY e.department 
        ORDER BY e.salary DESC
        ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING
    ) AS second_highest_salary_in_dept
FROM employees e
WHERE e.active = TRUE
    AND e.hire_date <= CURRENT_DATE - 365
ORDER BY e.department, e.salary DESC
)"},

    {"9. Multi-table JOIN with Complex Predicates", R"(
SELECT 
    o.order_id,
    o.order_date,
    c.customer_name,
    c.country AS customer_country,
    s.company_name AS shipping_company,
    s.country AS shipper_country,
    e.name AS employee_name,
    e.title AS employee_title,
    sup.company_name AS supplier_name,
    sup.country AS supplier_country,
    p.product_name,
    cat.category_name,
    od.quantity,
    od.unit_price,
    od.discount,
    (od.quantity * od.unit_price * (1 - od.discount)) AS net_amount
FROM orders o
INNER JOIN customers c 
    ON o.customer_id = c.customer_id
    AND c.active = TRUE
    AND c.credit_limit > 0
LEFT JOIN shippers s 
    ON o.shipper_id = s.shipper_id
    AND s.active = TRUE
INNER JOIN employees e 
    ON o.employee_id = e.employee_id
    AND e.department = 'Sales'
    AND e.active = TRUE
INNER JOIN order_details od 
    ON o.order_id = od.order_id
    AND od.quantity > 0
    AND od.unit_price > 0
INNER JOIN products p 
    ON od.product_id = p.product_id
    AND p.discontinued = FALSE
    AND p.units_in_stock > 0
LEFT JOIN suppliers sup 
    ON p.supplier_id = sup.supplier_id
    AND sup.active = TRUE
    AND sup.rating >= 3
LEFT JOIN categories cat 
    ON p.category_id = cat.category_id
WHERE o.order_date BETWEEN '2024-01-01' AND '2024-12-31'
    AND o.status NOT IN ('Cancelled', 'Refunded')
    AND (
        (c.country = s.country AND od.discount > 0.1)
        OR
        (c.country != s.country AND od.discount = 0)
    )
    AND od.quantity * od.unit_price > (
        SELECT AVG(quantity * unit_price) * 0.5
        FROM order_details
        WHERE order_id IN (
            SELECT order_id 
            FROM orders 
            WHERE EXTRACT(YEAR FROM order_date) = 2024
        )
    )
ORDER BY net_amount DESC, o.order_date DESC
)"},

    {"10. UNION ALL with Different Sources", R"(
SELECT 
    'Order' AS transaction_type,
    o.order_id AS transaction_id,
    o.order_date AS transaction_date,
    c.customer_name AS party_name,
    'Customer' AS party_type,
    SUM(od.quantity * od.unit_price) AS amount,
    'Revenue' AS category
FROM orders o
JOIN customers c ON o.customer_id = c.customer_id
JOIN order_details od ON o.order_id = od.order_id
WHERE o.order_date >= '2024-01-01'
GROUP BY o.order_id, o.order_date, c.customer_name

UNION ALL

SELECT 
    'Purchase' AS transaction_type,
    po.purchase_order_id AS transaction_id,
    po.order_date AS transaction_date,
    s.supplier_name AS party_name,
    'Supplier' AS party_type,
    -1 * SUM(pod.quantity * pod.unit_cost) AS amount,
    'Expense' AS category
FROM purchase_orders po
JOIN suppliers s ON po.supplier_id = s.supplier_id
JOIN purchase_order_details pod ON po.purchase_order_id = pod.purchase_order_id
WHERE po.order_date >= '2024-01-01'
GROUP BY po.purchase_order_id, po.order_date, s.supplier_name

UNION ALL

SELECT 
    'Refund' AS transaction_type,
    r.refund_id AS transaction_id,
    r.refund_date AS transaction_date,
    c.customer_name AS party_name,
    'Customer' AS party_type,
    -1 * r.refund_amount AS amount,
    'Revenue Adjustment' AS category
FROM refunds r
JOIN customers c ON r.customer_id = c.customer_id
WHERE r.refund_date >= '2024-01-01'

ORDER BY transaction_date DESC, amount DESC
)"},

    {"11. Deeply Nested Subqueries", R"(
SELECT 
    dept_summary.department_name,
    dept_summary.total_employees,
    dept_summary.avg_salary,
    dept_summary.top_performer_count,
    company_comparison.company_avg_salary,
    dept_summary.avg_salary / company_comparison.company_avg_salary AS salary_index
FROM (
    SELECT 
        d.department_name,
        COUNT(e.employee_id) AS total_employees,
        AVG(e.salary) AS avg_salary,
        SUM(
            CASE 
                WHEN e.performance_score > (
                    SELECT AVG(performance_score) + STDDEV(performance_score)
                    FROM employees e2
                    WHERE e2.department_id = d.department_id
                )
                THEN 1 
                ELSE 0 
            END
        ) AS top_performer_count
    FROM departments d
    JOIN employees e ON d.department_id = e.department_id
    WHERE e.active = TRUE
        AND e.hire_date <= CURRENT_DATE - 365
        AND e.salary > (
            SELECT MIN(salary)
            FROM (
                SELECT salary
                FROM employees
                WHERE department_id = d.department_id
                ORDER BY salary DESC
                LIMIT 10
            ) top_10_salaries
        )
    GROUP BY d.department_id, d.department_name
) dept_summary
CROSS JOIN (
    SELECT AVG(salary) AS company_avg_salary
    FROM employees
    WHERE active = TRUE
) company_comparison
WHERE dept_summary.total_employees >= 5
ORDER BY salary_index DESC
)"},

    {"12. Complex CASE in WHERE and SELECT", R"(
SELECT 
    p.product_id,
    p.product_name,
    p.category,
    p.unit_price,
    p.units_in_stock,
    CASE 
        WHEN p.units_in_stock = 0 THEN 'Out of Stock'
        WHEN p.units_in_stock < p.reorder_level THEN 'Low Stock'
        WHEN p.units_in_stock < p.reorder_level * 2 THEN 'Normal Stock'
        ELSE 'Overstock'
    END AS stock_status,
    CASE
        WHEN sales_data.total_sold IS NULL THEN 'No Sales'
        WHEN sales_data.total_sold < 10 THEN 'Slow Moving'
        WHEN sales_data.total_sold < 50 THEN 'Normal Moving'
        WHEN sales_data.total_sold < 100 THEN 'Fast Moving'
        ELSE 'Top Seller'
    END AS sales_velocity,
    COALESCE(sales_data.total_sold, 0) AS units_sold_last_90_days,
    COALESCE(sales_data.revenue, 0) AS revenue_last_90_days
FROM products p
LEFT JOIN (
    SELECT 
        od.product_id,
        SUM(od.quantity) AS total_sold,
        SUM(od.quantity * od.unit_price) AS revenue
    FROM order_details od
    JOIN orders o ON od.order_id = o.order_id
    WHERE o.order_date >= CURRENT_DATE - 90
    GROUP BY od.product_id
) sales_data ON p.product_id = sales_data.product_id
WHERE 
    CASE
        WHEN p.category IN ('Electronics', 'Computers') THEN p.unit_price > 100
        WHEN p.category IN ('Books', 'Music') THEN p.unit_price > 10
        WHEN p.category = 'Food' THEN p.units_in_stock > 50
        ELSE TRUE
    END
    AND (
        CASE 
            WHEN EXTRACT(MONTH FROM CURRENT_DATE) IN (11, 12) 
                THEN p.seasonal_item = TRUE OR p.gift_item = TRUE
            WHEN EXTRACT(MONTH FROM CURRENT_DATE) IN (6, 7, 8)
                THEN p.summer_item = TRUE
            ELSE TRUE
        END
    )
ORDER BY 
    CASE 
        WHEN stock_status = 'Out of Stock' THEN 1
        WHEN stock_status = 'Low Stock' THEN 2
        ELSE 3
    END,
    revenue_last_90_days DESC
)"},

    {"13. Self-joins and Hierarchical Queries", R"(
SELECT 
    e1.employee_id,
    e1.name AS employee_name,
    e1.title AS employee_title,
    e1.salary AS employee_salary,
    e2.name AS manager_name,
    e2.title AS manager_title,
    e2.salary AS manager_salary,
    e3.name AS skip_manager_name,
    e3.title AS skip_manager_title,
    (e1.salary / e2.salary) AS salary_ratio_to_manager,
    COUNT(e4.employee_id) AS direct_reports_count,
    AVG(e4.salary) AS avg_direct_report_salary,
    CASE
        WHEN e1.salary > e2.salary THEN 'Anomaly: Paid more than manager'
        WHEN e1.salary > AVG(e4.salary) * 1.5 THEN 'Highly paid for level'
        WHEN e1.salary < AVG(e4.salary) * 0.8 THEN 'Underpaid for level'
        ELSE 'Normal compensation'
    END AS compensation_analysis
FROM employees e1
LEFT JOIN employees e2 ON e1.manager_id = e2.employee_id
LEFT JOIN employees e3 ON e2.manager_id = e3.employee_id
LEFT JOIN employees e4 ON e4.manager_id = e1.employee_id
WHERE e1.active = TRUE
    AND (e2.active = TRUE OR e2.employee_id IS NULL)
GROUP BY 
    e1.employee_id, e1.name, e1.title, e1.salary,
    e2.employee_id, e2.name, e2.title, e2.salary,
    e3.employee_id, e3.name, e3.title
HAVING COUNT(e4.employee_id) > 0 
    OR e1.salary > 100000
ORDER BY 
    CASE 
        WHEN e3.employee_id IS NULL AND e2.employee_id IS NULL THEN 1
        WHEN e3.employee_id IS NULL THEN 2
        ELSE 3
    END,
    e1.salary DESC
)"},

    {"14. Complex String Operations and Pattern Matching", R"(
SELECT 
    c.customer_id,
    c.customer_name,
    c.email,
    c.phone,
    CASE
        WHEN c.email LIKE '%@gmail.com' THEN 'Gmail'
        WHEN c.email LIKE '%@yahoo.com' THEN 'Yahoo'
        WHEN c.email LIKE '%@outlook.com' THEN 'Outlook'
        WHEN c.email LIKE '%@%.com' THEN 'Other Commercial'
        WHEN c.email LIKE '%@%.org' THEN 'Organization'
        WHEN c.email LIKE '%@%.edu' THEN 'Educational'
        ELSE 'Unknown'
    END AS email_provider,
    CASE
        WHEN c.phone LIKE '+1%' THEN 'USA/Canada'
        WHEN c.phone LIKE '+44%' THEN 'UK'
        WHEN c.phone LIKE '+33%' THEN 'France'
        WHEN c.phone LIKE '+49%' THEN 'Germany'
        WHEN c.phone LIKE '+%' THEN 'International'
        ELSE 'Domestic'
    END AS phone_region,
    UPPER(SUBSTRING(c.customer_name FROM 1 FOR 1)) || 
    LOWER(SUBSTRING(c.customer_name FROM 2)) AS formatted_name,
    LENGTH(c.customer_name) AS name_length,
    POSITION('@' IN c.email) AS at_position,
    COUNT(*) OVER (
        PARTITION BY SUBSTRING(c.email FROM POSITION('@' IN c.email))
    ) AS same_domain_count
FROM customers c
WHERE c.active = TRUE
    AND c.email IS NOT NULL
    AND c.email LIKE '%@%'
    AND LENGTH(c.email) > 5
    AND (
        c.customer_name NOT LIKE '%Test%'
        AND c.customer_name NOT LIKE '%Demo%'
        AND c.customer_name NOT LIKE '%Sample%'
    )
ORDER BY same_domain_count DESC, c.customer_name
)"},

    {"15. Time-based Analysis with Date Functions", R"(
SELECT 
    DATE_TRUNC('month', o.order_date) AS order_month,
    EXTRACT(YEAR FROM o.order_date) AS order_year,
    EXTRACT(QUARTER FROM o.order_date) AS order_quarter,
    EXTRACT(WEEK FROM o.order_date) AS order_week,
    EXTRACT(DOW FROM o.order_date) AS day_of_week,
    COUNT(DISTINCT o.order_id) AS total_orders,
    COUNT(DISTINCT o.customer_id) AS unique_customers,
    SUM(od.quantity * od.unit_price) AS total_revenue,
    AVG(od.quantity * od.unit_price) AS avg_order_value,
    SUM(od.quantity * od.unit_price) / NULLIF(
        LAG(SUM(od.quantity * od.unit_price), 1) OVER (
            ORDER BY DATE_TRUNC('month', o.order_date)
        ), 0
    ) - 1 AS month_over_month_growth,
    SUM(od.quantity * od.unit_price) / NULLIF(
        LAG(SUM(od.quantity * od.unit_price), 12) OVER (
            ORDER BY DATE_TRUNC('month', o.order_date)
        ), 0
    ) - 1 AS year_over_year_growth,
    SUM(SUM(od.quantity * od.unit_price)) OVER (
        ORDER BY DATE_TRUNC('month', o.order_date)
        ROWS BETWEEN 11 PRECEDING AND CURRENT ROW
    ) AS rolling_12_month_revenue
FROM orders o
JOIN order_details od ON o.order_id = od.order_id
WHERE o.order_date BETWEEN '2020-01-01' AND '2024-12-31'
    AND o.status NOT IN ('Cancelled', 'Refunded')
GROUP BY 
    DATE_TRUNC('month', o.order_date),
    EXTRACT(YEAR FROM o.order_date),
    EXTRACT(QUARTER FROM o.order_date),
    EXTRACT(WEEK FROM o.order_date),
    EXTRACT(DOW FROM o.order_date)
ORDER BY order_month DESC
)"},

    {"16. Complex NULL Handling with COALESCE", R"(
SELECT 
    e.employee_id,
    COALESCE(e.preferred_name, e.first_name, 'Unknown') AS display_name,
    COALESCE(e.work_email, e.personal_email, e.backup_email, 'no-email@company.com') AS contact_email,
    COALESCE(e.mobile_phone, e.work_phone, e.home_phone, 'No Phone') AS primary_phone,
    COALESCE(e.salary, 
        (SELECT AVG(salary) FROM employees WHERE department_id = e.department_id),
        (SELECT AVG(salary) FROM employees),
        50000
    ) AS effective_salary,
    COALESCE(
        e.commission_pct * e.salary,
        CASE 
            WHEN e.department = 'Sales' THEN e.salary * 0.10
            WHEN e.department = 'Marketing' THEN e.salary * 0.05
            ELSE 0
        END
    ) AS commission_amount,
    COALESCE(e.manager_id, 
        (SELECT employee_id FROM employees WHERE title = 'CEO' LIMIT 1)
    ) AS reporting_to,
    NULLIF(e.bonus, 0) AS actual_bonus,
    GREATEST(
        COALESCE(e.salary, 0),
        COALESCE(e.hourly_rate * 2080, 0),
        COALESCE(e.contract_value / 12, 0)
    ) AS max_compensation,
    LEAST(
        COALESCE(e.years_experience, 99),
        COALESCE(EXTRACT(YEAR FROM AGE(CURRENT_DATE, e.hire_date)), 0)
    ) AS min_experience
FROM employees e
WHERE COALESCE(e.termination_date, CURRENT_DATE + 1) > CURRENT_DATE
    AND NULLIF(TRIM(e.department), '') IS NOT NULL
ORDER BY effective_salary DESC
)"},

    {"17. Advanced Grouping with CUBE and ROLLUP", R"(
SELECT 
    COALESCE(p.category, 'All Categories') AS category,
    COALESCE(p.subcategory, 'All Subcategories') AS subcategory,
    COALESCE(s.country, 'All Countries') AS supplier_country,
    COALESCE(EXTRACT(YEAR FROM o.order_date)::TEXT, 'All Years') AS order_year,
    COUNT(DISTINCT o.order_id) AS order_count,
    COUNT(DISTINCT c.customer_id) AS unique_customers,
    SUM(od.quantity) AS total_quantity,
    SUM(od.quantity * od.unit_price) AS total_revenue,
    AVG(od.quantity * od.unit_price) AS avg_order_value,
    MIN(od.unit_price) AS min_price,
    MAX(od.unit_price) AS max_price,
    GROUPING(p.category) AS is_category_total,
    GROUPING(p.subcategory) AS is_subcategory_total,
    GROUPING(s.country) AS is_country_total,
    GROUPING(EXTRACT(YEAR FROM o.order_date)) AS is_year_total
FROM orders o
JOIN order_details od ON o.order_id = od.order_id
JOIN products p ON od.product_id = p.product_id
JOIN suppliers s ON p.supplier_id = s.supplier_id
JOIN customers c ON o.customer_id = c.customer_id
WHERE o.order_date BETWEEN '2023-01-01' AND '2024-12-31'
GROUP BY ROLLUP(
    EXTRACT(YEAR FROM o.order_date),
    p.category,
    p.subcategory,
    s.country
)
HAVING SUM(od.quantity * od.unit_price) > 1000
ORDER BY 
    GROUPING(EXTRACT(YEAR FROM o.order_date)),
    GROUPING(p.category),
    GROUPING(p.subcategory),
    GROUPING(s.country),
    total_revenue DESC
)"},

    {"18. Complex HAVING with Subqueries", R"(
SELECT 
    c.customer_id,
    c.customer_name,
    c.country,
    COUNT(DISTINCT o.order_id) AS order_count,
    COUNT(DISTINCT p.category) AS unique_categories_ordered,
    SUM(od.quantity * od.unit_price) AS total_spent,
    AVG(od.quantity * od.unit_price) AS avg_order_value,
    MIN(o.order_date) AS first_order_date,
    MAX(o.order_date) AS last_order_date,
    MAX(o.order_date) - MIN(o.order_date) AS customer_lifetime_days
FROM customers c
JOIN orders o ON c.customer_id = o.customer_id
JOIN order_details od ON o.order_id = od.order_id
JOIN products p ON od.product_id = p.product_id
WHERE o.status IN ('Completed', 'Shipped', 'Delivered')
GROUP BY c.customer_id, c.customer_name, c.country
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
    AND COUNT(DISTINCT p.category) > (
        SELECT AVG(category_count)
        FROM (
            SELECT COUNT(DISTINCT p2.category) AS category_count
            FROM customers c3
            JOIN orders o3 ON c3.customer_id = o3.customer_id
            JOIN order_details od3 ON o3.order_id = od3.order_id
            JOIN products p2 ON od3.product_id = p2.product_id
            GROUP BY c3.customer_id
        ) category_counts
    )
    AND MAX(o.order_date) >= CURRENT_DATE - 180
ORDER BY total_spent DESC
LIMIT 100
)"},

    {"19. Multi-level Window Functions", R"(
SELECT 
    sub.*,
    SUM(monthly_revenue) OVER (
        PARTITION BY customer_id 
        ORDER BY order_month 
        ROWS BETWEEN 2 PRECEDING AND CURRENT ROW
    ) AS three_month_rolling_revenue,
    AVG(monthly_revenue) OVER (
        PARTITION BY customer_id 
        ORDER BY order_month 
        ROWS BETWEEN 5 PRECEDING AND CURRENT ROW
    ) AS six_month_avg_revenue,
    monthly_revenue / NULLIF(
        FIRST_VALUE(monthly_revenue) OVER (
            PARTITION BY customer_id 
            ORDER BY order_month
        ), 0
    ) AS revenue_index_vs_first_month,
    RANK() OVER (
        PARTITION BY order_month 
        ORDER BY monthly_revenue DESC
    ) AS monthly_customer_rank,
    PERCENT_RANK() OVER (
        PARTITION BY customer_id 
        ORDER BY monthly_revenue
    ) AS customer_month_percentile
FROM (
    SELECT 
        c.customer_id,
        c.customer_name,
        DATE_TRUNC('month', o.order_date) AS order_month,
        COUNT(DISTINCT o.order_id) AS monthly_orders,
        SUM(od.quantity * od.unit_price) AS monthly_revenue,
        AVG(od.quantity * od.unit_price) AS avg_order_value,
        ROW_NUMBER() OVER (
            PARTITION BY c.customer_id 
            ORDER BY DATE_TRUNC('month', o.order_date)
        ) AS customer_month_sequence,
        LAG(SUM(od.quantity * od.unit_price), 1, 0) OVER (
            PARTITION BY c.customer_id 
            ORDER BY DATE_TRUNC('month', o.order_date)
        ) AS prev_month_revenue,
        LEAD(SUM(od.quantity * od.unit_price), 1, 0) OVER (
            PARTITION BY c.customer_id 
            ORDER BY DATE_TRUNC('month', o.order_date)
        ) AS next_month_revenue
    FROM customers c
    JOIN orders o ON c.customer_id = o.customer_id
    JOIN order_details od ON o.order_id = od.order_id
    WHERE o.order_date >= '2023-01-01'
    GROUP BY c.customer_id, c.customer_name, DATE_TRUNC('month', o.order_date)
) sub
WHERE monthly_revenue > 1000
ORDER BY customer_id, order_month
)"},

    {"20. Ultimate Complex Query - Everything Combined", R"(
WITH RECURSIVE category_hierarchy AS (
    SELECT 
        category_id,
        parent_category_id,
        category_name,
        0 AS level,
        CAST(category_id AS VARCHAR) AS path
    FROM categories
    WHERE parent_category_id IS NULL
    
    UNION ALL
    
    SELECT 
        c.category_id,
        c.parent_category_id,
        c.category_name,
        ch.level + 1,
        ch.path || '/' || CAST(c.category_id AS VARCHAR)
    FROM categories c
    JOIN category_hierarchy ch ON c.parent_category_id = ch.category_id
),
customer_segments AS (
    SELECT 
        c.customer_id,
        CASE
            WHEN total_value > 100000 AND order_count > 50 THEN 'Platinum'
            WHEN total_value > 50000 AND order_count > 25 THEN 'Gold'
            WHEN total_value > 10000 AND order_count > 10 THEN 'Silver'
            ELSE 'Bronze'
        END AS segment,
        total_value,
        order_count,
        avg_order_value,
        last_order_date
    FROM (
        SELECT 
            o.customer_id,
            SUM(od.quantity * od.unit_price) AS total_value,
            COUNT(DISTINCT o.order_id) AS order_count,
            AVG(od.quantity * od.unit_price) AS avg_order_value,
            MAX(o.order_date) AS last_order_date
        FROM orders o
        JOIN order_details od ON o.order_id = od.order_id
        WHERE o.order_date >= CURRENT_DATE - 730
        GROUP BY o.customer_id
    ) customer_totals
    JOIN customers c ON customer_totals.customer_id = c.customer_id
),
product_performance AS (
    SELECT 
        p.product_id,
        p.product_name,
        ch.category_name,
        ch.level AS category_level,
        ch.path AS category_path,
        SUM(od.quantity) AS total_quantity_sold,
        SUM(od.quantity * od.unit_price) AS total_revenue,
        COUNT(DISTINCT o.customer_id) AS unique_customers,
        AVG(od.quantity * od.unit_price) AS avg_order_value,
        STDDEV(od.quantity) AS quantity_stddev,
        ROW_NUMBER() OVER (
            PARTITION BY ch.category_id 
            ORDER BY SUM(od.quantity * od.unit_price) DESC
        ) AS category_rank,
        PERCENT_RANK() OVER (
            ORDER BY SUM(od.quantity * od.unit_price)
        ) AS overall_revenue_percentile,
        SUM(SUM(od.quantity * od.unit_price)) OVER (
            PARTITION BY ch.category_id 
            ORDER BY SUM(od.quantity * od.unit_price) DESC
            ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
        ) / SUM(SUM(od.quantity * od.unit_price)) OVER (
            PARTITION BY ch.category_id
        ) AS category_revenue_cumulative_pct
    FROM products p
    JOIN category_hierarchy ch ON p.category_id = ch.category_id
    JOIN order_details od ON p.product_id = od.product_id
    JOIN orders o ON od.order_id = o.order_id
    WHERE o.order_date >= CURRENT_DATE - 365
    GROUP BY p.product_id, p.product_name, ch.category_id, ch.category_name, ch.level, ch.path
)
SELECT 
    cs.segment AS customer_segment,
    pp.category_name,
    pp.product_name,
    c.customer_name,
    c.country,
    o.order_id,
    o.order_date,
    od.quantity,
    od.unit_price,
    od.quantity * od.unit_price AS line_total,
    pp.category_rank,
    pp.overall_revenue_percentile,
    pp.category_revenue_cumulative_pct,
    cs.total_value AS customer_lifetime_value,
    CASE
        WHEN pp.category_rank <= 3 AND cs.segment IN ('Platinum', 'Gold') THEN 'Premium Match'
        WHEN pp.category_rank > 10 AND cs.segment = 'Platinum' THEN 'Upsell Opportunity'
        WHEN pp.overall_revenue_percentile > 0.8 AND cs.segment = 'Bronze' THEN 'Growth Opportunity'
        ELSE 'Standard'
    END AS opportunity_type,
    ROW_NUMBER() OVER (
        PARTITION BY cs.segment, pp.category_name 
        ORDER BY od.quantity * od.unit_price DESC
    ) AS segment_category_rank,
    SUM(od.quantity * od.unit_price) OVER (
        PARTITION BY c.customer_id 
        ORDER BY o.order_date 
        ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
    ) AS customer_running_total,
    DENSE_RANK() OVER (
        ORDER BY o.order_date DESC
    ) AS recency_rank,
    COUNT(*) OVER (
        PARTITION BY c.country, pp.category_name
    ) AS country_category_orders
FROM orders o
JOIN customers c ON o.customer_id = c.customer_id
JOIN customer_segments cs ON c.customer_id = cs.customer_id
JOIN order_details od ON o.order_id = od.order_id
JOIN product_performance pp ON od.product_id = pp.product_id
WHERE o.order_date >= CURRENT_DATE - 90
    AND o.status IN ('Completed', 'Shipped')
    AND pp.category_rank <= 20
    AND cs.segment IN ('Platinum', 'Gold', 'Silver')
    AND EXISTS (
        SELECT 1
        FROM orders o2
        WHERE o2.customer_id = c.customer_id
            AND o2.order_date >= CURRENT_DATE - 30
    )
ORDER BY 
    CASE cs.segment
        WHEN 'Platinum' THEN 1
        WHEN 'Gold' THEN 2
        WHEN 'Silver' THEN 3
        ELSE 4
    END,
    pp.category_revenue_cumulative_pct,
    o.order_date DESC,
    line_total DESC
LIMIT 1000
)"}
};

int main() {
    Parser parser;
    ColoredASTDumper dumper;
    
    std::cout << colors::BOLD << colors::STMT 
              << "\n╔════════════════════════════════════════════════════════════════╗\n"
              << "║         DB25 Parser - Complex Query AST Visualization         ║\n"
              << "╚════════════════════════════════════════════════════════════════╝\n"
              << colors::RESET << std::endl;
    
    int successful = 0;
    int failed = 0;
    
    for (size_t i = 0; i < complex_queries.size() && i < 20; ++i) {
        const auto& [title, sql] = complex_queries[i];
        
        std::cout << colors::BOLD << colors::KEYWORD 
                  << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
                  << "\n" << title 
                  << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
                  << colors::RESET << std::endl;
        
        auto result = parser.parse(sql);
        
        if (result.has_value()) {
            successful++;
            dumper.dump(result.value());
            
            // Print some statistics
            std::cout << colors::ID << "Memory used: " 
                      << parser.get_memory_used() << " bytes, "
                      << "Nodes created: " << parser.get_node_count() 
                      << colors::RESET << std::endl;
        } else {
            failed++;
            auto error = result.error();
            std::cout << colors::LITERAL << "✗ PARSE ERROR at line " 
                      << error.line << ", column " << error.column 
                      << ": " << error.message 
                      << colors::RESET << std::endl;
        }
        
        parser.reset(); // Reset for next query
    }
    
    // Final summary
    std::cout << colors::BOLD << colors::STMT 
              << "\n╔════════════════════════════════════════════════════════════════╗\n"
              << "║                          SUMMARY                              ║\n"
              << "╠════════════════════════════════════════════════════════════════╣\n"
              << "║ " << colors::FUNC << "✓ Successful: " << std::setw(2) << successful 
              << colors::STMT << "                                            ║\n"
              << "║ " << colors::LITERAL << "✗ Failed:     " << std::setw(2) << failed 
              << colors::STMT << "                                            ║\n"
              << "╚════════════════════════════════════════════════════════════════╝\n"
              << colors::RESET << std::endl;
    
    return failed > 0 ? 1 : 0;
}