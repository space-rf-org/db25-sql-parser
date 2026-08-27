/*
 * DepthGuard security test.
 *
 * Verifies that the parser's recursion DepthGuard protects against stack
 * overflow from maliciously (or accidentally) deep nesting. These are real,
 * gating assertions: deeply nested inputs MUST be rejected with a graceful
 * error Result rather than crashing or overflowing the stack, custom depth
 * limits MUST be honoured, and the parser MUST remain usable after a depth
 * failure.
 */

#include <string>
#include <sstream>
#include <chrono>

#include <gtest/gtest.h>

#include "db25/parser/parser.hpp"

using namespace db25;
using namespace db25::parser;

namespace {

// The error message emitted by the guard when the limit is exceeded.
constexpr const char* kDepthError = "Maximum recursion depth exceeded";

// Generate deeply nested subqueries: SELECT * FROM (SELECT * FROM (... SELECT 1 ...))
std::string generate_nested_subqueries(int depth) {
    std::stringstream sql;
    for (int i = 0; i < depth; ++i) {
        sql << "SELECT * FROM (";
    }
    sql << "SELECT 1";
    for (int i = 0; i < depth; ++i) {
        sql << ") AS t" << i;
    }
    return sql.str();
}

// Generate deeply nested parenthesised join groups in FROM:
// SELECT 1 FROM (((( ... t ... )))). Each '(' whose next token begins a table
// reference is a join group and recurses parse_from_clause -> parse_table_reference.
std::string generate_nested_from_groups(int depth) {
    std::stringstream sql;
    sql << "SELECT 1 FROM ";
    for (int i = 0; i < depth; ++i) {
        sql << "(";
    }
    sql << "t";
    for (int i = 0; i < depth; ++i) {
        sql << ")";
    }
    return sql.str();
}

// Generate deeply nested parenthesised expressions: SELECT (1 + (1 + ( ... 1 ... )))
std::string generate_nested_expr(int depth) {
    std::stringstream sql;
    sql << "SELECT ";
    for (int i = 0; i < depth; ++i) {
        sql << "(1 + ";
    }
    sql << "1";
    for (int i = 0; i < depth; ++i) {
        sql << ")";
    }
    sql << " AS result";
    return sql.str();
}

// Generate deeply nested CREATE TRIGGER with a single-statement body:
// CREATE TRIGGER ... CREATE TRIGGER ... (x depth) SELECT 1. Each level recurses
// parse_create_trigger -> parse_statement -> parse_create_stmt -> parse_create_trigger.
std::string generate_nested_triggers_single(int depth) {
    std::stringstream sql;
    for (int i = 0; i < depth; ++i) {
        sql << "CREATE TRIGGER x AFTER INSERT ON t ";
    }
    sql << "SELECT 1";
    return sql.str();
}

// Same recursion, but through the BEGIN ... END trigger-body form.
std::string generate_nested_triggers_block(int depth) {
    std::stringstream sql;
    for (int i = 0; i < depth; ++i) {
        sql << "CREATE TRIGGER x AFTER INSERT ON t BEGIN ";
    }
    sql << "SELECT 1";
    for (int i = 0; i < depth; ++i) {
        sql << " END";
    }
    return sql.str();
}

// Generate a FLAT (unparenthesised) left-deep set-operation chain:
// SELECT 1 UNION SELECT 1 UNION ... (count operators). This is folded in a single
// linear pass (fold_set_operations), NOT through the per-level DepthGuard, yet it
// produces a `count`-deep left-nested AST that every downstream stage walks
// recursively - so the fold must apply the same depth cap.
std::string generate_setop_chain(int ops) {
    std::stringstream sql;
    sql << "SELECT 1";
    for (int i = 0; i < ops; ++i) {
        sql << " UNION SELECT 1";
    }
    return sql.str();
}

// A flat left-associative binary-operator chain: `1 + 1 + ... + 1` (`ops`
// operators). Folded ITERATIVELY in parse_expression's operator loop, so - like
// the set-op chain - it escapes the recursion guard and must be depth-capped or
// it builds an unbounded left-deep AST that overflows downstream walkers.
std::string generate_operator_chain(int ops) {
    std::stringstream sql;
    sql << "SELECT 1";
    for (int i = 0; i < ops; ++i) {
        sql << " + 1";
    }
    return sql.str();
}

// A flat postfix `::cast` chain: `1::int::int::...` (`ops` casts). Folded
// iteratively in parse_cast_postfix, the same hazard.
std::string generate_cast_chain(int ops) {
    std::stringstream sql;
    sql << "SELECT 1";
    for (int i = 0; i < ops; ++i) {
        sql << "::int";
    }
    return sql.str();
}

// A flat postfix COLLATE chain: `a COLLATE "C" COLLATE "C" ...` (`ops` collates).
// Folded iteratively in parse_collate_postfix, the same hazard.
std::string generate_collate_chain(int ops) {
    std::stringstream sql;
    sql << "SELECT a";
    for (int i = 0; i < ops; ++i) {
        sql << " COLLATE \"C\"";
    }
    sql << " FROM t";
    return sql.str();
}

// An ALTERNATING postfix chain `a COLLATE "C"::int COLLATE "C"::int ...`
// (`pairs` collate/cast pairs, AST depth ~= 2*pairs). Each pass through the
// outer postfix loop applies one COLLATE and one ::cast; before the shared
// fold budget, each helper's per-call counter reset to 0 every pass, so this
// interleaving bypassed the per-fold caps and built an unbounded left-deep AST
// even though a pure COLLATE (or pure ::cast) chain of the same depth is
// rejected.
std::string generate_alternating_postfix_chain(int pairs) {
    std::stringstream sql;
    sql << "SELECT a";
    for (int i = 0; i < pairs; ++i) {
        sql << " COLLATE \"C\"::int";
    }
    sql << " FROM t";
    return sql.str();
}

}  // namespace

