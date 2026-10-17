/*
 * Non-reserved keywords in NAME positions.
 *
 * The tokenizer classifies all 208 of its keywords as TokenType::Keyword, so a
 * table, CTE or alias named `data`, `type`, `level`, `query`, `range` or `key`
 * never reaches the parser as an Identifier. Every parser site that tested for
 * TokenType::Identifier therefore rejected valid SQL.
 *
 * WHY THESE TESTS ASSERT AST SHAPE AND NOT has_value(). At the table-name and
 * alias sites the old behaviour was not a parse error. parse_table_reference
 * returned nullptr, parse_from_clause returned nullptr, and parse_select_stmt's
 * clause-boundary test let the unconsumed keyword pass unreported - so
 * `SELECT * FROM data` returned a successful Result whose AST was
 * `(SelectStmt (SelectList (Star)))` with the FROM clause gone, and
 * `SELECT * FROM t data` returned a TableRef with no alias. A suite that
 * checked only has_value() would have been green throughout. The assertions
 * below name the node that must exist, so the silent-drop form fails them.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "db25/ast/ast_node.hpp"
#include "db25/parser/parser.hpp"

using namespace db25::parser;
using db25::ast::ASTNode;
using db25::ast::NodeType;

namespace {

// Depth-first search for the first node of `type`.
const ASTNode* find(const ASTNode* n, NodeType type) {
    if (n == nullptr) return nullptr;
    if (n->node_type == type) return n;
    for (const ASTNode* c = n->first_child; c != nullptr; c = c->next_sibling) {
        if (const ASTNode* hit = find(c, type)) return hit;
    }
    return nullptr;
}

std::vector<const ASTNode*> find_all(const ASTNode* n, NodeType type) {
    std::vector<const ASTNode*> out;
    if (n == nullptr) return out;
    if (n->node_type == type) out.push_back(n);
    for (const ASTNode* c = n->first_child; c != nullptr; c = c->next_sibling) {
        auto sub = find_all(c, type);
        out.insert(out.end(), sub.begin(), sub.end());
    }
    return out;
}

// Parse and return the root, failing the test (not crashing) if parsing errored.
const ASTNode* parse_ok(Parser& parser, const std::string& sql) {
    auto result = parser.parse(sql);
    EXPECT_TRUE(result.has_value()) << "expected a successful parse for: [" << sql << "]";
    return result.has_value() ? *result : nullptr;
}

// The table named by the first TableRef, or "<no TableRef>" if the reference
// was dropped. Returning a sentinel rather than dereferencing null keeps a
// regression a test FAILURE instead of a segfaulting test binary.
std::string first_table_name(const ASTNode* root) {
    const ASTNode* t = find(root, NodeType::TableRef);
    return t == nullptr ? "<no TableRef>" : std::string(t->primary_text);
}

std::string first_table_alias(const ASTNode* root) {
    const ASTNode* t = find(root, NodeType::TableRef);
    return t == nullptr ? "<no TableRef>" : std::string(t->schema_name);
}

}  // namespace

// ---------------------------------------------------------------------------
// A keyword IS a name where the grammar admits only a name.
// ---------------------------------------------------------------------------

TEST(KeywordIdentifiers, KeywordTableNameKeepsTheFromClause) {
    Parser parser;
    const ASTNode* root = parse_ok(parser, "SELECT * FROM data");
    ASSERT_NE(find(root, NodeType::FromClause), nullptr)
        << "the FROM clause was dropped, and the parse still succeeded";
    EXPECT_EQ(first_table_name(root), "data");
}

TEST(KeywordIdentifiers, EveryNonGrammaticalKeywordWorksAsATableName) {
    // A spread across the table rather than one word, so a fix that special-
    // cases DATA does not pass.
    for (const char* name : {"data", "type", "level", "key", "chain", "query",
                             "range", "start", "owner", "schema", "search",
                             "depth", "plan", "text", "zone", "action"}) {
        Parser parser;
        const std::string sql = std::string("SELECT * FROM ") + name;
        const ASTNode* root = parse_ok(parser, sql);
        EXPECT_EQ(first_table_name(root), name) << "for: [" << sql << "]";
    }
}

TEST(KeywordIdentifiers, KeywordAliasIsRecorded) {
    {
        Parser parser;
        const ASTNode* root = parse_ok(parser, "SELECT * FROM t data");
        EXPECT_EQ(first_table_name(root), "t");
        EXPECT_EQ(first_table_alias(root), "data") << "the bare alias was dropped";
    }
    {
        Parser parser;
        const ASTNode* root = parse_ok(parser, "SELECT * FROM t AS level");
        EXPECT_EQ(first_table_alias(root), "level") << "the AS alias was dropped";
    }
}

TEST(KeywordIdentifiers, KeywordCteNameResolvesThroughToTheFromClause) {
    Parser parser;
    const ASTNode* root =
        parse_ok(parser, "WITH data AS (SELECT 1 AS id) SELECT id FROM data");
    const ASTNode* cte = find(root, NodeType::CTEDefinition);
    ASSERT_NE(cte, nullptr);
    EXPECT_EQ(std::string(cte->primary_text), "data");
    ASSERT_NE(find(root, NodeType::FromClause), nullptr);
}

TEST(KeywordIdentifiers, KeywordTableNameOnAJoinRightHandSide) {
    Parser parser;
    const ASTNode* root =
        parse_ok(parser, "SELECT * FROM orders o JOIN data d ON o.id = d.id");
    auto refs = find_all(root, NodeType::TableRef);
    ASSERT_EQ(refs.size(), 2u) << "the join's right-hand table was dropped";
    EXPECT_EQ(std::string(refs[1]->primary_text), "data");
    EXPECT_EQ(std::string(refs[1]->schema_name), "d");
}

TEST(KeywordIdentifiers, KeywordTableNameIsQualified) {
    Parser parser;
    const ASTNode* root = parse_ok(parser, "SELECT * FROM public.data");
    const ASTNode* t = find(root, NodeType::TableRef);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(std::string(t->primary_text), "data");
    EXPECT_EQ(std::string(t->catalog_name), "public");
}

TEST(KeywordIdentifiers, KeywordTableNameInDmlTargets) {
    {
        Parser parser;
        const ASTNode* root = parse_ok(parser, "DELETE FROM data");
        EXPECT_EQ(first_table_name(root), "data");
    }
    {
        Parser parser;
        const ASTNode* root = parse_ok(parser, "UPDATE data SET id = 1");
        EXPECT_EQ(first_table_name(root), "data");
        EXPECT_EQ(first_table_alias(root), "") << "SET was taken as the alias";
        EXPECT_NE(find(root, NodeType::SetClause), nullptr);
    }
    {
        Parser parser;
        const ASTNode* root = parse_ok(parser, "INSERT INTO data (id) VALUES (1)");
        EXPECT_EQ(first_table_name(root), "data");
    }
}

// ---------------------------------------------------------------------------
// A keyword is NOT a name where it has a grammatical role. These are what
// stops the widening above from swallowing the rest of the statement.
// ---------------------------------------------------------------------------

TEST(KeywordIdentifiers, JoinKeywordsAreNotTakenAsAnAlias) {
    for (const char* sql : {"SELECT * FROM a NATURAL JOIN b",
                            "SELECT * FROM a CROSS JOIN b",
                            "SELECT * FROM a LEFT JOIN b ON a.id = b.id",
                            "SELECT * FROM a INNER JOIN b ON a.id = b.id"}) {
        Parser parser;
        const ASTNode* root = parse_ok(parser, sql);
        EXPECT_EQ(first_table_alias(root), "")
            << "a join keyword became the left table's alias in: [" << sql << "]";
        EXPECT_NE(find(root, NodeType::JoinClause), nullptr) << "for: [" << sql << "]";
    }
}

TEST(KeywordIdentifiers, ClauseKeywordsAreNotTakenAsAnAlias) {
    struct Case { const char* sql; NodeType clause; };
    for (const Case& c : {Case{"SELECT * FROM t WHERE x = 1", NodeType::WhereClause},
                          Case{"SELECT * FROM t GROUP BY x", NodeType::GroupByClause},
                          Case{"SELECT * FROM t ORDER BY x", NodeType::OrderByClause},
                          Case{"SELECT * FROM t LIMIT 1", NodeType::LimitClause}}) {
        Parser parser;
        const ASTNode* root = parse_ok(parser, c.sql);
        EXPECT_EQ(first_table_alias(root), "")
            << "a clause keyword became the table's alias in: [" << c.sql << "]";
        EXPECT_NE(find(root, c.clause), nullptr) << "for: [" << c.sql << "]";
    }
}

TEST(KeywordIdentifiers, DefaultValuesIsNotAnAlias) {
    // This case is why DEFAULT is in the exclusion set: the first version of the
    // predicate omitted it and DEFAULT became t's alias.
    Parser parser;
    const ASTNode* root = parse_ok(parser, "INSERT INTO t DEFAULT VALUES");
    EXPECT_EQ(first_table_name(root), "t");
    EXPECT_EQ(first_table_alias(root), "");
}

TEST(KeywordIdentifiers, AMissingTableReferenceIsStillAnError) {
    // The first four were rejected before this change by a hand-listed set of
    // clause-boundary keywords in parse_select_stmt. The last four were NOT:
    // they fell past that list, produced no FROM clause and no error, and
    // returned a SelectStmt the caller had no way to tell was truncated.
    for (const char* sql : {"SELECT * FROM where",
                            "SELECT * FROM group BY x",
                            "SELECT 1 FROM",
                            "SELECT * FROM order BY x",
                            "SELECT * FROM on",
                            "SELECT * FROM as",
                            "SELECT * FROM natural",
                            "SELECT * FROM and"}) {
        Parser parser;
        EXPECT_FALSE(parser.parse(sql).has_value())
            << "a grammar keyword was accepted as a table name in: [" << sql << "]";
    }
}

TEST(KeywordIdentifiers, AStatementKeywordIsNotACteName) {
    for (const char* sql : {"WITH select AS (SELECT 1) SELECT 1",
                            "WITH from AS (SELECT 1) SELECT 1",
                            "WITH where AS (SELECT 1) SELECT 1"}) {
        Parser parser;
        EXPECT_FALSE(parser.parse(sql).has_value())
            << "a reserved keyword was accepted as a CTE name in: [" << sql << "]";
    }
}
