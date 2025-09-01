#include <gtest/gtest.h>
#include "db25/ast/ast_node.hpp"
#include "db25/ast/node_types.hpp"

using namespace db25::ast;

TEST(ASTNodeTest, SizeAndAlignment) {
    EXPECT_EQ(sizeof(ASTNode), 64);
    EXPECT_EQ(alignof(ASTNode), 64);
}

TEST(ASTNodeTest, DefaultConstruction) {
    const ASTNode node;
    
    EXPECT_EQ(node.node_type, NodeType::Unknown);
    EXPECT_EQ(node.flags, NodeFlags::None);
    EXPECT_EQ(node.child_count, 0);
    EXPECT_EQ(node.node_id, 0);
    EXPECT_EQ(node.source_start, 0);
    EXPECT_EQ(node.source_end, 0);
    EXPECT_EQ(node.parent, nullptr);
    EXPECT_EQ(node.first_child, nullptr);
    EXPECT_EQ(node.next_sibling, nullptr);
}

TEST(ASTNodeTest, TypeConstruction) {
    const ASTNode node(NodeType::SelectStmt);
    
    EXPECT_EQ(node.node_type, NodeType::SelectStmt);
    EXPECT_EQ(node.flags, NodeFlags::None);
    EXPECT_EQ(node.child_count, 0);
}

TEST(ASTNodeTest, TypeCheckers) {
    {
        const ASTNode stmt(NodeType::SelectStmt);
        EXPECT_TRUE(stmt.is_statement());
        EXPECT_FALSE(stmt.is_expression());
        EXPECT_FALSE(stmt.is_literal());
    }
    
    {
        const ASTNode expr(NodeType::BinaryExpr);
        EXPECT_FALSE(expr.is_statement());
        EXPECT_TRUE(expr.is_expression());
        EXPECT_FALSE(expr.is_literal());
    }
    
    {
        const ASTNode lit(NodeType::IntegerLiteral);
        EXPECT_FALSE(lit.is_statement());
        EXPECT_FALSE(lit.is_expression());
        EXPECT_TRUE(lit.is_literal());
    }
}

TEST(ASTNodeTest, FlagOperations) {
    ASTNode node;
    
    EXPECT_FALSE(node.has_flag(NodeFlags::Distinct));
    
    node.set_flag(NodeFlags::Distinct);
    EXPECT_TRUE(node.has_flag(NodeFlags::Distinct));
    
    node.set_flag(NodeFlags::HasAlias);
    EXPECT_TRUE(node.has_flag(NodeFlags::Distinct));
    EXPECT_TRUE(node.has_flag(NodeFlags::HasAlias));
    
    node.clear_flag(NodeFlags::Distinct);
    EXPECT_FALSE(node.has_flag(NodeFlags::Distinct));
    EXPECT_TRUE(node.has_flag(NodeFlags::HasAlias));
}

TEST(ASTNodeTest, SourceLocation) {
    ASTNode node;
    node.source_start = 10;
    node.source_end = 25;
    
    const auto [start, end] = node.get_source_range();
    EXPECT_EQ(start, 10u);
    EXPECT_EQ(end, 25u);
    
    EXPECT_EQ(node.get_source_length(), 15u);
}

TEST(ASTNodeTest, TreeNavigation) {
    ASTNode parent(NodeType::SelectStmt);
    ASTNode child1(NodeType::SelectList);
    ASTNode child2(NodeType::FromClause);
    ASTNode child3(NodeType::WhereClause);
    
    parent.add_child(&child1);
    parent.add_child(&child2);
    parent.add_child(&child3);
    
    EXPECT_EQ(parent.child_count, 3);
    EXPECT_EQ(parent.first_child, &child1);
    
    EXPECT_EQ(child1.parent, &parent);
    EXPECT_EQ(child1.next_sibling, &child2);
    
    EXPECT_EQ(child2.parent, &parent);
    EXPECT_EQ(child2.next_sibling, &child3);
    
    EXPECT_EQ(child3.parent, &parent);
    EXPECT_EQ(child3.next_sibling, nullptr);
}

TEST(ASTNodeTest, FindChild) {
    ASTNode parent(NodeType::SelectStmt);
    ASTNode select_list(NodeType::SelectList);
    ASTNode from_clause(NodeType::FromClause);
    ASTNode where_clause(NodeType::WhereClause);
    
    parent.add_child(&select_list);
    parent.add_child(&from_clause);
    parent.add_child(&where_clause);
    
    EXPECT_EQ(parent.find_child(NodeType::SelectList), &select_list);
    EXPECT_EQ(parent.find_child(NodeType::FromClause), &from_clause);
    EXPECT_EQ(parent.find_child(NodeType::WhereClause), &where_clause);
    EXPECT_EQ(parent.find_child(NodeType::OrderByClause), nullptr);
}

TEST(ASTNodeTest, RemoveChild) {
    ASTNode parent(NodeType::SelectStmt);
    ASTNode child1(NodeType::SelectList);
    ASTNode child2(NodeType::FromClause);
    ASTNode child3(NodeType::WhereClause);
    
    parent.add_child(&child1);
    parent.add_child(&child2);
    parent.add_child(&child3);
    
    EXPECT_EQ(parent.child_count, 3);
    
    parent.remove_child(&child2);
    
    EXPECT_EQ(parent.child_count, 2);
    EXPECT_EQ(parent.first_child, &child1);
    EXPECT_EQ(child1.next_sibling, &child3);
    EXPECT_EQ(child2.parent, nullptr);
    EXPECT_EQ(child2.next_sibling, nullptr);
}

TEST(ASTNodeTest, GetChildren) {
    ASTNode parent(NodeType::SelectStmt);
    ASTNode child1(NodeType::SelectList);
    ASTNode child2(NodeType::FromClause);
    ASTNode child3(NodeType::WhereClause);
    
    parent.add_child(&child1);
    parent.add_child(&child2);
    parent.add_child(&child3);
    
    auto children = parent.get_children();
    EXPECT_EQ(children.size(), 3u);
    EXPECT_EQ(children[0], &child1);
    EXPECT_EQ(children[1], &child2);
    EXPECT_EQ(children[2], &child3);
}

TEST(NodeTypeTest, ToString) {
    EXPECT_STREQ(node_type_to_string(NodeType::SelectStmt), "SelectStmt");
    EXPECT_STREQ(node_type_to_string(NodeType::BinaryExpr), "BinaryExpr");
    EXPECT_STREQ(node_type_to_string(NodeType::IntegerLiteral), "IntegerLiteral");
}

TEST(BinaryOpTest, ToString) {
    EXPECT_STREQ(binary_op_to_string(BinaryOp::Add), "+");
    EXPECT_STREQ(binary_op_to_string(BinaryOp::Equal), "=");
    EXPECT_STREQ(binary_op_to_string(BinaryOp::And), "AND");
}

TEST(UnaryOpTest, ToString) {
    EXPECT_STREQ(unary_op_to_string(UnaryOp::Not), "NOT");
    EXPECT_STREQ(unary_op_to_string(UnaryOp::Negate), "-");
    EXPECT_STREQ(unary_op_to_string(UnaryOp::IsNull), "IS NULL");
}

TEST(DataTypeTest, ToString) {
    EXPECT_STREQ(data_type_to_string(DataType::Integer), "Integer");
    EXPECT_STREQ(data_type_to_string(DataType::VarChar), "VarChar");
    EXPECT_STREQ(data_type_to_string(DataType::Boolean), "Boolean");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}