// A flat set-op chain within the limit parses (the AST is bounded, so the
// analyzer / binder / optimizer can walk it without overflow).
TEST(DepthGuard, ShallowSetOpChainParses) {
    Parser parser;
    auto result = parser.parse(generate_setop_chain(50));
    ASSERT_TRUE(result.has_value())
        << "Shallow set-op chain should parse, got error: " << result.error().message;
}

// A flat set-op chain longer than the depth limit must be rejected gracefully
// with the standard depth error - NOT accepted to build an unbounded left-deep
// AST that stack-overflows analyze_setop / bind_setop / the optimizer passes on
// otherwise-legal input.
TEST(DepthGuard, DeepSetOpChainRejected) {
    Parser parser;
    const size_t limit = parser.config().max_depth;
    auto result = parser.parse(generate_setop_chain(static_cast<int>(limit) * 3));
    ASSERT_FALSE(result.has_value())
        << "Deep flat set-op chain must be rejected by the depth cap";
    EXPECT_EQ(result.error().message, kDepthError);
}

// The cap sits exactly at max_depth: a chain of `max_depth` operators is accepted,
// one more is rejected. Uses a small custom limit so the boundary is cheap to hit.
TEST(DepthGuard, SetOpChainBoundaryHonored) {
    Parser parser;
    ParserConfig config = parser.config();
    config.max_depth = 40;
    parser.set_config(config);

    auto ok = parser.parse(generate_setop_chain(40));
    ASSERT_TRUE(ok.has_value())
        << "A chain of exactly max_depth operators should parse, got: "
        << (ok.has_value() ? std::string{} : ok.error().message);

    auto bad = parser.parse(generate_setop_chain(41));
    ASSERT_FALSE(bad.has_value())
        << "One operator past max_depth must be rejected";
    EXPECT_EQ(bad.error().message, kDepthError);
}

// A flat binary-operator chain within the limit parses; one far past it is
// rejected with the standard depth error instead of building an unbounded
// left-deep AST that overflows analyze/bind/optimize (the same hazard the
// set-op chain cap addresses, for expression operators).
TEST(DepthGuard, ShallowOperatorChainParses) {
    Parser parser;
    auto result = parser.parse(generate_operator_chain(50));
    ASSERT_TRUE(result.has_value())
        << "Shallow operator chain should parse, got error: " << result.error().message;
}

