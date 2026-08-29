/*
 * LATERAL joins.
 *
 * A LATERAL derived table on the right of a comma / JOIN is evaluated per left
 * row and may reference the columns of the preceding FROM items. The parser
 * records it as a LateralJoin node (the analyzer and binder key laterality off
 * that node type) whose child is the derived-table subquery, and carries the
 * IsLateral flag. This suite pins:
 *   - the comma form `FROM a, LATERAL (subq) s` parses to a LateralJoin (not a
 *     bare Subquery, and not a dropped trailing item);
 *   - the JOIN forms `CROSS JOIN LATERAL` / `[INNER] JOIN LATERAL (...) ON ...`
 *     parse to a LateralJoin carrying the join qualifier in primary_text;
 *   - a non-LATERAL derived table stays a plain Subquery (no false laterality);
 *   - outer LATERAL joins (LEFT/RIGHT/FULL JOIN LATERAL) and NATURAL JOIN
 *     LATERAL are rejected cleanly (their null-extension / merge is not modelled
 *     by the LateralJoin node, so accepting them would silently drop it);
 *   - a truncated `, LATERAL` with no subquery is a parse error, not a drop.
 */

#include <gtest/gtest.h>

#include <string>

#include "db25/ast/ast_node.hpp"
#include "db25/ast/node_types.hpp"
#include "db25/parser/parser.hpp"

using namespace db25::parser;
using db25::ast::NodeFlags;
using db25::ast::NodeType;

namespace {

const db25::ast::ASTNode* parse_ok(Parser& parser, const std::string& sql) {
    auto result = parser.parse(sql);
    EXPECT_TRUE(result.has_value()) << "expected a clean parse for: [" << sql << "]";
    if (!result.has_value()) return nullptr;
    EXPECT_EQ(parser.trailing_token_count(), 0u)
        << "unconsumed trailing tokens for: [" << sql << "]";
    return result.value();
}

void expect_parse_error(const std::string& sql) {
    Parser parser;
    auto result = parser.parse(sql);
    EXPECT_FALSE(result.has_value()) << "expected a parse error for: [" << sql << "]";
}

// First node of `type` anywhere in the tree (pre-order).
const db25::ast::ASTNode* find_node(const db25::ast::ASTNode* n, NodeType type) {
    if (n == nullptr) return nullptr;
    if (n->node_type == type) return n;
    for (const db25::ast::ASTNode* c = n->first_child; c != nullptr; c = c->next_sibling) {
        if (const auto* hit = find_node(c, type)) return hit;
    }
    return nullptr;
}

bool is_lateral(const db25::ast::ASTNode* n) {
    return (n->semantic_flags & static_cast<uint16_t>(NodeFlags::IsLateral)) != 0;
}

// A LateralJoin's derived-table child (a Subquery) is where the alias lives.
const db25::ast::ASTNode* first_child_of(const db25::ast::ASTNode* n) {
    return n != nullptr ? n->first_child : nullptr;
}

}  // namespace

// The comma form is semantically a CROSS JOIN LATERAL: it must become a
// LateralJoin node, not a bare Subquery (which would carry no laterality) and
// not a dropped trailing item (which silently narrowed `SELECT *`).
TEST(LateralJoins, CommaFormIsLateralJoin) {
    Parser parser;
    const auto* root = parse_ok(parser, "SELECT * FROM t1, LATERAL (SELECT t1.x AS z) s");
    ASSERT_NE(root, nullptr);
    const auto* lat = find_node(root, NodeType::LateralJoin);
    ASSERT_NE(lat, nullptr) << "comma-form LATERAL did not produce a LateralJoin node";
    EXPECT_TRUE(is_lateral(lat));
    // Its child is the derived table, aliased `s`.
    const auto* derived = first_child_of(lat);
    ASSERT_NE(derived, nullptr);
    EXPECT_EQ(derived->node_type, NodeType::Subquery);
    // The derived-table alias is stored in schema_name (repurposed for aliases).
    EXPECT_EQ(std::string(derived->schema_name), "s");
}

