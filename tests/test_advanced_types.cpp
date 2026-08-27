/*
 * DB25 Parser - Advanced Types Test Suite
 * Tests INTERVAL, ARRAY, ROW, JSON operators support
 */

#include <gtest/gtest.h>
#include "db25/parser/parser.hpp"
#include <functional>

using namespace db25;
using namespace db25::parser;
using namespace db25::ast;

class AdvancedTypesTest : public ::testing::Test {
protected:
    Parser parser;
    
    void SetUp() override {
        parser.reset();
    }
    
    // Helper to check if AST contains specific text
    bool contains_text(ASTNode* node, const std::string& text) {
        if (!node) return false;
        
        if (node->primary_text.find(text) != std::string::npos) {
            return true;
        }
        
        for (auto* child = node->first_child; child; child = child->next_sibling) {
            if (contains_text(child, text)) return true;
        }
        
        return false;
    }
    
    // Helper to find node by type
    ASTNode* find_node_type(ASTNode* node, NodeType type) {
        if (!node) return nullptr;
        if (node->node_type == type) return node;
        
        for (auto* child = node->first_child; child; child = child->next_sibling) {
            auto* found = find_node_type(child, type);
            if (found) return found;
        }
        return nullptr;
    }
};

// ============================================================================
// INTERVAL Type Tests
// ============================================================================

TEST_F(AdvancedTypesTest, IntervalColumnType) {
    auto result = parser.parse("CREATE TABLE events (duration INTERVAL)");
    ASSERT_TRUE(result.has_value()) << "Should parse INTERVAL column type";
    
    auto* ast = result.value();
    EXPECT_TRUE(contains_text(ast, "INTERVAL")) << "Should contain INTERVAL in AST";
}

TEST_F(AdvancedTypesTest, IntervalArrayType) {
    auto result = parser.parse("CREATE TABLE schedules (durations INTERVAL[])");
    ASSERT_TRUE(result.has_value()) << "Should parse INTERVAL[] array type";
    
    auto* ast = result.value();
    EXPECT_TRUE(contains_text(ast, "INTERVAL")) << "Should contain INTERVAL in AST";
}

TEST_F(AdvancedTypesTest, IntervalLiteral) {
    auto result = parser.parse("SELECT INTERVAL '1 day'");
    ASSERT_TRUE(result.has_value()) << "Should parse INTERVAL literal";
    
    auto* ast = result.value();
    // INTERVAL literals are currently parsed as string literals
    // This is a known limitation
    EXPECT_NE(find_node_type(ast, NodeType::StringLiteral), nullptr) 
        << "INTERVAL literal parsed as string (current behavior)";
}

TEST_F(AdvancedTypesTest, IntervalInExpression) {
    auto result = parser.parse("SELECT date + INTERVAL '1 month' FROM events");
    ASSERT_TRUE(result.has_value()) << "Should parse INTERVAL in expression";
}

TEST_F(AdvancedTypesTest, IntervalInWindowFrame) {
    auto result = parser.parse(
        "SELECT SUM(value) OVER ("
        "ORDER BY date RANGE BETWEEN INTERVAL '1' DAY PRECEDING AND CURRENT ROW"
        ") FROM measurements"
    );
    ASSERT_TRUE(result.has_value()) << "Should parse INTERVAL in window frame";
    
    auto* ast = result.value();
    auto* window = find_node_type(ast, NodeType::WindowSpec);
    ASSERT_NE(window, nullptr) << "Should have window spec";
    
    // Check that INTERVAL is captured in frame bounds
    EXPECT_TRUE(contains_text(window, "INTERVAL")) 
        << "Window frame should contain INTERVAL";
}

// ============================================================================
// ARRAY Type Tests
// ============================================================================

TEST_F(AdvancedTypesTest, ArrayColumnType) {
    auto result = parser.parse("CREATE TABLE users (tags TEXT[])");
    ASSERT_TRUE(result.has_value()) << "Should parse TEXT[] array type";
    
    auto* ast = result.value();
    EXPECT_TRUE(contains_text(ast, "TEXT[]") || contains_text(ast, "[]")) 
        << "Should contain array notation in AST";
}

TEST_F(AdvancedTypesTest, IntegerArrayType) {
    auto result = parser.parse("CREATE TABLE data (ids INTEGER[])");
    ASSERT_TRUE(result.has_value()) << "Should parse INTEGER[] array type";
}

TEST_F(AdvancedTypesTest, MultiDimArray) {
    auto result = parser.parse("CREATE TABLE matrices (matrix INTEGER[][])");
    ASSERT_TRUE(result.has_value()) << "Should parse multi-dimensional array";
}