TEST(DepthGuard, DeepOperatorChainRejected) {
    Parser parser;
    const size_t limit = parser.config().max_depth;
    auto result = parser.parse(generate_operator_chain(static_cast<int>(limit) * 3));
    ASSERT_FALSE(result.has_value())
        << "Deep flat operator chain must be rejected by the depth cap";
    EXPECT_EQ(result.error().message, kDepthError);
}

// The same for a flat `::cast` chain.
TEST(DepthGuard, ShallowCastChainParses) {
    Parser parser;
    auto result = parser.parse(generate_cast_chain(50));
    ASSERT_TRUE(result.has_value())
        << "Shallow cast chain should parse, got error: " << result.error().message;
}

TEST(DepthGuard, DeepCastChainRejected) {
    Parser parser;
    const size_t limit = parser.config().max_depth;
    auto result = parser.parse(generate_cast_chain(static_cast<int>(limit) * 3));
    ASSERT_FALSE(result.has_value())
        << "Deep flat cast chain must be rejected by the depth cap";
    EXPECT_EQ(result.error().message, kDepthError);
}

// The same for a flat COLLATE chain.
TEST(DepthGuard, ShallowCollateChainParses) {
    Parser parser;
    auto result = parser.parse(generate_collate_chain(50));
    ASSERT_TRUE(result.has_value())
        << "Shallow collate chain should parse, got error: " << result.error().message;
}

TEST(DepthGuard, DeepCollateChainRejected) {
    Parser parser;
    const size_t limit = parser.config().max_depth;
    auto result = parser.parse(generate_collate_chain(static_cast<int>(limit) * 3));
    ASSERT_FALSE(result.has_value())
        << "Deep flat collate chain must be rejected by the depth cap";
    EXPECT_EQ(result.error().message, kDepthError);
}

// A shallow alternating COLLATE/::cast postfix chain (well under the cap even
// counting both folds) still parses.
TEST(DepthGuard, ShallowAlternatingPostfixChainParses) {
    Parser parser;
    auto result = parser.parse(generate_alternating_postfix_chain(25));  // depth ~50
    ASSERT_TRUE(result.has_value())
        << "Shallow alternating postfix chain should parse, got error: "
        << (result.has_value() ? std::string{} : result.error().message);
}

// A deep alternating COLLATE/::cast postfix chain must be rejected: the two
// folds share one budget, so interleaving them cannot slip past the cap. Uses
// enough pairs that the combined depth (2*pairs) exceeds max_depth even though
// neither the collate count nor the cast count alone does.
TEST(DepthGuard, DeepAlternatingPostfixChainRejected) {
    Parser parser;
    const size_t limit = parser.config().max_depth;
    // limit pairs -> ~2*limit AST depth; each individual fold kind is only
    // `limit` deep, so a per-helper counter would accept it.
    auto result =
        parser.parse(generate_alternating_postfix_chain(static_cast<int>(limit)));
    ASSERT_FALSE(result.has_value())
        << "Deep alternating COLLATE/::cast postfix chain must be rejected by the "
           "shared depth cap";
    EXPECT_EQ(result.error().message, kDepthError);
}

// A shallow chain of nested triggers stays under the limit and must parse. This
// also confirms the recursion is real (each CREATE TRIGGER body can itself be a
// statement), i.e. the deep cases below exercise a genuine recursion vector.
TEST(DepthGuard, ShallowNestedTriggersParse) {
    Parser parser;
    ASSERT_TRUE(parser.parse(generate_nested_triggers_single(3)).has_value());
    ASSERT_TRUE(parser.parse(generate_nested_triggers_block(3)).has_value());
}

