/*
 * DB25 Parser - Correctness Round 2 Regression Tests
 *
 * Covers four parser-correctness fixes:
 *   1. Schema-qualified table names (schema.table [alias]) with all trailing
 *      clauses retained (previously a data-loss bug).
 *   2. Trailing-input detection surfaced as a queryable diagnostic.
 *   3. Case-insensitive DML keyword gating (lowercase RETURNING, etc.).
 *   4. INSERT RETURNING and ON CONFLICT wired to the real clause helpers.
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

namespace {

class CorrectnessRound2Test : public ::testing::Test {
protected:
    Parser parser;

    ASTNode* parse(const std::string& sql) {
        auto result = parser.parse(sql);
        return result.has_value() ? result.value() : nullptr;
    }

    static ASTNode* find_child(ASTNode* node, NodeType type) {
        for (auto* c = node ? node->first_child : nullptr; c; c = c->next_sibling) {
            if (c->node_type == type) return c;
        }
        return nullptr;
    }
};

// ============================================================================
// Fix 1: schema-qualified table names
// ============================================================================
// Field representation contract (also enforced against the semantic analyzer):
//   primary_text = table name, schema_name = alias, catalog_name = schema.

TEST_F(CorrectnessRound2Test, SchemaQualifiedSelectRetainsAliasAndWhere) {
    auto* ast = parse("SELECT * FROM public.users u WHERE u.id=1");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::SelectStmt);

    auto* from = find_child(ast, NodeType::FromClause);
    ASSERT_NE(from, nullptr);
    auto* table = from->first_child;
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->node_type, NodeType::TableRef);
    EXPECT_EQ(table->primary_text, "users");   // table name
    EXPECT_EQ(table->catalog_name, "public");  // schema qualifier
    EXPECT_EQ(table->schema_name, "u");         // alias slot (analyzer reads this)

    // The WHERE clause must survive the qualified table name.
    ASSERT_NE(find_child(ast, NodeType::WhereClause), nullptr);
}

TEST_F(CorrectnessRound2Test, SchemaQualifiedDeleteRetainsSchemaAndWhere) {
    // Data-loss regression: DELETE FROM public.users WHERE id=1 previously
    // dropped ".users" and the entire WHERE, becoming an unbounded delete.
    auto* ast = parse("DELETE FROM public.users WHERE id=1");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::DeleteStmt);

    auto* table = ast->first_child;
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->node_type, NodeType::TableRef);
    EXPECT_EQ(table->primary_text, "users");
    EXPECT_EQ(table->catalog_name, "public");

    ASSERT_NE(find_child(ast, NodeType::WhereClause), nullptr);
}

TEST_F(CorrectnessRound2Test, UnqualifiedTableUnaffected) {
    auto* ast = parse("SELECT * FROM users WHERE id=1");
    ASSERT_NE(ast, nullptr);
    auto* from = find_child(ast, NodeType::FromClause);
    ASSERT_NE(from, nullptr);
    auto* table = from->first_child;
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->primary_text, "users");
    EXPECT_TRUE(table->catalog_name.empty());
    ASSERT_NE(find_child(ast, NodeType::WhereClause), nullptr);
}

// ============================================================================
// Fix 2: trailing-input detection (lenient, queryable)
// ============================================================================

TEST_F(CorrectnessRound2Test, TrailingInputIsFlagged) {
    auto result = parser.parse("SELECT * FROM t WHERE x=1 EXTRA STUFF HERE");
    ASSERT_TRUE(result.has_value());  // still lenient success
    EXPECT_TRUE(parser.has_trailing_input());
    EXPECT_GT(parser.trailing_token_count(), 0u);
}

TEST_F(CorrectnessRound2Test, NoTrailingInputForCleanStatement) {
    auto result = parser.parse("SELECT * FROM t WHERE x=1");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(parser.has_trailing_input());
    EXPECT_EQ(parser.trailing_token_count(), 0u);
}

TEST_F(CorrectnessRound2Test, TrailingSemicolonNotCountedAsTrailingInput) {
    auto result = parser.parse("SELECT * FROM t;");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(parser.has_trailing_input());
}

// ============================================================================
// Fix 3: case-insensitive DML keyword gating
// ============================================================================

TEST_F(CorrectnessRound2Test, LowercaseUpdateReturning) {
    auto* ast = parse("update t set a=1 returning *");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::UpdateStmt);
    ASSERT_NE(find_child(ast, NodeType::ReturningClause), nullptr);
}

TEST_F(CorrectnessRound2Test, LowercaseDeleteReturning) {
    auto* ast = parse("delete from t returning id");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::DeleteStmt);
    ASSERT_NE(find_child(ast, NodeType::ReturningClause), nullptr);
}

// ============================================================================
// Fix 4: INSERT RETURNING and ON CONFLICT wired to real helpers
// ============================================================================

TEST_F(CorrectnessRound2Test, InsertReturning) {
    auto* ast = parse("INSERT INTO t (a) VALUES (1) RETURNING *");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::InsertStmt);
    auto* ret = find_child(ast, NodeType::ReturningClause);
    ASSERT_NE(ret, nullptr);
    ASSERT_NE(ret->first_child, nullptr);
    EXPECT_EQ(ret->first_child->node_type, NodeType::Star);
}

TEST_F(CorrectnessRound2Test, InsertOnConflictTargetAndSet) {
    auto* ast = parse("INSERT INTO t (a) VALUES (1) ON CONFLICT (a) DO UPDATE SET a=2");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->node_type, NodeType::InsertStmt);

    auto* on_conflict = find_child(ast, NodeType::OnConflictClause);
    ASSERT_NE(on_conflict, nullptr);

    // Conflict target column "a" must be present.
    auto* target = find_child(on_conflict, NodeType::Identifier);
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->primary_text, "a");

    // DO UPDATE SET must be populated.
    auto* set_clause = find_child(on_conflict, NodeType::SetClause);
    ASSERT_NE(set_clause, nullptr);
    ASSERT_NE(set_clause->first_child, nullptr);
    EXPECT_EQ(set_clause->first_child->primary_text, "a");
}

TEST_F(CorrectnessRound2Test, InsertOnConflictDoNothing) {
    auto* ast = parse("INSERT INTO t (a) VALUES (1) ON CONFLICT DO NOTHING");
    ASSERT_NE(ast, nullptr);
    ASSERT_NE(find_child(ast, NodeType::OnConflictClause), nullptr);
}

}  // namespace