TEST_F(AdvancedTypesTest, ArrayConstructor) {
    auto result = parser.parse("SELECT ARRAY[1, 2, 3]");
    // ARRAY constructor might be parsed as function call
    ASSERT_TRUE(result.has_value()) << "Should parse ARRAY constructor";
    
    auto* ast = result.value();
    auto* func = find_node_type(ast, NodeType::FunctionCall);
    if (func) {
        EXPECT_EQ(func->primary_text, "ARRAY") 
            << "ARRAY constructor parsed as function call (current behavior)";
    }
}

TEST_F(AdvancedTypesTest, ArrayInANY) {
    auto result = parser.parse("SELECT * FROM users WHERE id = ANY(ARRAY[1,2,3])");
    ASSERT_TRUE(result.has_value()) << "Should parse ARRAY with ANY";
}

// ============================================================================
// ROW Type Tests
// ============================================================================

TEST_F(AdvancedTypesTest, RowConstructor) {
    auto result = parser.parse("SELECT ROW(1, 'text', true)");
    // ROW constructor might be parsed as function call
    ASSERT_TRUE(result.has_value()) << "Should parse ROW constructor";
    
    auto* ast = result.value();
    auto* func = find_node_type(ast, NodeType::FunctionCall);
    if (func) {
        EXPECT_EQ(func->primary_text, "ROW") 
            << "ROW constructor parsed as function call (current behavior)";
    }
}

TEST_F(AdvancedTypesTest, RowComparison) {
    auto result = parser.parse("SELECT * FROM t WHERE (a, b, c) = ROW(1, 2, 3)");
    ASSERT_TRUE(result.has_value()) << "Should parse ROW comparison";
}

// ============================================================================
// JSON Operator Tests
// ============================================================================

// The PostgreSQL JSON access operators `->` / `->>` are NOT supported. They must
// be rejected with a clear error, NOT silently accepted by dropping the operator
// and the trailing FROM clause (the tokenizer emits `-` then `>`, so `-` would
// otherwise parse as binary minus and desync). Previously these queries appeared
// to "parse" only because trailing-input tolerance swallowed `->'key' FROM ...`,
// leaving a truncated `SELECT data` with no FROM clause.
TEST_F(AdvancedTypesTest, JsonArrowOperatorRejected) {
    auto result = parser.parse("SELECT data->'key' FROM json_table");
    EXPECT_FALSE(result.has_value())
        << "`->` is unsupported and must be rejected, not silently truncated";
}

TEST_F(AdvancedTypesTest, JsonDoubleArrowOperatorRejected) {
    auto result = parser.parse("SELECT data->>'key' FROM json_table");
    EXPECT_FALSE(result.has_value())
        << "`->>` is unsupported and must be rejected, not silently truncated";
}

TEST_F(AdvancedTypesTest, JsonPathOperator) {
    auto result = parser.parse("SELECT data#>'{users,0,name}' FROM json_table");
    bool parsed = result.has_value();
    // JSON path operators might not be supported
    if (!parsed) {
        GTEST_SKIP() << "JSON #> operator not supported";
    }
}

TEST_F(AdvancedTypesTest, JsonContainsOperator) {
    auto result = parser.parse("SELECT * FROM json_table WHERE data @> '{\"key\": \"value\"}'");
    bool parsed = result.has_value();
    // JSON contains operator might not be supported
    if (!parsed) {
        GTEST_SKIP() << "JSON @> operator not supported";
    }
}

// ============================================================================
// Complex Type Tests
// ============================================================================

TEST_F(AdvancedTypesTest, CastToInterval) {
    auto result = parser.parse("SELECT CAST('1 day' AS INTERVAL)");
    ASSERT_TRUE(result.has_value()) << "Should parse CAST to INTERVAL";
    
    auto* ast = result.value();
    auto* cast = find_node_type(ast, NodeType::CastExpr);
    ASSERT_NE(cast, nullptr) << "Should have CAST expression";
}

TEST_F(AdvancedTypesTest, ArrayOfInterval) {
    auto result = parser.parse("CREATE TABLE schedules (periods INTERVAL[])");
    ASSERT_TRUE(result.has_value()) << "Should parse INTERVAL[] type";
}

