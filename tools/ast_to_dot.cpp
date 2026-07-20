/*
 * Copyright (c) 2024 DB25 Parser Project
 *
 * ast_to_dot - Emit a Graphviz DOT graph of the parsed AST.
 *
 * Usage:
 *   ast_to_dot "SELECT ..."            # SQL as first argument
 *   echo "SELECT ..." | ast_to_dot     # SQL on stdin
 *
 * The output is intentionally monochrome and restrained so the rendered
 * diagrams read as documentation figures rather than decorative graphics.
 */

#include <db25/parser/parser.hpp>

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

using db25::ast::ASTNode;
using db25::ast::NodeType;

namespace {

// Local node-type name table. The library's node_type_to_string is a
// constexpr defined in a .cpp and is not linkable from here, so the tool
// carries its own compact mapping.
const char* type_name(NodeType t) {
    switch (t) {
        case NodeType::SelectStmt:        return "SelectStmt";
        case NodeType::InsertStmt:        return "InsertStmt";
        case NodeType::UpdateStmt:        return "UpdateStmt";
        case NodeType::DeleteStmt:        return "DeleteStmt";
        case NodeType::CreateTableStmt:   return "CreateTableStmt";
        case NodeType::CreateIndexStmt:   return "CreateIndexStmt";
        case NodeType::CreateViewStmt:    return "CreateViewStmt";
        case NodeType::AlterTableStmt:    return "AlterTableStmt";
        case NodeType::DropStmt:          return "DropStmt";
        case NodeType::TransactionStmt:   return "TransactionStmt";
        case NodeType::ValuesStmt:        return "ValuesStmt";

        case NodeType::SelectList:        return "SelectList";
        case NodeType::FromClause:        return "FromClause";
        case NodeType::JoinClause:        return "JoinClause";
        case NodeType::WhereClause:       return "WhereClause";
        case NodeType::GroupByClause:     return "GroupByClause";
        case NodeType::HavingClause:      return "HavingClause";
        case NodeType::OrderByClause:     return "OrderByClause";
        case NodeType::LimitClause:       return "LimitClause";
        case NodeType::WithClause:        return "WithClause";
        case NodeType::ReturningClause:   return "ReturningClause";
        case NodeType::UsingClause:       return "UsingClause";
        case NodeType::ValuesClause:      return "ValuesClause";
        case NodeType::SetClause:         return "SetClause";
        case NodeType::GroupingElement:   return "GroupingElement";
        case NodeType::RowExpr:           return "RowExpr";

        case NodeType::BinaryExpr:        return "BinaryExpr";
        case NodeType::UnaryExpr:         return "UnaryExpr";
        case NodeType::FunctionCall:      return "FunctionCall";
        case NodeType::FunctionExpr:      return "FunctionExpr";
        case NodeType::CaseExpr:          return "CaseExpr";
        case NodeType::CastExpr:          return "CastExpr";
        case NodeType::SubqueryExpr:      return "SubqueryExpr";
        case NodeType::ExistsExpr:        return "ExistsExpr";
        case NodeType::InExpr:            return "InExpr";
        case NodeType::BetweenExpr:       return "BetweenExpr";
        case NodeType::LikeExpr:          return "LikeExpr";
        case NodeType::IsNullExpr:        return "IsNullExpr";
        case NodeType::WindowExpr:        return "WindowExpr";

        case NodeType::TableRef:          return "TableRef";
        case NodeType::ColumnRef:         return "ColumnRef";
        case NodeType::AliasRef:          return "AliasRef";
        case NodeType::SchemaRef:         return "SchemaRef";
        case NodeType::ColumnList:        return "ColumnList";

        case NodeType::IntegerLiteral:    return "IntegerLiteral";
        case NodeType::FloatLiteral:      return "FloatLiteral";
        case NodeType::StringLiteral:     return "StringLiteral";
        case NodeType::BooleanLiteral:    return "BooleanLiteral";
        case NodeType::NullLiteral:       return "NullLiteral";
        case NodeType::DateTimeLiteral:   return "DateTimeLiteral";
        case NodeType::IntervalLiteral:   return "IntervalLiteral";
        case NodeType::ArrayConstructor:  return "ArrayConstructor";

        case NodeType::Identifier:        return "Identifier";
        case NodeType::Star:              return "Star";
        case NodeType::Parameter:         return "Parameter";

        case NodeType::Subquery:          return "Subquery";
        case NodeType::CTEClause:         return "CTEClause";
        case NodeType::CTEDefinition:     return "CTEDefinition";

        case NodeType::WindowFunction:    return "WindowFunction";
        case NodeType::WindowSpec:        return "WindowSpec";
        case NodeType::PartitionBy:       return "PartitionBy";
        case NodeType::PartitionByClause: return "PartitionBy";
        case NodeType::WindowFrame:       return "WindowFrame";
        case NodeType::FrameClause:       return "FrameClause";
        case NodeType::FrameBound:        return "FrameBound";

        case NodeType::ColumnDefinition:  return "ColumnDefinition";
        case NodeType::DataTypeNode:      return "DataType";
        case NodeType::TableConstraint:   return "TableConstraint";
        case NodeType::ColumnConstraint:  return "ColumnConstraint";
        case NodeType::CreateTriggerStmt: return "CreateTriggerStmt";
        case NodeType::CreateSchemaStmt:  return "CreateSchemaStmt";
        case NodeType::TruncateStmt:      return "TruncateStmt";
        case NodeType::ExplainStmt:       return "ExplainStmt";
        case NodeType::SetStmt:           return "SetStmt";
        case NodeType::SearchClause:      return "SearchClause";
        case NodeType::CycleClause:       return "CycleClause";

        case NodeType::UnionStmt:         return "UnionStmt";
        case NodeType::IntersectStmt:     return "IntersectStmt";
        case NodeType::ExceptStmt:        return "ExceptStmt";
        default:                          return "Node";
    }
}

// Node categories drive a subtle, colour-free style difference (fill shade
// and border weight only) so the figures stay legible in print.
enum class Category { Statement, Clause, Expr, Ref, Literal, Other };

Category category(NodeType t) {
    switch (t) {
        case NodeType::SelectStmt: case NodeType::InsertStmt: case NodeType::UpdateStmt:
        case NodeType::DeleteStmt: case NodeType::CreateTableStmt: case NodeType::CreateViewStmt:
        case NodeType::CreateIndexStmt: case NodeType::AlterTableStmt: case NodeType::DropStmt:
        case NodeType::TransactionStmt: case NodeType::ValuesStmt:
        case NodeType::UnionStmt: case NodeType::IntersectStmt: case NodeType::ExceptStmt:
            return Category::Statement;
        case NodeType::SelectList: case NodeType::FromClause: case NodeType::JoinClause:
        case NodeType::WhereClause: case NodeType::GroupByClause: case NodeType::HavingClause:
        case NodeType::OrderByClause: case NodeType::LimitClause: case NodeType::WithClause:
        case NodeType::ReturningClause: case NodeType::UsingClause: case NodeType::ValuesClause:
        case NodeType::SetClause: case NodeType::CTEClause: case NodeType::WindowSpec:
        case NodeType::PartitionBy: case NodeType::PartitionByClause: case NodeType::WindowFrame:
        case NodeType::FrameClause:
            return Category::Clause;
        case NodeType::BinaryExpr: case NodeType::UnaryExpr: case NodeType::FunctionCall:
        case NodeType::FunctionExpr: case NodeType::CaseExpr: case NodeType::CastExpr:
        case NodeType::SubqueryExpr: case NodeType::ExistsExpr: case NodeType::InExpr:
        case NodeType::BetweenExpr: case NodeType::LikeExpr: case NodeType::IsNullExpr:
        case NodeType::WindowExpr: case NodeType::WindowFunction: case NodeType::RowExpr:
        case NodeType::ArrayConstructor:
            return Category::Expr;
        case NodeType::TableRef: case NodeType::ColumnRef: case NodeType::AliasRef:
        case NodeType::SchemaRef: case NodeType::Identifier: case NodeType::Star:
        case NodeType::ColumnList: case NodeType::Parameter:
            return Category::Ref;
        case NodeType::IntegerLiteral: case NodeType::FloatLiteral: case NodeType::StringLiteral:
        case NodeType::BooleanLiteral: case NodeType::NullLiteral: case NodeType::DateTimeLiteral:
        case NodeType::IntervalLiteral:
            return Category::Literal;
        default:
            return Category::Other;
    }
}

std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '&':  out += "&amp;"; break;
            default:   out += c;
        }
    }
    return out;
}