// Deeply nested CREATE TRIGGER (single-statement body) previously drove unbounded
// native recursion and overflowed the stack: every DDL entry point wrote its
// DepthGuard as an if-init-statement, so the guard object was destroyed at the
// end of the `if` and depth never accumulated. It must now be rejected gracefully.
TEST(DepthGuard, DeeplyNestedTriggersSingleRejected) {
    Parser parser;
    const size_t limit = parser.config().max_depth;
    auto result = parser.parse(generate_nested_triggers_single(static_cast<int>(limit) * 3));
    ASSERT_FALSE(result.has_value())
        << "Deeply nested CREATE TRIGGER must be rejected by the DepthGuard";
    EXPECT_EQ(result.error().message, kDepthError);
}

// Deeply nested CREATE TRIGGER through the BEGIN ... END body form. Besides the
// same defeated-guard stack overflow, this path had a second hazard: the body
// loop re-called parse_statement() on a token it could not consume without ever
// advancing, so once the depth guard returned nullptr the crash degraded into an
// infinite loop. It must terminate and reject gracefully.
TEST(DepthGuard, DeeplyNestedTriggersBlockRejected) {
    Parser parser;
    const size_t limit = parser.config().max_depth;
    auto result = parser.parse(generate_nested_triggers_block(static_cast<int>(limit) * 3));
    ASSERT_FALSE(result.has_value())
        << "Deeply nested BEGIN..END triggers must be rejected by the DepthGuard";
    EXPECT_EQ(result.error().message, kDepthError);
}

// A stray, unparseable token inside a BEGIN ... END trigger body must not hang.
// The body loop calls parse_statement(), which returns nullptr on '@'; without a
// forward-progress guard the loop spins on the same token forever. Completion of
// this test (it does not time out) is the assertion; the parser must also stay
// usable afterwards.
TEST(DepthGuard, GarbageInTriggerBodyTerminates) {
    Parser parser;
    (void)parser.parse("CREATE TRIGGER x AFTER INSERT ON t BEGIN @ END");
    // Reusable after the (previously hanging) input.
    auto ok = parser.parse("SELECT * FROM users WHERE id = 1");
    ASSERT_TRUE(ok.has_value())
        << "Parser must remain usable after a malformed trigger body";
}

// A modestly nested query stays well under the default limit and must parse.
TEST(DepthGuard, ShallowNestedQueryParses) {
    Parser parser;
    const std::string sql = generate_nested_subqueries(10);
    auto result = parser.parse(sql);
    ASSERT_TRUE(result.has_value())
        << "Shallow query should parse, got error: " << result.error().message;
}

// Deeply nested subqueries that exceed the default depth limit must be
// rejected gracefully (error Result, no crash, no stack overflow).
TEST(DepthGuard, DeeplyNestedSubqueriesRejected) {
    Parser parser;
    const size_t limit = parser.config().max_depth;
    const std::string sql = generate_nested_subqueries(static_cast<int>(limit) * 3);

    auto result = parser.parse(sql);
    ASSERT_FALSE(result.has_value())
        << "Deeply nested subqueries must be rejected by the DepthGuard";
    EXPECT_EQ(result.error().message, kDepthError);
}

// A modestly nested parenthesised FROM join group stays under the limit and parses.
TEST(DepthGuard, ShallowNestedFromGroupParses) {
    Parser parser;
    auto result = parser.parse(generate_nested_from_groups(5));
    ASSERT_TRUE(result.has_value())
        << "Shallow FROM join group should parse, got error: " << result.error().message;
}

// Deeply nested parenthesised FROM join groups (`FROM ((((...`) previously drove
// unbounded native recursion (parse_table_reference <-> parse_from_clause) and
// overflowed the stack. They must now be rejected gracefully by the DepthGuard.
TEST(DepthGuard, DeeplyNestedFromGroupsRejected) {
    Parser parser;
    const size_t limit = parser.config().max_depth;
    const std::string sql = generate_nested_from_groups(static_cast<int>(limit) * 3);

    auto result = parser.parse(sql);
    ASSERT_FALSE(result.has_value())
        << "Deeply nested FROM join groups must be rejected by the DepthGuard";
    EXPECT_EQ(result.error().message, kDepthError);
}