// An array-type postfix cast (`x::text[]`) must parse in ANY expression
// position, not only at the top level of a SELECT item / WHERE. The postfix
// cast handler did not consume the trailing `[]`, so `x::text[]` nested in
// parentheses, a function argument, or a CASE branch was rejected (the `[` was
// left dangling). Regression: this is the construct in the pg_case corpus row
// `... THEN ARRAY[...] || enum_range(...)::text[] ...`.
TEST_F(AdvancedTypesTest, ArrayCastInNestedContexts) {
    // Parenthesized.
    EXPECT_TRUE(parser.parse("SELECT (x::text[])").has_value());
    parser.reset();
    // Function argument.
    EXPECT_TRUE(parser.parse("SELECT f(x::text[])").has_value());
    parser.reset();
    // CASE branch, including the || array-concat form from the corpus.
    EXPECT_TRUE(parser.parse(
        "SELECT CASE WHEN true THEN ARRAY['a'] || x::text[] ELSE ARRAY['c'] END")
                    .has_value());
    parser.reset();
    // The full pg_case corpus statement now parses.
    EXPECT_TRUE(parser.parse(
        "SELECT CASE 'foo'::text WHEN 'foo' "
        "THEN ARRAY['a', 'b', 'c', 'd'] || enum_range(NULL::casetestenum)::text[] "
        "ELSE ARRAY['x', 'y'] END")
                    .has_value());
    parser.reset();
    // A CastExpr node is produced and marked as an array type.
    auto result = parser.parse("SELECT CASE WHEN true THEN x::text[] END");
    ASSERT_TRUE(result.has_value());
    auto* cast = find_node_type(result.value(), NodeType::CastExpr);
    ASSERT_NE(cast, nullptr);
    EXPECT_TRUE(contains_text(cast, "[]")) << "type node records array-ness";
}

// The `::type` array-suffix consumer must be LINEAR in the number of `[]`
// dimensions. It once rebuilt the whole (growing) type string on every `[]`,
// which is O(N^2) time and O(N^2) never-freed arena memory - a large but legal
// `x::int[][]...[]` exhausted memory. This pins both the exact multi-dimension
// type text (a few dims) and that a large-dimension cast parses cheaply and
// records the full suffix (the quadratic version would take seconds / ~1 GB at
// this size).
TEST_F(AdvancedTypesTest, ArrayCastDimensionSuffixIsLinear) {
    // Exact suffix for 2 and 3 dimensions.
    for (auto [sql, want] : std::initializer_list<std::pair<const char*, const char*>>{
             {"SELECT x::int[]", "int[]"},
             {"SELECT x::int[][]", "int[][]"},
             {"SELECT x::int[][][]", "int[][][]"}}) {
        auto r = parser.parse(sql);
        ASSERT_TRUE(r.has_value()) << sql;
        auto* c = find_node_type(r.value(), NodeType::CastExpr);
        ASSERT_NE(c, nullptr) << sql;
        EXPECT_TRUE(contains_text(c, want)) << "type text should be " << want;
        parser.reset();
    }

    // Large-dimension cast: 4000 `[]` pairs. Linear -> parses instantly; the
    // old quadratic path would take seconds and hundreds of MB.
    const std::size_t kDims = 4000;
    std::string sql = "SELECT x::int";
    sql.reserve(sql.size() + kDims * 2 + 8);
    for (std::size_t i = 0; i < kDims; ++i) {
        sql += "[]";
    }
    auto big = parser.parse(sql);
    ASSERT_TRUE(big.has_value()) << "large multi-dimension array cast must parse";
    auto* bc = find_node_type(big.value(), NodeType::CastExpr);
    ASSERT_NE(bc, nullptr);
    // Full suffix recorded: base "int" + kDims * "[]".
    std::string expect_suffix(kDims * 2, ' ');
    for (std::size_t i = 0; i < kDims; ++i) {
        expect_suffix[2 * i] = '[';
        expect_suffix[2 * i + 1] = ']';
    }
    EXPECT_TRUE(contains_text(bc, expect_suffix))
        << "every dimension of a large array cast must be recorded";
}

