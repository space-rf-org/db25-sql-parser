/*
 * SQL-standard keyword-argument string function forms:
 *   POSITION(substring IN string)
 *   SUBSTRING(string FROM start [FOR length])   (and FOR-only)
 *   TRIM([LEADING | TRAILING | BOTH] [characters] FROM string)
 *
 * These use the keywords IN / FROM / FOR in place of commas, so they cannot go
 * through the generic comma-separated argument parser. This suite pins:
 *   - the keyword forms now parse cleanly (no leftover tokens);
 *   - the ordinary comma / single-argument spellings still parse (no
 *     regression), and the parsed AST is a FunctionCall with the right name;
 *   - a genuine `IN (...)` membership test is unaffected (the POSITION-only IN
 *     suppression does not leak);
 *   - malformed keyword forms are rejected cleanly rather than silently
 *     mis-parsed;
 *   - TRIM records its trim specification (LEADING/TRAILING/BOTH) in
 *     semantic_flags without emitting it as an argument child.
 *
 * See issue: SQL-standard keyword-argument function forms.
 */

#include <gtest/gtest.h>

#include <string>

#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"

using namespace db25::parser;
using db25::ast::NodeType;

namespace {

// Parse and require success with NO trailing (unconsumed) tokens - a form that
// parses "successfully" but drops tokens is exactly the silent-accept bug this
// feature guards against.
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

// Find the first FunctionCall node named `name` (case-insensitive) anywhere in
// the tree.
const db25::ast::ASTNode* find_call(const db25::ast::ASTNode* n, const char* name) {
    if (n == nullptr) return nullptr;
    if (n->node_type == NodeType::FunctionCall) {
        std::string u;
        for (char c : n->primary_text)
            u.push_back((c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c);
        if (u == name) return n;
    }
    for (const db25::ast::ASTNode* c = n->first_child; c != nullptr; c = c->next_sibling) {
        if (const auto* hit = find_call(c, name)) return hit;
    }
    return nullptr;
}

uint16_t child_count(const db25::ast::ASTNode* n) {
    uint16_t k = 0;
    for (const db25::ast::ASTNode* c = n->first_child; c != nullptr; c = c->next_sibling) ++k;
    return k;
}

}  // namespace

// -------- keyword forms parse cleanly ---------------------------------------

TEST(StringKeywordFunctions, PositionKeywordForm) {
    Parser p;
    const auto* root = parse_ok(p, "SELECT POSITION('@' IN email) FROM users");
    ASSERT_NE(root, nullptr);
    const auto* call = find_call(root, "POSITION");
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(child_count(call), 2u);  // substring, string
}

TEST(StringKeywordFunctions, SubstringFromFor) {
    Parser p;
    const auto* root = parse_ok(p, "SELECT SUBSTRING(email FROM 1 FOR 5) FROM users");
    ASSERT_NE(root, nullptr);
    const auto* call = find_call(root, "SUBSTRING");
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(child_count(call), 3u);  // str, from, for
}

TEST(StringKeywordFunctions, SubstringFromOnly) {
    Parser p;
    const auto* root = parse_ok(p, "SELECT SUBSTRING(email FROM 2) FROM users");
    ASSERT_NE(root, nullptr);
    EXPECT_NE(find_call(root, "SUBSTRING"), nullptr);
}

TEST(StringKeywordFunctions, SubstringForOnly) {
    Parser p;
    EXPECT_NE(parse_ok(p, "SELECT SUBSTRING(email FOR 5) FROM users"), nullptr);
}

TEST(StringKeywordFunctions, TrimBothFrom) {
    Parser p;
    const auto* root = parse_ok(p, "SELECT TRIM(BOTH ' ' FROM name) FROM t");
    ASSERT_NE(root, nullptr);
    const auto* call = find_call(root, "TRIM");
    ASSERT_NE(call, nullptr);
    // The BOTH spec is recorded in flags, NOT as an argument child; the two
    // argument children are the characters expr and the source string.
    EXPECT_EQ(child_count(call), 2u);
    // Trim spec occupies semantic_flags bits 12-13; BOTH == 3.
    EXPECT_EQ((call->semantic_flags >> 12) & 0x3u, 3u);
}

TEST(StringKeywordFunctions, TrimLeadingFromNoChars) {
    Parser p;
    const auto* root = parse_ok(p, "SELECT TRIM(LEADING FROM name) FROM t");
    ASSERT_NE(root, nullptr);
    const auto* call = find_call(root, "TRIM");
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(child_count(call), 1u);                       // just the source string
    EXPECT_EQ((call->semantic_flags >> 12) & 0x3u, 1u);     // LEADING == 1
}

TEST(StringKeywordFunctions, TrimCharsFrom) {
    Parser p;
    EXPECT_NE(parse_ok(p, "SELECT TRIM(' ' FROM name) FROM t"), nullptr);
}

TEST(StringKeywordFunctions, NestedKeywordForms) {
    Parser p;
    EXPECT_NE(parse_ok(p, "SELECT UPPER(SUBSTRING(c.name FROM 1 FOR 1)) FROM c"),
              nullptr);
    EXPECT_NE(parse_ok(p, "SELECT SUBSTRING(email FROM POSITION('@' IN email)) FROM users"),
              nullptr);
    // Concatenated search operand: `||` (precedence 5, tied with IN) must not be
    // cut by the IN suppression.
    EXPECT_NE(parse_ok(p, "SELECT POSITION('a' || 'b' IN s) FROM t"), nullptr);
}

// -------- comma / single-argument spellings still parse (no regression) ------

TEST(StringKeywordFunctions, CommaAndSingleFormsPreserved) {
    Parser p;
    EXPECT_NE(parse_ok(p, "SELECT SUBSTRING(email, 1, 5) FROM users"), nullptr);
    EXPECT_NE(parse_ok(p, "SELECT TRIM(name) FROM t"), nullptr);
    EXPECT_NE(parse_ok(p, "SELECT UPPER(TRIM(LOWER(name))) FROM users"), nullptr);
    EXPECT_NE(parse_ok(p, "SELECT POSITION(a, b) FROM t"), nullptr);
    // A delimited name is an ordinary user function, never the special form.
    EXPECT_NE(parse_ok(p, "SELECT \"substring\"(a, b) FROM t"), nullptr);
}

// -------- the POSITION IN-suppression must not leak to real membership -------

TEST(StringKeywordFunctions, GenuineInMembershipUnaffected) {
    Parser p;
    EXPECT_NE(parse_ok(p, "SELECT x FROM t WHERE x IN (1, 2, 3)"), nullptr);
    EXPECT_NE(parse_ok(p, "SELECT x FROM t WHERE POSITION('a' IN s) > 0 AND x IN (1, 2)"),
              nullptr);
    // A real IN(list) nested one level deeper inside POSITION's operand still
    // parses as membership (depth-scoped suppression).
    EXPECT_NE(parse_ok(p, "SELECT POSITION((CASE WHEN x IN (1,2) THEN 'a' ELSE 'b' END) "
                          "IN s) FROM t"),
              nullptr);
}

// -------- malformed keyword forms are rejected cleanly ----------------------

TEST(StringKeywordFunctions, MalformedFormsRejected) {
    expect_parse_error("SELECT POSITION(x) FROM t");          // no IN, not a valid form
    expect_parse_error("SELECT POSITION('a' IN) FROM t");     // missing source string
    expect_parse_error("SELECT SUBSTRING(x FROM) FROM t");    // FROM without a position
    expect_parse_error("SELECT SUBSTRING(x FOR) FROM t");     // FOR without a length
    expect_parse_error("SELECT TRIM(FROM) FROM t");           // FROM without a source
    expect_parse_error("SELECT TRIM(BOTH ' ' FROM) FROM t");  // spec + chars, no source
}