void emit_node(std::ostream& os, const ASTNode* n) {
    if (!n) return;

    std::string label = type_name(n->node_type);
    if (!n->primary_text.empty()) {
        label += "\\n";
        label += escape(n->primary_text);
    }
    if (!n->schema_name.empty()) {
        label += "\\n(";  // alias / qualifier is repurposed into schema_name
        label += escape(n->schema_name);
        label += ")";
    }

    const char* fill;
    const char* penwidth;
    switch (category(n->node_type)) {
        case Category::Statement: fill = "#e8e8e8"; penwidth = "1.6"; break;
        case Category::Clause:    fill = "#f2f2f2"; penwidth = "1.1"; break;
        case Category::Expr:      fill = "#ffffff"; penwidth = "1.1"; break;
        case Category::Ref:       fill = "#fbfbfb"; penwidth = "0.8"; break;
        case Category::Literal:   fill = "#ffffff"; penwidth = "0.8"; break;
        default:                  fill = "#ffffff"; penwidth = "0.8"; break;
    }

    os << "  n" << n->node_id << " [label=\"" << label << "\", fillcolor=\""
       << fill << "\", penwidth=" << penwidth << "];\n";

    for (const ASTNode* c = n->first_child; c; c = c->next_sibling) {
        os << "  n" << n->node_id << " -> n" << c->node_id << ";\n";
        emit_node(os, c);
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string sql;
    if (argc > 1) {
        sql = argv[1];
    } else {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        sql = ss.str();
    }

    db25::parser::Parser parser;
    auto result = parser.parse(sql);
    if (!result) {
        std::cerr << "parse error: " << result.error().message << "\n";
        return 1;
    }

    std::ostream& os = std::cout;
    os << "digraph AST {\n";
    os << "  graph [rankdir=TB, bgcolor=\"white\", nodesep=0.28, ranksep=0.42, "
          "fontname=\"Helvetica\"];\n";
    os << "  node  [shape=box, style=\"rounded,filled\", color=\"#333333\", "
          "fontname=\"Helvetica\", fontsize=10, margin=\"0.09,0.05\"];\n";
    os << "  edge  [color=\"#888888\", arrowsize=0.6, penwidth=0.9];\n";
    emit_node(os, result.value());
    os << "}\n";
    return 0;
}