// The functional CAST(x AS <type>[]) must consume the array-type suffix exactly
// as the equivalent x::<type>[] postfix cast does. It once did not, so
// CAST(x AS int[]) was rejected outright, and - worse - embedding it in a larger
// statement let the parser's leftover-token tolerance SILENTLY DROP the rest of
// the statement.
TEST_F(AdvancedTypesTest, FunctionalCastArraySuffix) {
    // CAST(x AS int[]) parses and records the array type ('int[]' text), matching
    // the ::int[] postfix shape.
    auto one = parser.parse("SELECT CAST(x AS int[])");
    ASSERT_TRUE(one.has_value()) << "CAST(x AS int[]) must parse";
    auto* cast = find_node_type(one.value(), NodeType::CastExpr);
    ASSERT_NE(cast, nullptr);
    EXPECT_TRUE(contains_text(cast, "int[]")) << "records the array type text";
    parser.reset();

    // Multi-dimension and sized dimensions collapse to `[]` per dimension, as in
    // the postfix cast / DDL.
    auto two = parser.parse("SELECT CAST(x AS int[][])");
    ASSERT_TRUE(two.has_value());
    EXPECT_TRUE(contains_text(find_node_type(two.value(), NodeType::CastExpr), "int[][]"));
    parser.reset();
    auto sized = parser.parse("SELECT CAST(x AS int[3])");
    ASSERT_TRUE(sized.has_value());
    EXPECT_TRUE(contains_text(find_node_type(sized.value(), NodeType::CastExpr), "int[]"));
    parser.reset();

    // No silent truncation: a following select item AND a FROM/WHERE tail survive.
    auto multi = parser.parse("SELECT x::int[], CAST(x AS int[]) FROM t WHERE y = 1");
    ASSERT_TRUE(multi.has_value());
    auto* list = find_node_type(multi.value(), NodeType::SelectList);
    ASSERT_NE(list, nullptr);
    int item_count = 0;
    for (auto* c = list->first_child; c; c = c->next_sibling) ++item_count;
    EXPECT_EQ(item_count, 2) << "the CAST(x AS int[]) select item must not be dropped";
    EXPECT_NE(find_node_type(multi.value(), NodeType::FromClause), nullptr);
    EXPECT_NE(find_node_type(multi.value(), NodeType::WhereClause), nullptr);
}

// DATE / TIME / TIMESTAMP '<literal>' must parse as a DateTimeLiteral that KEEPS
// its value (the value string was previously dropped on the floor), and
// CURRENT_DATE / CURRENT_TIME / CURRENT_TIMESTAMP as niladic FunctionCalls (not
// columns). Both are then valid EXTRACT operands - a typed-literal or niladic
// EXTRACT operand used to fail to parse entirely.
TEST_F(AdvancedTypesTest, TemporalLiteralsAndNiladicFunctions) {
    // DATE literal -> DateTimeLiteral carrying its value.
    auto d = parser.parse("SELECT DATE '2020-01-01'");
    ASSERT_TRUE(d.has_value());
    auto* dl = find_node_type(d.value(), NodeType::DateTimeLiteral);
    ASSERT_NE(dl, nullptr) << "DATE '...' must be a DateTimeLiteral";
    EXPECT_TRUE(contains_text(dl, "2020-01-01")) << "the date value must be preserved";
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT TIMESTAMP '2020-01-01 00:00:00'").has_value());
    parser.reset();

    // CURRENT_DATE / CURRENT_TIMESTAMP / CURRENT_TIME -> a niladic FunctionCall,
    // not a column, with no dangling tokens.
    for (const char* sql : {"SELECT CURRENT_DATE", "SELECT current_timestamp",
                            "SELECT CURRENT_TIME"}) {
        auto r = parser.parse(sql);
        ASSERT_TRUE(r.has_value()) << sql;
        auto* list = find_node_type(r.value(), NodeType::SelectList);
        ASSERT_NE(list, nullptr);
        ASSERT_NE(list->first_child, nullptr);
        EXPECT_EQ(list->first_child->node_type, NodeType::FunctionCall)
            << sql << " must be a niladic FunctionCall, not a column";
        parser.reset();
    }

    // A DELIMITED identifier "current_date" is an ordinary column, NOT the
    // niladic function - the double quotes are exactly how SQL forces the
    // column reading. The tokenizer flags it; the parser must not promote it.
    for (const char* sql : {"SELECT \"current_date\" FROM t",
                            "SELECT \"CURRENT_TIMESTAMP\" FROM t"}) {
        auto r = parser.parse(sql);
        ASSERT_TRUE(r.has_value()) << sql;
        auto* list = find_node_type(r.value(), NodeType::SelectList);
        ASSERT_NE(list, nullptr);
        ASSERT_NE(list->first_child, nullptr);
        EXPECT_NE(list->first_child->node_type, NodeType::FunctionCall)
            << sql << " is a delimited identifier (a column), not a niladic function";
        parser.reset();
    }

    // EXTRACT over a typed literal / niladic function / arithmetic now parses.
    for (const char* sql : {
            "SELECT EXTRACT(YEAR FROM DATE '2020-01-01')",
            "SELECT EXTRACT(DAY FROM INTERVAL '3 days')",
            "SELECT EXTRACT(YEAR FROM CURRENT_DATE)",
            "SELECT EXTRACT(HOUR FROM ts + INTERVAL '1 day') FROM t"}) {
        EXPECT_TRUE(parser.parse(sql).has_value()) << sql;
        parser.reset();
    }

    // Guards: a bare `date` column and CAST(x AS DATE) are unchanged (the
    // typed-literal branch only fires when a string follows the type keyword).
    EXPECT_TRUE(parser.parse("SELECT date FROM t").has_value());
    parser.reset();
    EXPECT_TRUE(parser.parse("SELECT CAST(x AS DATE) FROM t").has_value());
}

