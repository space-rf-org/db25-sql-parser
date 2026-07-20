/*
 * DB25 Parser - AST String Lifetime Regression Tests
 *
 * These tests pin down the invariant introduced by the "AST lifetime
 * hardening" work: every std::string_view stored on an AST node
 * (primary_text, schema_name, catalog_name, ...) is copied into the
 * parser's arena and therefore does NOT alias either
 *   (a) the caller-supplied SQL text buffer, or
 *   (b) the parser's internal tokenizer token buffer.
 *
 * Because parse()/parse_script() now release the tokenizer before they
 * return, a returned AST that still exposes correct string content proves
 * the AST outlives the tokenizer.
 */

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "db25/ast/ast_node.hpp"
#include "db25/parser/parser.hpp"

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

namespace {

// Depth-first collection of every node in the tree.
void collect_nodes(const ASTNode* node, std::vector<const ASTNode*>& out) {
    if (!node) {
        return;
    }
    out.push_back(node);
    for (const ASTNode* c = node->first_child; c != nullptr; c = c->next_sibling) {
        collect_nodes(c, out);
    }
}

std::vector<const ASTNode*> all_nodes(const ASTNode* root) {
    std::vector<const ASTNode*> out;
    collect_nodes(root, out);
    return out;
}

// Find the first node whose primary_text matches `text`.
const ASTNode* find_by_text(const ASTNode* root, std::string_view text) {
    for (const ASTNode* n : all_nodes(root)) {
        if (n->primary_text == text) {
            return n;
        }
    }
    return nullptr;
}

// True if `view` points somewhere inside the byte range of `buffer`.
bool points_into(std::string_view view, const std::string& buffer) {
    const char* lo = buffer.data();
    const char* hi = buffer.data() + buffer.size();
    const char* p = view.data();
    return p >= lo && p < hi;
}

}  // namespace

class AstLifetimeTest : public ::testing::Test {
protected:
    Parser parser;
};

// The AST string data must not alias the caller's SQL text buffer: it is an
// independent arena copy.
TEST_F(AstLifetimeTest, StringsAreNotAliasedToInputBuffer) {
    std::string sql = "SELECT alpha_col, beta_col FROM gamma_table WHERE alpha_col = 42";

    auto result = parser.parse(sql);
    ASSERT_TRUE(result.has_value()) << "failed to parse test statement";
    const ASTNode* root = result.value();
    ASSERT_NE(root, nullptr);

    for (const ASTNode* n : all_nodes(root)) {
        EXPECT_FALSE(points_into(n->primary_text, sql))
            << "primary_text '" << n->primary_text
            << "' aliases the input buffer instead of the arena";
        EXPECT_FALSE(points_into(n->schema_name, sql))
            << "schema_name '" << n->schema_name
            << "' aliases the input buffer instead of the arena";
        EXPECT_FALSE(points_into(n->catalog_name, sql))
            << "catalog_name aliases the input buffer instead of the arena";
    }
}

// After parse() returns, the internal tokenizer has been destroyed. If any AST
// string still pointed into the tokenizer's buffer this would be a
// use-after-free. We reclaim the freed region with heap churn and then verify
// the AST still reports the correct text.
TEST_F(AstLifetimeTest, StringsSurviveTokenizerDestruction) {
    std::string sql =
        "SELECT customer_id, UPPER(customer_name) FROM customers "
        "WHERE customer_name = 'widget'";

    auto result = parser.parse(sql);
    ASSERT_TRUE(result.has_value());
    const ASTNode* root = result.value();
    ASSERT_NE(root, nullptr);

    // Capture views into the (now tokenizer-independent) AST.
    const ASTNode* tbl = find_by_text(root, "customers");
    const ASTNode* col = find_by_text(root, "customer_id");
    const ASTNode* fn = find_by_text(root, "UPPER");
    ASSERT_NE(tbl, nullptr);
    ASSERT_NE(col, nullptr);
    ASSERT_NE(fn, nullptr);
    std::string_view tbl_view = tbl->primary_text;
    std::string_view col_view = col->primary_text;
    std::string_view fn_view = fn->primary_text;

    // Best-effort: scribble a lot of fresh heap allocations to reclaim/overwrite
    // whatever memory the just-destroyed tokenizer occupied.
    std::vector<std::string> churn;
    churn.reserve(4096);
    for (int i = 0; i < 4096; ++i) {
        churn.emplace_back(64, static_cast<char>('A' + (i % 26)));
    }

    // The captured views must still be intact.
    EXPECT_EQ(tbl_view, "customers");
    EXPECT_EQ(col_view, "customer_id");
    EXPECT_EQ(fn_view, "UPPER");
}