// Deeply nested parenthesised expressions must likewise be rejected gracefully.
TEST(DepthGuard, DeeplyNestedExpressionsRejected) {
    Parser parser;
    const size_t limit = parser.config().max_depth;
    const std::string sql = generate_nested_expr(static_cast<int>(limit) * 3);

    auto result = parser.parse(sql);
    ASSERT_FALSE(result.has_value())
        << "Deeply nested expressions must be rejected by the DepthGuard";
    EXPECT_EQ(result.error().message, kDepthError);
}

// A custom (lower) depth limit must be honoured: inputs within the limit parse,
// inputs beyond it are rejected.
TEST(DepthGuard, CustomDepthLimitHonored) {
    Parser parser;
    ParserConfig config = parser.config();
    config.max_depth = 50;
    parser.set_config(config);

    // Comfortably within the custom limit.
    {
        auto ok = parser.parse(generate_nested_subqueries(10));
        ASSERT_TRUE(ok.has_value())
            << "Query within custom limit should parse, got: " << ok.error().message;
    }

    // Well beyond the custom limit.
    {
        auto bad = parser.parse(generate_nested_subqueries(200));
        ASSERT_FALSE(bad.has_value())
            << "Query beyond custom limit must be rejected";
        EXPECT_EQ(bad.error().message, kDepthError);
    }
}

// After a depth failure the parser must reset cleanly and remain reusable.
TEST(DepthGuard, ParserReusableAfterDepthFailure) {
    Parser parser;
    const size_t limit = parser.config().max_depth;

    auto deep = parser.parse(generate_nested_subqueries(static_cast<int>(limit) * 3));
    ASSERT_FALSE(deep.has_value());
    ASSERT_EQ(deep.error().message, kDepthError);

    // The parser must recover and parse an ordinary query.
    auto ok = parser.parse("SELECT * FROM users WHERE id = 1");
    ASSERT_TRUE(ok.has_value())
        << "Parser must be reusable after a depth failure, got: "
        << ok.error().message;
}

// A run of nested parentheses around a query body at the FROM / IN / scalar /
// EXISTS gates must be classified and parsed in ~LINEAR time. The query-body
// gate predicate (paren_group_starts_query) once ran a recursive balanced scan
// re-invoked at every nesting level - an O(depth^3) parse-time DoS on inputs the
// parser accepts (below max_depth): FROM depth 800 took ~6.5s. It is now a single
// linear pass. A few hundred nested parens must parse in milliseconds; the very
// generous wall-clock ceiling here (seconds) exists only to fail loudly if the
// super-linear blowup ever returns, without being flaky under CI load.
TEST(DepthGuard, NestedParenQueryBodyParsesInLinearTime) {
    const auto within = [](const std::string& sql, bool expect_ok) {
        Parser parser;
        const auto t0 = std::chrono::steady_clock::now();
        auto result = parser.parse(sql);
        const double sec =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        EXPECT_EQ(result.has_value(), expect_ok) << sql.substr(0, 40);
        // Pre-fix: ~6.5s at depth 800, ~29s at 1600. Post-fix: sub-millisecond.
        EXPECT_LT(sec, 2.0) << "super-linear parse-time regression: " << sec << "s";
    };
    // depth 300 is well below max_depth (1000): these parse successfully.
    within("SELECT * FROM " + std::string(300, '(') + "SELECT 1" + std::string(300, ')'),
           true);
    within("SELECT x FROM t WHERE x IN " + std::string(300, '(') + "SELECT 1" +
               std::string(300, ')'),
           true);
    within("SELECT " + std::string(300, '(') + "SELECT 1" + std::string(300, ')') +
               " FROM t",
           true);
    // depth 3000 exceeds max_depth: must be rejected gracefully - and, crucially,
    // FAST (pre-fix this depth effectively hung).
    within("SELECT * FROM " + std::string(3000, '(') + "SELECT 1" + std::string(3000, ')'),
           false);
}