// The PRECISION forms CURRENT_TIME(p) / CURRENT_TIMESTAMP(p) take a
// parenthesized argument and are ordinary function calls - the niladic
// promotion must NOT swallow them, which would drop the argument list AND every
// following clause (FROM/WHERE/...) while parse() still reported success.
TEST_F(AdvancedTypesTest, NiladicDatetimePrecisionFormIsAFunctionCall) {
    for (const char* sql : {"SELECT CURRENT_TIMESTAMP(6) FROM t",
                            "SELECT current_timestamp(6) FROM t",
                            "SELECT CURRENT_TIME(2) FROM t"}) {
        auto r = parser.parse(sql);
        ASSERT_TRUE(r.has_value()) << sql;
        // The select item is a FunctionCall carrying the precision argument.
        auto* list = find_node_type(r.value(), NodeType::SelectList);
        ASSERT_NE(list, nullptr) << sql;
        ASSERT_NE(list->first_child, nullptr) << sql;
        EXPECT_EQ(list->first_child->node_type, NodeType::FunctionCall) << sql;
        EXPECT_NE(list->first_child->first_child, nullptr)
            << sql << ": the precision argument must be a child, not dropped";
        // The FROM clause after the argument list must survive.
        EXPECT_NE(find_node_type(r.value(), NodeType::FromClause), nullptr)
            << sql << ": the FROM clause after CURRENT_TIMESTAMP(p) was dropped";
        parser.reset();
    }
}

// ============================================================================
// Summary Test
// ============================================================================

TEST_F(AdvancedTypesTest, TypeSupportSummary) {
    struct TypeTest {
        std::string name;
        std::string sql;
        bool expected_to_parse;
        std::string context;
    };
    
    std::vector<TypeTest> tests = {
        // DDL Context - Generally well supported
        {"INTERVAL in DDL", "CREATE TABLE t (d INTERVAL)", true, "DDL"},
        {"ARRAY in DDL", "CREATE TABLE t (arr INTEGER[])", true, "DDL"},
        {"JSON in DDL", "CREATE TABLE t (data JSON)", true, "DDL"},
        
        // Expression Context - Limited support
        {"INTERVAL literal", "SELECT INTERVAL '1 day'", true, "Expression"},
        {"ARRAY constructor", "SELECT ARRAY[1,2,3]", true, "Expression"},
        {"ROW constructor", "SELECT ROW(1,2,3)", true, "Expression"},
        {"JSON operator ->", "SELECT data->'key' FROM t", false, "Expression"},
        {"JSON operator ->>", "SELECT data->>'key' FROM t", false, "Expression"},
    };
    
    int ddl_passed = 0, ddl_total = 0;
    int expr_passed = 0, expr_total = 0;
    
    for (const auto& test : tests) {
        parser.reset();
        auto result = parser.parse(test.sql);
        bool parsed = result.has_value();
        
        if (test.context == "DDL") {
            ddl_total++;
            if (parsed) ddl_passed++;
        } else {
            expr_total++;
            if (parsed) expr_passed++;
        }
        
        if (parsed != test.expected_to_parse) {
            ADD_FAILURE() << test.name << " - Expected to " 
                         << (test.expected_to_parse ? "parse" : "fail")
                         << " but got opposite result";
        }
    }
    
    // Report summary
    std::cout << "\n=== Advanced Type Support Summary ===" << std::endl;
    std::cout << "DDL Context: " << ddl_passed << "/" << ddl_total 
              << " (" << (ddl_passed * 100 / ddl_total) << "%)" << std::endl;
    std::cout << "Expression Context: " << expr_passed << "/" << expr_total 
              << " (" << (expr_passed * 100 / expr_total) << "%)" << std::endl;
    
    // Current status assessment
    EXPECT_GE(ddl_passed * 100 / ddl_total, 90) 
        << "DDL context should have >90% support";
    EXPECT_GE(expr_passed * 100 / expr_total, 60) 
        << "Expression context should have >60% support";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}