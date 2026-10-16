#include <gtest/gtest.h>
#include "db25/ast/ast_node.hpp"
#include "db25/ast/node_types.hpp"
#include <cstdint>
#include <map>
#include <string>

using namespace db25::ast;

TEST(ASTNodeTest, SizeAndAlignment) {
    // The node is a 128-byte, 128-byte-aligned (two cache lines) structure; this
    // is also static_assert-enforced in ast_node.hpp.
    EXPECT_EQ(sizeof(ASTNode), 128);
    EXPECT_EQ(alignof(ASTNode), 128);
}

TEST(ASTNodeTest, ChildCountSaturatesInsteadOfWrapping) {
    // The 2-byte child_count must saturate at 0xFFFF, never wrap. A plain uint16_t
    // wraps modulo 65536 on a node with >= 65536 direct children (machine-generated
    // SELECT / IN / VALUES lists); exactly 65536 wraps to 0, which would make
    // get_children()'s `child_count == 0` early-out drop a fully-linked child list.
    EXPECT_EQ(sizeof(SaturatingChildCount), 2u);

    SaturatingChildCount c;
    EXPECT_EQ(static_cast<uint16_t>(c), 0);
    for (int i = 0; i < 65534; ++i) { c++; }
    EXPECT_EQ(static_cast<uint16_t>(c), 65534);   // exact up to 65534
    c++;
    EXPECT_EQ(static_cast<uint16_t>(c), 65535);   // reaches the sentinel
    c++;                                          // would wrap to 0 without saturation
    EXPECT_EQ(static_cast<uint16_t>(c), 65535);   // stuck at 0xFFFF
    for (int i = 0; i < 1000; ++i) { c++; }
    EXPECT_EQ(static_cast<uint16_t>(c), 65535);   // stays saturated
    --c;                                          // saturated count is unknown: sticky
    EXPECT_EQ(static_cast<uint16_t>(c), 65535);

    // Assignment of a small constant (the `child_count = 1/2/3` builder sites) and
    // ordinary decrement below the sentinel behave like a plain counter.
    c = 3;
    EXPECT_EQ(static_cast<uint16_t>(c), 3);
    --c;
    EXPECT_EQ(static_cast<uint16_t>(c), 2);
}

