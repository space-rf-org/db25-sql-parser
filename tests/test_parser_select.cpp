/*
 * DB25 Parser - SELECT Statement Tests
 * Testing proper parsing of SELECT clauses and AST structure
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class SelectParserTest : public ::testing::Test {
protected:
    Parser parser;
    
    void SetUp() override {
        parser.reset();
    }
    
    ASTNode* parse_select(const std::string& sql) {
        auto result = parser.parse(sql);
        if (!result.has_value()) {
            return nullptr;
        }
        return result.value();
    }
    
    // Helper to find a child node by type
    ASTNode* find_child_by_type(ASTNode* parent, NodeType type) {
        if (!parent) return nullptr;
        
        ASTNode* child = parent->get_first_child();
        while (child) {
            if (child->node_type == type) {
                return child;
            }
            child = child->get_next_sibling();
        }
        return nullptr;
    }
    
    // Count children
    int count_children(ASTNode* parent) {
        if (!parent) return 0;
        
        int count = 0;
        ASTNode* child = parent->get_first_child();
        while (child) {
            count++;
            child = child->get_next_sibling();
        }
        return count;
    }
};

// ============================================================================
// BASIC STRUCTURE TESTS
// ============================================================================

TEST_F(SelectParserTest, SelectStarHasCorrectStructure) {
    auto* ast = parse_select("SELECT * FROM users");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::SelectStmt);
    
    // Should have at least 2 children: select_list and from_clause
    EXPECT_GE(count_children(ast), 2);
    
    // Find SELECT list
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr) << "SELECT statement should have SelectList child";
    
    // Find FROM clause
    auto* from_clause = find_child_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr) << "SELECT statement should have FromClause child";
}

// ============================================================================
// QUALIFIED STAR TESTS  (table.* -> Star node with schema_name == qualifier)
// ============================================================================

TEST_F(SelectParserTest, QualifiedStarCarriesQualifier) {
    auto* ast = parse_select("SELECT t.* FROM t");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::SelectStmt);

    // SELECT list must contain a single Star whose schema_name holds "t".
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    EXPECT_EQ(count_children(select_list), 1);

    auto* star = find_child_by_type(select_list, NodeType::Star);
    ASSERT_NE(star, nullptr) << "t.* should parse into a Star node";
    EXPECT_EQ(star->schema_name, "t") << "qualifier 't' must land in schema_name";

    // The FROM clause must still be present and reference table "t".
    auto* from_clause = find_child_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr) << "FROM clause must survive qualified star";
    ASSERT_NE(from_clause->get_first_child(), nullptr);
    EXPECT_EQ(from_clause->get_first_child()->primary_text, "t");
}

TEST_F(SelectParserTest, UnqualifiedStarHasEmptyQualifier) {
    auto* ast = parse_select("SELECT * FROM t");
    ASSERT_NE(ast, nullptr);

    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);

    auto* star = find_child_by_type(select_list, NodeType::Star);
    ASSERT_NE(star, nullptr);
    EXPECT_TRUE(star->schema_name.empty())
        << "plain SELECT * must keep an empty schema_name";

    auto* from_clause = find_child_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr);
}

TEST_F(SelectParserTest, MixedQualifiedStarAndColumn) {
    auto* ast = parse_select("SELECT a.*, b.id FROM a JOIN b ON a.id = b.id");
    ASSERT_NE(ast, nullptr);

    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    ASSERT_EQ(count_children(select_list), 2);

    // First item: a.* -> Star with schema_name "a".
    auto* first = select_list->get_first_child();
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->node_type, NodeType::Star);
    EXPECT_EQ(first->schema_name, "a");

    // Second item: b.id -> ColumnRef, not a Star.
    auto* second = first->get_next_sibling();
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->node_type, NodeType::ColumnRef);
    EXPECT_EQ(second->primary_text, "b.id");

    // FROM clause still parses (the JOIN did not get swallowed).
    auto* from_clause = find_child_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr);
}

TEST_F(SelectParserTest, MultiPartQualifiedStar) {
    // schema.table.* -> Star whose schema_name is the full dotted qualifier.
    auto* ast = parse_select("SELECT s.t.* FROM s.t");
    ASSERT_NE(ast, nullptr);

    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);

    auto* star = find_child_by_type(select_list, NodeType::Star);
    ASSERT_NE(star, nullptr);
    EXPECT_EQ(star->schema_name, "s.t");

    auto* from_clause = find_child_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr);
}

// ----------------------------------------------------------------------------
// QUALIFIED STAR - PRODUCTION-SHAPED REGRESSIONS
//
// These lock in the behaviour the semantic analyzer's expand_star relies on
// for the common production pattern `SELECT o.*, c.name FROM orders o ...`:
//   * `<alias>.*` must become a Star whose qualifier lands in schema_name;
//   * the FROM clause (and its table alias) must survive intact;
//   * a `<alias>.*` immediately followed by `, <column>` must not swallow the
//     comma or the following column;
//   * the arithmetic `a * b` must remain a multiplication BinaryExpr and must
//     never be mistaken for a qualified star.
// ----------------------------------------------------------------------------

TEST_F(SelectParserTest, QualifiedStarWithTableAlias) {
    // The canonical production case: alias.* with an aliased table in FROM.
    auto* ast = parse_select("SELECT o.* FROM orders o");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::SelectStmt);

    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    EXPECT_EQ(count_children(select_list), 1);

    auto* star = find_child_by_type(select_list, NodeType::Star);
    ASSERT_NE(star, nullptr) << "o.* must parse into a Star node, not a multiply";
    EXPECT_EQ(star->schema_name, "o") << "alias 'o' must land in schema_name";

    // FROM clause must survive and still reference the aliased table.
    auto* from_clause = find_child_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr) << "FROM clause must not be dropped";
    auto* table_ref = from_clause->get_first_child();
    ASSERT_NE(table_ref, nullptr);
    EXPECT_EQ(table_ref->node_type, NodeType::TableRef);
    EXPECT_EQ(table_ref->primary_text, "orders");
}

TEST_F(SelectParserTest, QualifiedStarThenColumn) {
    // `t.*, name` -> Star(schema_name="t") followed by a plain column ref.
    auto* ast = parse_select("SELECT t.*, name FROM t");
    ASSERT_NE(ast, nullptr);

    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    ASSERT_EQ(count_children(select_list), 2)
        << "the comma and following column must not be swallowed";

    auto* first = select_list->get_first_child();
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->node_type, NodeType::Star);
    EXPECT_EQ(first->schema_name, "t");

    auto* second = first->get_next_sibling();
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->node_type, NodeType::ColumnRef);
    EXPECT_EQ(second->primary_text, "name");

    auto* from_clause = find_child_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr);
}

TEST_F(SelectParserTest, PlainStarStaysUnqualified) {
    // Guard the plain `*` case alongside the qualified variants.
    auto* ast = parse_select("SELECT * FROM t");
    ASSERT_NE(ast, nullptr);

    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);

    auto* star = find_child_by_type(select_list, NodeType::Star);
    ASSERT_NE(star, nullptr);
    EXPECT_TRUE(star->schema_name.empty())
        << "plain '*' must carry no qualifier";
}

TEST_F(SelectParserTest, StarMultiplicationIsNotQualifiedStar) {
    // `a * b` must remain an arithmetic multiplication, never a Star.
    auto* ast = parse_select("SELECT a * b FROM t");
    ASSERT_NE(ast, nullptr);

    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    ASSERT_EQ(count_children(select_list), 1);

    auto* item = select_list->get_first_child();
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->node_type, NodeType::BinaryExpr)
        << "a * b must parse as multiplication, not a Star";
    EXPECT_EQ(item->primary_text, "*");

    // The two operands are column references a and b.
    auto* lhs = item->get_first_child();
    ASSERT_NE(lhs, nullptr);
    auto* rhs = lhs->get_next_sibling();
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(lhs->primary_text, "a");
    EXPECT_EQ(rhs->primary_text, "b");

    // No Star anywhere in the SELECT list.
    EXPECT_EQ(find_child_by_type(select_list, NodeType::Star), nullptr);
}

TEST_F(SelectParserTest, SelectListWithColumns) {
    auto* ast = parse_select("SELECT id, name, email FROM users");
    auto children = ast->get_children();

    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    // Should have 3 column references
    int column_count = count_children(select_list);
    EXPECT_EQ(column_count, 3) << "SELECT list should have 3 columns";
}

TEST_F(SelectParserTest, FromClauseWithTable) {
    auto* ast = parse_select("SELECT * FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* from_clause = find_child_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr);
    
    // Should have table reference
    auto* table_ref = find_child_by_type(from_clause, NodeType::TableRef);
    ASSERT_NE(table_ref, nullptr) << "FROM clause should have TableRef child";
    
    // Check table name is stored
    EXPECT_FALSE(table_ref->primary_text.empty());
    EXPECT_EQ(table_ref->primary_text, "users");
}

// ============================================================================
// WHERE CLAUSE TESTS
// ============================================================================

TEST_F(SelectParserTest, WhereClausePresent) {
    auto* ast = parse_select("SELECT * FROM users WHERE id = 1");
    ASSERT_NE(ast, nullptr);
    
    auto* where_clause = find_child_by_type(ast, NodeType::WhereClause);
    ASSERT_NE(where_clause, nullptr) << "Should have WHERE clause";
    
    // WHERE clause should have a condition expression
    EXPECT_GT(count_children(where_clause), 0) << "WHERE clause should have condition";
}

TEST_F(SelectParserTest, WhereWithBinaryExpression) {
    auto* ast = parse_select("SELECT * FROM users WHERE age > 18");
    ASSERT_NE(ast, nullptr);
    
    auto* where_clause = find_child_by_type(ast, NodeType::WhereClause);
    ASSERT_NE(where_clause, nullptr);
    
    // Should have binary expression as child
    auto* binary_expr = find_child_by_type(where_clause, NodeType::BinaryExpr);
    ASSERT_NE(binary_expr, nullptr) << "WHERE should contain binary expression";
}

// ============================================================================
// ORDER BY TESTS
// ============================================================================

TEST_F(SelectParserTest, OrderByClause) {
    auto* ast = parse_select("SELECT * FROM users ORDER BY name");
    ASSERT_NE(ast, nullptr);
    
    auto* order_by = find_child_by_type(ast, NodeType::OrderByClause);
    ASSERT_NE(order_by, nullptr) << "Should have ORDER BY clause";
    
    // Should have at least one order item
    EXPECT_GT(count_children(order_by), 0);
}

TEST_F(SelectParserTest, OrderByWithDirection) {
    auto* ast = parse_select("SELECT * FROM users ORDER BY name DESC");
    ASSERT_NE(ast, nullptr);
    
    auto* order_by = find_child_by_type(ast, NodeType::OrderByClause);
    ASSERT_NE(order_by, nullptr);
    
    // Should handle DESC keyword
    // The order item should have a flag or child indicating DESC
}

// ============================================================================
// LIMIT TESTS
// ============================================================================

TEST_F(SelectParserTest, LimitClause) {
    auto* ast = parse_select("SELECT * FROM users LIMIT 10");
    ASSERT_NE(ast, nullptr);
    
    auto* limit = find_child_by_type(ast, NodeType::LimitClause);
    ASSERT_NE(limit, nullptr) << "Should have LIMIT clause";
    
    // Should have a number literal as child
    auto* number = find_child_by_type(limit, NodeType::IntegerLiteral);
    ASSERT_NE(number, nullptr) << "LIMIT should have integer value";
}

TEST_F(SelectParserTest, LimitWithOffset) {
    auto* ast = parse_select("SELECT * FROM users LIMIT 10 OFFSET 20");
    ASSERT_NE(ast, nullptr);
    
    auto* limit = find_child_by_type(ast, NodeType::LimitClause);
    ASSERT_NE(limit, nullptr);
    
    // Should handle both LIMIT and OFFSET
    EXPECT_EQ(count_children(limit), 2) << "Should have limit and offset values";
}

// ============================================================================
// IDENTIFIER TESTS
// ============================================================================

TEST_F(SelectParserTest, ColumnIdentifiers) {
    auto* ast = parse_select("SELECT first_name, last_name FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    // Check first column
    auto* first_col = select_list->get_first_child();
    ASSERT_NE(first_col, nullptr);
    EXPECT_EQ(first_col->node_type, NodeType::ColumnRef);
    EXPECT_EQ(first_col->primary_text, "first_name");
    
    // Check second column
    auto* second_col = first_col->get_next_sibling();
    ASSERT_NE(second_col, nullptr);
    EXPECT_EQ(second_col->node_type, NodeType::ColumnRef);
    EXPECT_EQ(second_col->primary_text, "last_name");
}

TEST_F(SelectParserTest, QualifiedColumnNames) {
    auto* ast = parse_select("SELECT users.id, users.name FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    auto* first_col = select_list->get_first_child();
    ASSERT_NE(first_col, nullptr);
    
    // Should handle table.column notation
    EXPECT_EQ(first_col->node_type, NodeType::ColumnRef);
    // Should store both table and column parts
}

// ============================================================================
// EXPRESSION TESTS
// ============================================================================

TEST_F(SelectParserTest, ExpressionInSelectList) {
    auto* ast = parse_select("SELECT id + 1, name FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    // First item should be an expression
    auto* first_item = select_list->get_first_child();
    ASSERT_NE(first_item, nullptr);
    EXPECT_EQ(first_item->node_type, NodeType::BinaryExpr);
}

TEST_F(SelectParserTest, FunctionCallInSelect) {
    auto* ast = parse_select("SELECT COUNT(*) FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    auto* func_call = find_child_by_type(select_list, NodeType::FunctionCall);
    ASSERT_NE(func_call, nullptr) << "Should parse function call";
    EXPECT_EQ(func_call->primary_text, "COUNT");
}

// ============================================================================
// COMPLEX QUERIES
// ============================================================================

TEST_F(SelectParserTest, CompleteSelectStatement) {
    auto* ast = parse_select(
        "SELECT id, name "
        "FROM users "
        "WHERE age > 18 "
        "ORDER BY name DESC "
        "LIMIT 10"
    );
    ASSERT_NE(ast, nullptr);
    
    // Should have all clauses
    EXPECT_NE(find_child_by_type(ast, NodeType::SelectList), nullptr);
    EXPECT_NE(find_child_by_type(ast, NodeType::FromClause), nullptr);
    EXPECT_NE(find_child_by_type(ast, NodeType::WhereClause), nullptr);
    EXPECT_NE(find_child_by_type(ast, NodeType::OrderByClause), nullptr);
    EXPECT_NE(find_child_by_type(ast, NodeType::LimitClause), nullptr);
}

// ============================================================================
// ALIAS TESTS
// ============================================================================

TEST_F(SelectParserTest, ColumnAlias) {
    auto* ast = parse_select("SELECT id AS user_id FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    auto* alias_expr = find_child_by_type(select_list, NodeType::AliasRef);
    if (!alias_expr) {
        // Might be stored differently
        auto* first_item = select_list->get_first_child();
        ASSERT_NE(first_item, nullptr);
        // Check if alias is stored in the node
    }
}

TEST_F(SelectParserTest, TableAlias) {
    auto* ast = parse_select("SELECT u.id FROM users AS u");
    ASSERT_NE(ast, nullptr);
    
    auto* from_clause = find_child_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from_clause, nullptr);
    
    // Should handle table alias
}

// ============================================================================
// STAR EXPANSION
// ============================================================================

TEST_F(SelectParserTest, SelectStar) {
    auto* ast = parse_select("SELECT * FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    auto* star = find_child_by_type(select_list, NodeType::Star);
    ASSERT_NE(star, nullptr) << "Should have Star node for *";
}

TEST_F(SelectParserTest, QualifiedStar) {
    auto* ast = parse_select("SELECT users.* FROM users");
    ASSERT_NE(ast, nullptr);
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    auto* star = find_child_by_type(select_list, NodeType::Star);
    if (star) {
        // Should have table qualifier
        EXPECT_FALSE(star->schema_name.empty() || star->primary_text.empty());
    }
}

// ============================================================================
// DISTINCT TESTS
// ============================================================================

TEST_F(SelectParserTest, DistinctSimple) {
    auto* ast = parse_select("SELECT DISTINCT name FROM users");
    ASSERT_NE(ast, nullptr);
    
    // SELECT statement should have DISTINCT flag
    EXPECT_TRUE(static_cast<bool>(ast->semantic_flags & static_cast<uint16_t>(NodeFlags::Distinct)));
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    // Should still parse the column
    auto* column = find_child_by_type(select_list, NodeType::ColumnRef);
    ASSERT_NE(column, nullptr);
    EXPECT_EQ(column->primary_text, "name");
}

TEST_F(SelectParserTest, DistinctMultipleColumns) {
    auto* ast = parse_select("SELECT DISTINCT department, job_title FROM employees");
    ASSERT_NE(ast, nullptr);
    
    EXPECT_TRUE(static_cast<bool>(ast->semantic_flags & static_cast<uint16_t>(NodeFlags::Distinct)));
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    EXPECT_EQ(select_list->child_count, 2);
}

TEST_F(SelectParserTest, DistinctWithStar) {
    auto* ast = parse_select("SELECT DISTINCT * FROM users");
    ASSERT_NE(ast, nullptr);
    
    EXPECT_TRUE(static_cast<bool>(ast->semantic_flags & static_cast<uint16_t>(NodeFlags::Distinct)));
    
    auto* select_list = find_child_by_type(ast, NodeType::SelectList);
    ASSERT_NE(select_list, nullptr);
    
    auto* star = find_child_by_type(select_list, NodeType::Star);
    ASSERT_NE(star, nullptr);
}
// ===== Parenthesized JOIN group in FROM position =====
// Regression: `FROM (a JOIN b ON ...) JOIN c ON ...` previously dropped the
// whole FROM clause (parse_table_reference did not recognise a parenthesized
// join group, so parse_from_clause returned nullptr with FROM consumed and the
// remainder of the statement became trailing input).

namespace {
// Recursively count nodes of a given type anywhere in the subtree.
int count_nodes_of_type(ASTNode* n, NodeType t) {
    if (!n) return 0;
    int c = (n->node_type == t) ? 1 : 0;
    for (auto* ch = n->first_child; ch; ch = ch->next_sibling) {
        c += count_nodes_of_type(ch, t);
    }
    return c;
}
}  // namespace

TEST_F(SelectParserTest, ParenthesizedJoinGroupInFrom) {
    auto result = parser.parse(
        "SELECT * FROM (a JOIN b ON a.id=b.id) JOIN c ON a.id=c.id");
    ASSERT_TRUE(result.has_value()) << "parenthesized join group should parse";

    // No input may be dropped: the entire FROM clause must be consumed.
    EXPECT_FALSE(parser.has_trailing_input());
    EXPECT_EQ(parser.trailing_token_count(), 0u);

    auto* ast = result.value();
    auto* from = find_child_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from, nullptr) << "SELECT must have a FROM clause";

    // The FROM clause holds the parenthesized group (itself a nested join tree)
    // plus the trailing `JOIN c`. Overall there must be two JoinClause nodes:
    // the inner `JOIN b` and the outer `JOIN c`.
    EXPECT_EQ(count_nodes_of_type(from, NodeType::JoinClause), 2);

    // Three base tables (a, b, c) are referenced.
    EXPECT_EQ(count_nodes_of_type(from, NodeType::TableRef), 3);

    // The nested group is represented as a FromClause nested inside the outer
    // FROM clause, containing table `a` and the `JOIN b` clause.
    auto* nested = find_child_by_type(from, NodeType::FromClause);
    ASSERT_NE(nested, nullptr) << "nested join group should be present";
    EXPECT_EQ(count_nodes_of_type(nested, NodeType::JoinClause), 1);
    EXPECT_NE(find_child_by_type(nested, NodeType::TableRef), nullptr);
}

TEST_F(SelectParserTest, DerivedTableStillParsesAsSubquery) {
    // Control: a `( SELECT ... ) alias` must remain a derived table (Subquery),
    // not be mistaken for a parenthesized join group.
    auto result = parser.parse("SELECT * FROM (SELECT 1) x");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(parser.has_trailing_input());

    auto* ast = result.value();
    auto* from = find_child_by_type(ast, NodeType::FromClause);
    ASSERT_NE(from, nullptr);

    auto* subquery = find_child_by_type(from, NodeType::Subquery);
    ASSERT_NE(subquery, nullptr) << "derived table must parse as a Subquery";
    EXPECT_EQ(subquery->schema_name, "x");  // alias preserved
}

TEST_F(SelectParserTest, DerivedTableColumnAliasListCaptured) {
    // "( SELECT ... ) AS t (a, b)" renames the derived table's output columns.
    // The list must be CONSUMED (no trailing input) and CAPTURED as a ColumnList
    // of Identifier nodes under the Subquery - previously it was silently dropped.
    auto result = parser.parse("SELECT * FROM (SELECT 1) AS t(a, b)");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(parser.has_trailing_input()) << "column-alias list must be consumed";

    auto* from = find_child_by_type(result.value(), NodeType::FromClause);
    ASSERT_NE(from, nullptr);
    auto* subquery = find_child_by_type(from, NodeType::Subquery);
    ASSERT_NE(subquery, nullptr);
    EXPECT_EQ(subquery->schema_name, "t");

    auto* col_list = find_child_by_type(subquery, NodeType::ColumnList);
    ASSERT_NE(col_list, nullptr) << "column aliases must be captured, not dropped";
    EXPECT_EQ(count_children(col_list), 2);
    auto* first = col_list->get_first_child();
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->node_type, NodeType::Identifier);
    EXPECT_EQ(first->primary_text, "a");
    ASSERT_NE(first->get_next_sibling(), nullptr);
    EXPECT_EQ(first->get_next_sibling()->primary_text, "b");

    // The inner SELECT must still be reachable as a child (consumers read it).
    EXPECT_NE(find_child_by_type(subquery, NodeType::SelectStmt), nullptr);
}

TEST_F(SelectParserTest, DerivedTableColumnAliasInsideSubqueryParses) {
    // Regression: the same construct nested inside an expression subquery used to
    // fail to parse - the unconsumed "(a)" broke ")" matching for the outer
    // subquery. Both the IN-subquery and scalar-subquery positions must parse.
    auto a = parser.parse("SELECT NULL IN (SELECT * FROM (SELECT 1) AS t(a))");
    EXPECT_TRUE(a.has_value()) << "column-alias list inside an IN-subquery must parse";

    parser.reset();
    auto b = parser.parse("SELECT (SELECT max(x) FROM (SELECT 1) AS t(x))");
    EXPECT_TRUE(b.has_value()) << "column-alias list inside a scalar subquery must parse";
}

TEST_F(SelectParserTest, InsertTargetColumnListNotMistakenForAlias) {
    // Guard: parse_table_reference is shared with the INSERT path. The restriction
    // to derived-table (Subquery) refs must leave an INSERT's target column list
    // "(a, b, c)" for the DML parser, not steal it as an alias list.
    auto r = parser.parse("INSERT INTO tbl (a, b, c) VALUES (1, 2, 3)");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value()->node_type, NodeType::InsertStmt);
    auto* col_list = find_child_by_type(r.value(), NodeType::ColumnList);
    ASSERT_NE(col_list, nullptr) << "INSERT target column list must be preserved";
    EXPECT_EQ(count_children(col_list), 3);
}

TEST_F(SelectParserTest, ValuesDerivedTableInFrom) {
    // "( VALUES (..), (..) ) AS t" is a derived table whose body is a VALUES
    // list. Previously the "(" matched neither a derived table (only SELECT/WITH
    // were recognized) nor a join group, so the ENTIRE FROM clause was silently
    // dropped. It must now parse to FromClause -> Subquery -> ValuesStmt.
    auto result = parser.parse("SELECT * FROM (VALUES (1, 2), (3, 4)) AS t");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(parser.has_trailing_input());

    auto* from = find_child_by_type(result.value(), NodeType::FromClause);
    ASSERT_NE(from, nullptr) << "FROM clause must not be dropped for a VALUES source";

    auto* subquery = find_child_by_type(from, NodeType::Subquery);
    ASSERT_NE(subquery, nullptr) << "VALUES source must parse as a derived table";
    EXPECT_EQ(subquery->schema_name, "t");
    EXPECT_NE(find_child_by_type(subquery, NodeType::ValuesStmt), nullptr)
        << "derived-table body must be a ValuesStmt";
}

TEST_F(SelectParserTest, ValuesDerivedTableInsideSubqueryParses) {
    // Regression: a VALUES derived table nested inside an expression subquery used
    // to fail to parse outright (the unrecognized "(" cascaded into a paren
    // mismatch on the outer subquery).
    auto r = parser.parse("SELECT NULL IN (SELECT * FROM (VALUES (1)) AS t)");
    EXPECT_TRUE(r.has_value()) << "VALUES derived table inside an IN-subquery must parse";
}
