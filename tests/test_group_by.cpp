/*
 * Test GROUP BY and HAVING clauses with aggregate functions
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"
#include "db25/ast/node_types.hpp"
#include <string>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class GroupByTest : public ::testing::Test {
protected:
    std::unique_ptr<Parser> parser;
    
    void SetUp() override {
        parser = std::make_unique<Parser>();
    }
    
    ASTNode* parse(const std::string& sql) {
        auto result = parser->parse(sql);
        return result.has_value() ? result.value() : nullptr;
    }
    
    ASTNode* find_node_by_type(ASTNode* root, NodeType type) {
        if (!root) return nullptr;
        if (root->node_type == type) return root;
        
        auto* child = root->first_child;
        while (child) {
            auto* found = find_node_by_type(child, type);
            if (found) return found;
            child = child->next_sibling;
        }
        return nullptr;
    }
    
    int count_nodes_of_type(ASTNode* root, NodeType type) {
        if (!root) return 0;
        int count = (root->node_type == type) ? 1 : 0;
        
        auto* child = root->first_child;
        while (child) {
            count += count_nodes_of_type(child, type);
            child = child->next_sibling;
        }
        return count;
    }
};

// Test simple GROUP BY with single column
TEST_F(GroupByTest, SimpleGroupBy) {
    auto* ast = parse("SELECT department, COUNT(*) FROM employees GROUP BY department");
    ASSERT_NE(ast, nullptr);
    
    auto* group_by = find_node_by_type(ast, NodeType::GroupByClause);
    ASSERT_NE(group_by, nullptr);
    EXPECT_EQ(group_by->child_count, 1); // One grouping column
    
    // First child should be the column
    auto* column = group_by->first_child;
    ASSERT_NE(column, nullptr);
    EXPECT_EQ(column->node_type, NodeType::ColumnRef);
    EXPECT_EQ(column->primary_text, "department");
}

// Test GROUP BY with multiple columns
TEST_F(GroupByTest, MultipleGroupByColumns) {
    auto* ast = parse("SELECT department, job_title, COUNT(*) FROM employees GROUP BY department, job_title");
    ASSERT_NE(ast, nullptr);
    
    auto* group_by = find_node_by_type(ast, NodeType::GroupByClause);
    ASSERT_NE(group_by, nullptr);
    EXPECT_EQ(group_by->child_count, 2); // Two grouping columns
    
    // First column
    auto* col1 = group_by->first_child;
    ASSERT_NE(col1, nullptr);
    EXPECT_EQ(col1->node_type, NodeType::ColumnRef);
    EXPECT_EQ(col1->primary_text, "department");
    
    // Second column
    auto* col2 = col1->next_sibling;
    ASSERT_NE(col2, nullptr);
    EXPECT_EQ(col2->node_type, NodeType::ColumnRef);
    EXPECT_EQ(col2->primary_text, "job_title");
}

// Test GROUP BY with expression
TEST_F(GroupByTest, GroupByWithExpression) {
    auto* ast = parse("SELECT YEAR(hire_date), COUNT(*) FROM employees GROUP BY YEAR(hire_date)");
    ASSERT_NE(ast, nullptr);
    
    auto* group_by = find_node_by_type(ast, NodeType::GroupByClause);
    ASSERT_NE(group_by, nullptr);
    EXPECT_EQ(group_by->child_count, 1);
    
    // Should have a function call
    auto* func = group_by->first_child;
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->node_type, NodeType::FunctionCall);
    EXPECT_EQ(func->primary_text, "YEAR");
}

// Test GROUP BY with HAVING
TEST_F(GroupByTest, GroupByWithHaving) {
    auto* ast = parse("SELECT department, AVG(salary) FROM employees GROUP BY department HAVING AVG(salary) > 50000");
    ASSERT_NE(ast, nullptr);
    
    // Should have both GROUP BY and HAVING
    auto* group_by = find_node_by_type(ast, NodeType::GroupByClause);
    ASSERT_NE(group_by, nullptr);
    
    auto* having = find_node_by_type(ast, NodeType::HavingClause);
    ASSERT_NE(having, nullptr);
    
    // HAVING should contain a comparison
    auto* comparison = having->first_child;
    ASSERT_NE(comparison, nullptr);
    EXPECT_EQ(comparison->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(comparison->primary_text, ">");
}

// Test HAVING without GROUP BY (should parse but semantic error)
TEST_F(GroupByTest, HavingWithoutGroupBy) {
    auto* ast = parse("SELECT * FROM employees HAVING COUNT(*) > 10");
    ASSERT_NE(ast, nullptr);
    
    // Should still parse HAVING
    auto* having = find_node_by_type(ast, NodeType::HavingClause);
    ASSERT_NE(having, nullptr);
}

// Test aggregate functions
TEST_F(GroupByTest, AggregateFunction_COUNT) {
    auto* ast = parse("SELECT COUNT(*) FROM employees");
    ASSERT_NE(ast, nullptr);
    
    auto* func = find_node_by_type(ast, NodeType::FunctionCall);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->primary_text, "COUNT");
    
    // COUNT(*) should have a Star child
    auto* star = func->first_child;
    ASSERT_NE(star, nullptr);
    EXPECT_EQ(star->node_type, NodeType::Star);
}

TEST_F(GroupByTest, AggregateFunction_SUM) {
    auto* ast = parse("SELECT SUM(salary) FROM employees");
    ASSERT_NE(ast, nullptr);
    
    auto* func = find_node_by_type(ast, NodeType::FunctionCall);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->primary_text, "SUM");
    
    // SUM should have column argument
    auto* arg = func->first_child;
    ASSERT_NE(arg, nullptr);
    EXPECT_EQ(arg->node_type, NodeType::Identifier);
    EXPECT_EQ(arg->primary_text, "salary");
}

TEST_F(GroupByTest, AggregateFunction_AVG) {
    auto* ast = parse("SELECT AVG(salary) FROM employees");
    ASSERT_NE(ast, nullptr);
    
    auto* func = find_node_by_type(ast, NodeType::FunctionCall);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->primary_text, "AVG");
}

TEST_F(GroupByTest, AggregateFunction_MIN) {
    auto* ast = parse("SELECT MIN(salary) FROM employees");
    ASSERT_NE(ast, nullptr);
    
    auto* func = find_node_by_type(ast, NodeType::FunctionCall);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->primary_text, "MIN");
}

TEST_F(GroupByTest, AggregateFunction_MAX) {
    auto* ast = parse("SELECT MAX(salary) FROM employees");
    ASSERT_NE(ast, nullptr);
    
    auto* func = find_node_by_type(ast, NodeType::FunctionCall);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->primary_text, "MAX");
}

// Test multiple aggregates
TEST_F(GroupByTest, MultipleAggregates) {
    auto* ast = parse("SELECT department, COUNT(*), AVG(salary), MAX(salary) FROM employees GROUP BY department");
    ASSERT_NE(ast, nullptr);
    
    // Should have 3 function calls
    int func_count = count_nodes_of_type(ast, NodeType::FunctionCall);
    EXPECT_EQ(func_count, 3);
}

// Test GROUP BY with ORDER BY
TEST_F(GroupByTest, GroupByWithOrderBy) {
    auto* ast = parse("SELECT department, COUNT(*) FROM employees GROUP BY department ORDER BY COUNT(*) DESC");
    ASSERT_NE(ast, nullptr);
    
    // Should have both clauses
    EXPECT_NE(find_node_by_type(ast, NodeType::GroupByClause), nullptr);
    EXPECT_NE(find_node_by_type(ast, NodeType::OrderByClause), nullptr);
}

// Test complex HAVING condition
TEST_F(GroupByTest, ComplexHavingCondition) {
    auto* ast = parse("SELECT department FROM employees GROUP BY department HAVING COUNT(*) > 5 AND AVG(salary) > 50000");
    ASSERT_NE(ast, nullptr);
    
    auto* having = find_node_by_type(ast, NodeType::HavingClause);
    ASSERT_NE(having, nullptr);
    
    // Root of HAVING should be AND
    auto* and_expr = having->first_child;
    ASSERT_NE(and_expr, nullptr);
    EXPECT_EQ(and_expr->node_type, NodeType::BinaryExpr);
    EXPECT_EQ(and_expr->primary_text, "AND");
}

// Test GROUP BY with qualified columns
TEST_F(GroupByTest, GroupByQualifiedColumns) {
    auto* ast = parse("SELECT e.department, COUNT(*) FROM employees e GROUP BY e.department");
    ASSERT_NE(ast, nullptr);
    
    auto* group_by = find_node_by_type(ast, NodeType::GroupByClause);
    ASSERT_NE(group_by, nullptr);
    
    // Should have qualified column
    auto* column = group_by->first_child;
    ASSERT_NE(column, nullptr);
    EXPECT_EQ(column->node_type, NodeType::ColumnRef);
    EXPECT_EQ(column->primary_text, "e.department");
}

// Test GROUP BY position (GROUP BY 1, 2)
TEST_F(GroupByTest, GroupByPosition) {
    auto* ast = parse("SELECT department, job_title FROM employees GROUP BY 1, 2");
    ASSERT_NE(ast, nullptr);
    
    auto* group_by = find_node_by_type(ast, NodeType::GroupByClause);
    ASSERT_NE(group_by, nullptr);
    EXPECT_EQ(group_by->child_count, 2);
    
    // Should have integer literals
    auto* pos1 = group_by->first_child;
    ASSERT_NE(pos1, nullptr);
    EXPECT_EQ(pos1->node_type, NodeType::IntegerLiteral);
    EXPECT_EQ(pos1->primary_text, "1");
    
    auto* pos2 = pos1->next_sibling;
    ASSERT_NE(pos2, nullptr);
    EXPECT_EQ(pos2->node_type, NodeType::IntegerLiteral);
    EXPECT_EQ(pos2->primary_text, "2");
}

// A comma-separated LIST of grouping elements (`CUBE(a), ROLLUP(b)`), or a
// grouping element mixed with plain items (`ROLLUP(a), b` / `a, ROLLUP(b)`), is
// legal SQL but unsupported by this parser (the analyzer/binder do not model
// multi-element grouping-set lists). It must be REJECTED cleanly, never silently
// parsed as a GROUP BY over only the first element. Regression: the ROLLUP / CUBE
// / GROUPING SETS branches returned after one element without running the
// comma-item loop, so every following item was dropped with no diagnostic - a
// coarser grouping that mis-executes the query.
TEST_F(GroupByTest, GroupingSetListRejected) {
    EXPECT_EQ(parse("SELECT a FROM t GROUP BY CUBE(a), ROLLUP(b)"), nullptr);
    EXPECT_EQ(parse("SELECT a FROM t GROUP BY ROLLUP(a), b"), nullptr);
    EXPECT_EQ(parse("SELECT a FROM t GROUP BY CUBE(a), b, c"), nullptr);
    EXPECT_EQ(parse("SELECT a FROM t GROUP BY GROUPING SETS ((a)), b"), nullptr);
    EXPECT_EQ(parse("SELECT a FROM t GROUP BY a, ROLLUP(b)"), nullptr);

    // A single grouping element is still accepted (one GroupingElement child),
    // and ordinary GROUP BY lists are unaffected.
    auto* rollup = parse("SELECT a FROM t GROUP BY ROLLUP(a)");
    ASSERT_NE(rollup, nullptr);
    auto* gb1 = find_node_by_type(rollup, NodeType::GroupByClause);
    ASSERT_NE(gb1, nullptr);
    EXPECT_EQ(gb1->child_count, 1);
    EXPECT_EQ(gb1->first_child->node_type, NodeType::GroupingElement);

    auto* plain = parse("SELECT a, b FROM t GROUP BY a, b");
    ASSERT_NE(plain, nullptr);
    auto* gb2 = find_node_by_type(plain, NodeType::GroupByClause);
    ASSERT_NE(gb2, nullptr);
    EXPECT_EQ(gb2->child_count, 2);
}

// ROLLUP / CUBE / GROUPING (and SETS) are NON-RESERVED keywords: they introduce a
// grouping construct only when ROLLUP/CUBE is immediately followed by '(' or
// GROUPING by SETS. Used as a plain column identifier they must parse as an
// ordinary column reference. Regression: the GROUP BY branches dispatched on the
// bare keyword value with no lookahead, so `GROUP BY rollup` produced an empty
// ROLLUP grouping element (or dropped the whole clause), and `GROUP BY rollup, x`
// was wrongly rejected once the grouping-set-list guard was added.
TEST_F(GroupByTest, GroupingKeywordAsColumnName) {
    // A single keyword-named column groups by that column.
    for (const char* sql : {"SELECT rollup FROM t GROUP BY rollup",
                            "SELECT cube FROM t GROUP BY cube",
                            "SELECT g FROM t GROUP BY grouping"}) {
        auto* ast = parse(sql);
        ASSERT_NE(ast, nullptr) << sql;
        auto* gb = find_node_by_type(ast, NodeType::GroupByClause);
        ASSERT_NE(gb, nullptr) << sql;
        EXPECT_EQ(gb->child_count, 1) << sql;
        ASSERT_NE(gb->first_child, nullptr) << sql;
        EXPECT_NE(gb->first_child->node_type, NodeType::GroupingElement) << sql;
    }

    // A keyword-named column mixed with others is an ordinary multi-column list,
    // in either position.
    auto* first = parse("SELECT rollup, x FROM t GROUP BY rollup, x");
    ASSERT_NE(first, nullptr);
    auto* gbf = find_node_by_type(first, NodeType::GroupByClause);
    ASSERT_NE(gbf, nullptr);
    EXPECT_EQ(gbf->child_count, 2);

    auto* last = parse("SELECT x FROM t GROUP BY x, rollup");
    ASSERT_NE(last, nullptr);
    auto* gbl = find_node_by_type(last, NodeType::GroupByClause);
    ASSERT_NE(gbl, nullptr);
    EXPECT_EQ(gbl->child_count, 2);

    // `GROUP BY rollup + 1` is an expression over the column, not a grouping set.
    auto* expr = parse("SELECT a FROM t GROUP BY rollup + 1");
    ASSERT_NE(expr, nullptr);
    auto* gbe = find_node_by_type(expr, NodeType::GroupByClause);
    ASSERT_NE(gbe, nullptr);
    EXPECT_EQ(gbe->child_count, 1);
    ASSERT_NE(gbe->first_child, nullptr);
    EXPECT_NE(gbe->first_child->node_type, NodeType::GroupingElement);
}

// `GROUP BY ()` is the empty grouping set (the "grand total"): the query groups
// all rows into ONE group. The empty `()` is not a parseable expression, so it
// used to yield a null grouping item and (staying lenient about an empty GROUP
// BY) get dropped entirely - no GroupByClause node at all - which made the
// analyzer see an ungrouped query and accept a non-aggregated column. Emit a
// childless GroupByClause so the query is grouped with zero keys.
TEST_F(GroupByTest, EmptyGroupingSet) {
    auto* ast = parse("SELECT COUNT(*) FROM employees GROUP BY ()");
    ASSERT_NE(ast, nullptr);
    auto* group_by = find_node_by_type(ast, NodeType::GroupByClause);
    ASSERT_NE(group_by, nullptr);  // the clause must NOT be dropped
    EXPECT_EQ(group_by->child_count, 0);       // grand total: no grouping keys
    EXPECT_EQ(group_by->first_child, nullptr);

    // It is emitted regardless of the select list (the analyzer, not the parser,
    // decides whether a bare column is legal under it).
    auto* ast2 = parse("SELECT dept FROM employees GROUP BY ()");
    ASSERT_NE(ast2, nullptr);
    auto* gb2 = find_node_by_type(ast2, NodeType::GroupByClause);
    ASSERT_NE(gb2, nullptr);
    EXPECT_EQ(gb2->child_count, 0);

    // Guard: `GROUP BY (dept)` is a PARENTHESIZED grouping column, NOT the empty
    // set - one key, not zero.
    auto* ast3 = parse("SELECT dept FROM employees GROUP BY (dept)");
    ASSERT_NE(ast3, nullptr);
    auto* gb3 = find_node_by_type(ast3, NodeType::GroupByClause);
    ASSERT_NE(gb3, nullptr);
    EXPECT_EQ(gb3->child_count, 1);
    ASSERT_NE(gb3->first_child, nullptr);
}