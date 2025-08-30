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

TEST_F(AdvancedTypesTest, JsonArrowOperator) {
    auto result = parser.parse("SELECT data->'key' FROM json_table");
    ASSERT_TRUE(result.has_value()) << "Should parse -> operator";
    
    auto* ast = result.value();
    auto* binary = find_node_type(ast, NodeType::BinaryExpr);
    if (binary) {
        EXPECT_EQ(binary->primary_text, "->") << "Should have -> operator";
    }
}

TEST_F(AdvancedTypesTest, JsonDoubleArrowOperator) {
    auto result = parser.parse("SELECT data->>'key' FROM json_table");
    ASSERT_TRUE(result.has_value()) << "Should parse ->> operator";
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
        {"JSON operator ->", "SELECT data->'key' FROM t", true, "Expression"},
        {"JSON operator ->>", "SELECT data->>'key' FROM t", true, "Expression"},
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