TEST(LateralJoins, CrossJoinLateral) {
    Parser parser;
    const auto* root =
        parse_ok(parser, "SELECT * FROM t1 CROSS JOIN LATERAL (SELECT t1.x AS z) s");
    ASSERT_NE(root, nullptr);
    const auto* lat = find_node(root, NodeType::LateralJoin);
    ASSERT_NE(lat, nullptr);
    EXPECT_TRUE(is_lateral(lat));
    // The join qualifier is preserved so downstream nullability stays correct.
    EXPECT_EQ(std::string(lat->primary_text), "CROSS JOIN");
}

TEST(LateralJoins, InnerJoinLateralWithOn) {
    Parser parser;
    const auto* root = parse_ok(
        parser, "SELECT * FROM t1 JOIN LATERAL (SELECT t1.x AS z) s ON true");
    ASSERT_NE(root, nullptr);
    const auto* lat = find_node(root, NodeType::LateralJoin);
    ASSERT_NE(lat, nullptr);
    EXPECT_TRUE(is_lateral(lat));
    EXPECT_EQ(std::string(lat->primary_text), "JOIN");
    // Children: the derived table plus the ON predicate.
    EXPECT_GE(lat->child_count, 2u);
}

// A non-LATERAL derived table must NOT become a LateralJoin - it has no sibling
// visibility. `FROM t1, (SELECT t1.x) s` stays a plain Subquery (the analyzer
// then rejects the correlated reference, which needs LATERAL).
TEST(LateralJoins, NonLateralDerivedTableStaysSubquery) {
    Parser parser;
    const auto* root = parse_ok(parser, "SELECT * FROM t1, (SELECT 1 AS z) s");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(find_node(root, NodeType::LateralJoin), nullptr);
}

// LEFT [OUTER] JOIN LATERAL is legal: the RHS is correlated AND null-extended.
// It parses to a LateralJoin whose primary_text keeps the LEFT qualifier so the
// analyzer / binder apply the null-extension.
TEST(LateralJoins, LeftJoinLateralAccepted) {
    Parser parser;
    for (const char* sql : {
             "SELECT * FROM t1 LEFT JOIN LATERAL (SELECT t1.x AS z) s ON true",
             "SELECT * FROM t1 LEFT OUTER JOIN LATERAL (SELECT t1.x AS z) s ON true",
         }) {
        const auto* root = parse_ok(parser, sql);
        ASSERT_NE(root, nullptr) << sql;
        const auto* lat = find_node(root, NodeType::LateralJoin);
        ASSERT_NE(lat, nullptr) << sql;
        EXPECT_TRUE(is_lateral(lat)) << sql;
        EXPECT_NE(std::string(lat->primary_text).find("LEFT"), std::string::npos)
            << "LEFT qualifier preserved in primary_text: " << sql;
        parser.reset();
    }
}

// RIGHT / FULL JOIN LATERAL is a circular dependency SQL forbids: the left side
// would be null-extended from a right input that itself depends on the left.
TEST(LateralJoins, RightFullJoinLateralRejected) {
    expect_parse_error("SELECT * FROM t1 RIGHT JOIN LATERAL (SELECT t1.x) s ON true");
    expect_parse_error("SELECT * FROM t1 FULL JOIN LATERAL (SELECT t1.x) s ON true");
    expect_parse_error(
        "SELECT * FROM t1 RIGHT OUTER JOIN LATERAL (SELECT t1.x) s ON true");
}

TEST(LateralJoins, NaturalJoinLateralRejected) {
    expect_parse_error("SELECT * FROM t1 NATURAL JOIN LATERAL (SELECT t1.x) s");
}

// A comma promising LATERAL but with no following subquery is a truncated
// statement, not a silently dropped item.
TEST(LateralJoins, TruncatedCommaLateralRejected) {
    expect_parse_error("SELECT * FROM t1, LATERAL");
}