// Mutating (or freeing) the caller's SQL buffer after parsing must not affect
// the AST, because the AST owns its own arena copy of every string.
TEST_F(AstLifetimeTest, StringsSurviveInputBufferMutation) {
    std::string sql = "SELECT payload_col FROM payload_table";

    auto result = parser.parse(sql);
    ASSERT_TRUE(result.has_value());
    const ASTNode* root = result.value();
    ASSERT_NE(root, nullptr);

    const ASTNode* col = find_by_text(root, "payload_col");
    const ASTNode* tbl = find_by_text(root, "payload_table");
    ASSERT_NE(col, nullptr);
    ASSERT_NE(tbl, nullptr);
    std::string_view col_view = col->primary_text;
    std::string_view tbl_view = tbl->primary_text;

    // Obliterate the original SQL buffer contents.
    std::fill(sql.begin(), sql.end(), 'Z');
    sql.clear();
    sql.shrink_to_fit();

    EXPECT_EQ(col_view, "payload_col");
    EXPECT_EQ(tbl_view, "payload_table");
}

// Re-parsing (which internally reset()s the arena and recreates the tokenizer)
// must still yield a fully valid, correct AST for the new statement. This
// exercises the "second parse() call" path called out in the lifetime work.
TEST_F(AstLifetimeTest, SecondParseYieldsValidAst) {
    auto first = parser.parse("SELECT one_col FROM first_table");
    ASSERT_TRUE(first.has_value());
    ASSERT_NE(find_by_text(first.value(), "first_table"), nullptr);

    // Different statement; this reset()s the arena and rebuilds the tokenizer.
    auto second = parser.parse("SELECT two_col FROM second_table WHERE two_col > 7");
    ASSERT_TRUE(second.has_value());
    const ASTNode* root = second.value();
    ASSERT_NE(root, nullptr);

    const ASTNode* tbl = find_by_text(root, "second_table");
    const ASTNode* col = find_by_text(root, "two_col");
    ASSERT_NE(tbl, nullptr);
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(tbl->primary_text, "second_table");
    EXPECT_EQ(col->primary_text, "two_col");

    // And its strings are arena-owned, not aliased to the new input either.
    std::string second_sql = "SELECT two_col FROM second_table WHERE two_col > 7";
    for (const ASTNode* n : all_nodes(root)) {
        EXPECT_FALSE(points_into(n->primary_text, second_sql));
    }
}

// Regression: a fresh parse_script() CALL must reset the arena once at the
// start (like parse() does). Previously it only reset next_node_id_ to 1 while
// leaving prior nodes in the arena, so a parse() followed by parse_script(),
// and repeated parse_script() calls, produced duplicate node ids and let the
// arena grow without bound. We observe both invariants:
//   - node ids in each script start at 1 and are unique
//   - identical repeated calls do not grow arena memory
TEST_F(AstLifetimeTest, ParseScriptResetsArenaEachCall) {
    const std::string script = "SELECT a FROM t; SELECT b FROM u;";

    // Prime the arena with a prior parse() so leftover nodes would linger
    // without a reset.
    auto primed = parser.parse("SELECT primed_col FROM primed_table");
    ASSERT_TRUE(primed.has_value());

    auto first = parser.parse_script(script);
    ASSERT_TRUE(first.has_value());
    const size_t mem_after_first = parser.get_memory_used();

    // Node ids across the whole script must be unique and start at 1.
    std::set<uint32_t> ids;
    uint32_t min_id = UINT32_MAX;
    for (auto* stmt : first.value()) {
        for (const ASTNode* n : all_nodes(stmt)) {
            EXPECT_TRUE(ids.insert(n->node_id).second) << "duplicate node id " << n->node_id;
            min_id = std::min(min_id, n->node_id);
        }
    }
    EXPECT_EQ(min_id, 1u);

    // A second identical call must reset the arena, so memory does not grow.
    auto second = parser.parse_script(script);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(parser.get_memory_used(), mem_after_first);

    // And a third call after the same-input churn stays flat too.
    auto third = parser.parse_script(script);
    ASSERT_TRUE(third.has_value());
    EXPECT_EQ(parser.get_memory_used(), mem_after_first);
}