TEST(ASTNodeTest, AddChildSaturatesAndKeepsListIntact) {
    // add_child() bumps child_count; past the sentinel the cached count saturates
    // but the sibling list stays fully linked, so get_children() still returns every
    // child. This is the metadata-vs-traversal invariant the wrap would have broken.
    ASTNode parent(NodeType::SelectList);
    std::vector<ASTNode> kids(3);
    for (auto& k : kids) { parent.add_child(&k); }
    EXPECT_EQ(parent.child_count, 3);
    EXPECT_EQ(parent.get_children().size(), 3u);

    // Force the counter to the sentinel, then add one more real child.
    parent.child_count = SaturatingChildCount::kSaturated;
    ASTNode extra(NodeType::ColumnRef);
    parent.add_child(&extra);
    EXPECT_EQ(parent.child_count, SaturatingChildCount::kSaturated);  // did not wrap to 0
    EXPECT_EQ(parent.get_children().size(), 4u);                      // list still complete
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
// ---------------------------------------------------------------------------
// node_type_to_string: TOTAL and INJECTIVE over the whole enum.
//
// Found by the project's first line-coverage measurement: ast_node.cpp sat at
// 26.6%, and almost every unexecuted line was an arm of this switch. The naive
// reading is "write a test per arm", which would be filler - a name table is
// not made trustworthy by calling it 183 times.
//
// The two properties below are what can actually be WRONG with a hand-written
// mapping of this shape, and both are invisible to the parser tests that
// incidentally hit a few arms:
//
//   TOTALITY   - a NodeType added to the enum but not to the switch falls
//                through to the default and reports as something else. Every
//                diagnostic and every s-expr dump naming that node is then
//                silently wrong.
//   INJECTIVITY - two arms returning the same string is the classic
//                copy-paste slip in a 183-case switch, and it makes two
//                distinct node kinds indistinguishable in every dump.
TEST(ASTNodeTest, NodeTypeToStringIsTotal) {
    for (std::uint16_t v = 0; v < static_cast<std::uint16_t>(NodeType::MaxNodeType); ++v) {
        const auto t = static_cast<NodeType>(v);
        const char* s = node_type_to_string(t);
        ASSERT_NE(s, nullptr) << "NodeType " << v << " maps to nullptr";
        EXPECT_NE(std::string{s}, "") << "NodeType " << v << " maps to the empty string";
        if (t != NodeType::Unknown) {
            EXPECT_NE(std::string{s}, "Unknown")
                << "NodeType " << v << " is in the enum but not in the switch: it "
                   "falls through to the Unknown arm";
        }
    }
}

TEST(ASTNodeTest, NodeTypeToStringIsInjective) {
    std::map<std::string, int> seen;
    for (std::uint16_t v = 0; v < static_cast<std::uint16_t>(NodeType::MaxNodeType); ++v) {
        const std::string s = node_type_to_string(static_cast<NodeType>(v));
        const auto [it, fresh] = seen.emplace(s, v);
        EXPECT_TRUE(fresh) << "NodeType " << v << " and NodeType " << it->second
                           << " both render as \"" << s << "\"";
    }
}

// ---------------------------------------------------------------------------
// remove_child: the three positions, plus the count.
//
// Also surfaced by the coverage measurement - the whole middle-of-list branch
// (the walk that finds the predecessor) was never executed by any of the
// thirty-nine test binaries. It is real pointer surgery on a singly-linked
// sibling chain, which is exactly the shape that goes wrong silently: an
// unlinked node that is still reachable, or a chain truncated at the removal
// point, both leave a tree that LOOKS fine until something walks it.
TEST(ASTNodeTest, RemoveChildUnlinksAtEveryPosition) {
    const auto names = [](const ASTNode& p) {
        std::string out;
        for (const ASTNode* c = p.first_child; c != nullptr; c = c->next_sibling)
            out += std::to_string(static_cast<int>(c->node_type)) + ",";
        return out;
    };

    {  // FIRST child: parent's head pointer must advance.
        ASTNode p(NodeType::SelectList);
        ASTNode a(NodeType::ColumnRef), b(NodeType::BinaryExpr), c(NodeType::IntegerLiteral);
        p.add_child(&a); p.add_child(&b); p.add_child(&c);
        p.remove_child(&a);
        EXPECT_EQ(p.first_child, &b);
        EXPECT_EQ(p.child_count.value, 2u);
        EXPECT_EQ(a.parent, nullptr);
        EXPECT_EQ(a.next_sibling, nullptr);       // fully unlinked, not just skipped
    }
    {  // MIDDLE child: the predecessor walk - the branch coverage showed unused.
        ASTNode p(NodeType::SelectList);
        ASTNode a(NodeType::ColumnRef), b(NodeType::BinaryExpr), c(NodeType::IntegerLiteral);
        p.add_child(&a); p.add_child(&b); p.add_child(&c);
        p.remove_child(&b);
        EXPECT_EQ(p.first_child, &a);
        EXPECT_EQ(a.next_sibling, &c) << "the predecessor must be stitched to b's successor";
        EXPECT_EQ(p.child_count.value, 2u);
        EXPECT_EQ(b.parent, nullptr);
        EXPECT_EQ(b.next_sibling, nullptr);
    }
    {  // LAST child: the chain must TERMINATE, not dangle at the removed node.
        ASTNode p(NodeType::SelectList);
        ASTNode a(NodeType::ColumnRef), b(NodeType::BinaryExpr), c(NodeType::IntegerLiteral);
        p.add_child(&a); p.add_child(&b); p.add_child(&c);
        p.remove_child(&c);
        EXPECT_EQ(b.next_sibling, nullptr) << "removing the tail must null the new tail's link";
        EXPECT_EQ(p.child_count.value, 2u);
    }
    {  // ONLY child: the list goes empty.
        ASTNode p(NodeType::SelectList);
        ASTNode a(NodeType::ColumnRef);
        p.add_child(&a);
        p.remove_child(&a);
        EXPECT_EQ(p.first_child, nullptr);
        EXPECT_EQ(p.child_count.value, 0u);
    }
}
