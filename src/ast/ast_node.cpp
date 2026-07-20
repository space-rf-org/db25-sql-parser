#include "db25/ast/ast_node.hpp"
#include <cassert>
#include <vector>

namespace db25::ast {

std::span<ASTNode*> ASTNode::get_children() const noexcept {
    if (child_count == 0 || !first_child) {
        return {};
    }
    
    static thread_local std::vector<ASTNode*> children_buffer;
    children_buffer.clear();
    children_buffer.reserve(child_count);
    
    ASTNode* current = first_child;
    while (current) {
        children_buffer.push_back(current);
        current = current->next_sibling;
    }
    
    return {children_buffer.data(), children_buffer.size()};
}

void ASTNode::add_child(ASTNode* const child) noexcept {
    assert(child != nullptr);
    assert(child->parent == nullptr);
    assert(child->next_sibling == nullptr);
    
    child->parent = this;
    
    if (!first_child) {
        first_child = child;
    } else {
        ASTNode* last = first_child;
        while (last->next_sibling) {
            last = last->next_sibling;
        }
        last->next_sibling = child;
    }
    
    ++child_count;
}

void ASTNode::remove_child(ASTNode* const child) noexcept {
    assert(child != nullptr);
    assert(child->parent == this);
    
    if (first_child == child) {
        first_child = child->next_sibling;
    } else {
        ASTNode* current = first_child;
        while (current && current->next_sibling != child) {
            current = current->next_sibling;
        }
        if (current) {
            current->next_sibling = child->next_sibling;
        }
    }
    
    child->parent = nullptr;
    child->next_sibling = nullptr;
    --child_count;
}

[[nodiscard]] ASTNode* ASTNode::find_child(const NodeType type) const noexcept {
    ASTNode* current = first_child;
    while (current) {
        if (current->node_type == type) {
            return current;
        }
        current = current->next_sibling;
    }
    return nullptr;
}

// NOTE: this switch must cover every NodeType enumerator. It is compiled with
// -Wswitch, so adding a new enumerator without a case here produces a warning
// rather than silently returning "Unknown". Keep it in sync with node_types.hpp.
[[nodiscard]] const char* node_type_to_string(const NodeType type) noexcept {
    switch (type) {
        case NodeType::Unknown: return "Unknown";
        // Statements
        case NodeType::SelectStmt: return "SelectStmt";
        case NodeType::InsertStmt: return "InsertStmt";
        case NodeType::UpdateStmt: return "UpdateStmt";
        case NodeType::DeleteStmt: return "DeleteStmt";
        case NodeType::CreateTableStmt: return "CreateTableStmt";
        case NodeType::CreateIndexStmt: return "CreateIndexStmt";
        case NodeType::CreateViewStmt: return "CreateViewStmt";
        case NodeType::AlterTableStmt: return "AlterTableStmt";
        case NodeType::DropStmt: return "DropStmt";
        case NodeType::TruncateStmt: return "TruncateStmt";
        case NodeType::TransactionStmt: return "TransactionStmt";
        case NodeType::ExplainStmt: return "ExplainStmt";
        case NodeType::ValuesStmt: return "ValuesStmt";
        case NodeType::SetStmt: return "SetStmt";
        case NodeType::VacuumStmt: return "VacuumStmt";
        case NodeType::AnalyzeStmt: return "AnalyzeStmt";
        case NodeType::AttachStmt: return "AttachStmt";
        case NodeType::DetachStmt: return "DetachStmt";
        case NodeType::ReindexStmt: return "ReindexStmt";
        case NodeType::PragmaStmt: return "PragmaStmt";
        case NodeType::BeginStmt: return "BeginStmt";
        case NodeType::CommitStmt: return "CommitStmt";
        case NodeType::RollbackStmt: return "RollbackStmt";
        case NodeType::SavepointStmt: return "SavepointStmt";
        case NodeType::ReleaseSavepointStmt: return "ReleaseSavepointStmt";
        case NodeType::CreateTriggerStmt: return "CreateTriggerStmt";
        case NodeType::CreateSchemaStmt: return "CreateSchemaStmt";
        case NodeType::CreateFunctionStmt: return "CreateFunctionStmt";
        case NodeType::CreateProcedureStmt: return "CreateProcedureStmt";
        case NodeType::AlterSchemaStmt: return "AlterSchemaStmt";
        case NodeType::AlterIndexStmt: return "AlterIndexStmt";
        case NodeType::AlterViewStmt: return "AlterViewStmt";
        // DDL components
        case NodeType::ColumnDefinition: return "ColumnDefinition";
        case NodeType::ColumnConstraint: return "ColumnConstraint";
        case NodeType::TableConstraint: return "TableConstraint";
        case NodeType::DataTypeNode: return "DataTypeNode";
        case NodeType::DefaultClause: return "DefaultClause";
        case NodeType::CheckConstraint: return "CheckConstraint";
        case NodeType::ForeignKeyConstraint: return "ForeignKeyConstraint";
        case NodeType::PrimaryKeyConstraint: return "PrimaryKeyConstraint";
        case NodeType::UniqueConstraint: return "UniqueConstraint";
        case NodeType::IndexColumn: return "IndexColumn";
        case NodeType::AlterTableAction: return "AlterTableAction";
        // Expressions
        case NodeType::BinaryExpr: return "BinaryExpr";
        case NodeType::UnaryExpr: return "UnaryExpr";
        case NodeType::FunctionExpr: return "FunctionExpr";
        case NodeType::FunctionCall: return "FunctionCall";
        case NodeType::CaseExpr: return "CaseExpr";
        case NodeType::CastExpr: return "CastExpr";
        case NodeType::SubqueryExpr: return "SubqueryExpr";
        case NodeType::ExistsExpr: return "ExistsExpr";
        case NodeType::InExpr: return "InExpr";
        case NodeType::BetweenExpr: return "BetweenExpr";
        case NodeType::LikeExpr: return "LikeExpr";
        case NodeType::IsNullExpr: return "IsNullExpr";
        case NodeType::WindowExpr: return "WindowExpr";
        // Clauses
        case NodeType::SelectList: return "SelectList";
        case NodeType::FromClause: return "FromClause";
        case NodeType::JoinClause: return "JoinClause";
        case NodeType::WhereClause: return "WhereClause";
        case NodeType::GroupByClause: return "GroupByClause";
        case NodeType::HavingClause: return "HavingClause";
        case NodeType::OrderByClause: return "OrderByClause";
        case NodeType::LimitClause: return "LimitClause";
        case NodeType::WithClause: return "WithClause";
        case NodeType::ReturningClause: return "ReturningClause";
        case NodeType::OnConflictClause: return "OnConflictClause";
        case NodeType::UsingClause: return "UsingClause";
        case NodeType::ValuesClause: return "ValuesClause";
        case NodeType::GroupingElement: return "GroupingElement";
        case NodeType::SetClause: return "SetClause";
        case NodeType::RowExpr: return "RowExpr";
        // References
        case NodeType::TableRef: return "TableRef";
        case NodeType::ColumnRef: return "ColumnRef";
        case NodeType::AliasRef: return "AliasRef";
        case NodeType::SchemaRef: return "SchemaRef";
        // Literals
        case NodeType::IntegerLiteral: return "IntegerLiteral";
        case NodeType::FloatLiteral: return "FloatLiteral";
        case NodeType::StringLiteral: return "StringLiteral";
        case NodeType::BooleanLiteral: return "BooleanLiteral";
        case NodeType::NullLiteral: return "NullLiteral";
        case NodeType::DateTimeLiteral: return "DateTimeLiteral";
        // Joins
        case NodeType::InnerJoin: return "InnerJoin";
        case NodeType::LeftJoin: return "LeftJoin";
        case NodeType::RightJoin: return "RightJoin";
        case NodeType::FullJoin: return "FullJoin";
        case NodeType::CrossJoin: return "CrossJoin";
        case NodeType::LateralJoin: return "LateralJoin";
        // Other
        case NodeType::Identifier: return "Identifier";
        case NodeType::Star: return "Star";
        case NodeType::Parameter: return "Parameter";
        case NodeType::Comment: return "Comment";
        // Subquery & CTE
        case NodeType::Subquery: return "Subquery";
        case NodeType::CTEClause: return "CTEClause";
        case NodeType::CTEDefinition: return "CTEDefinition";
        case NodeType::ColumnList: return "ColumnList";
        // Window
        case NodeType::WindowFunction: return "WindowFunction";
        case NodeType::WindowSpec: return "WindowSpec";
        case NodeType::PartitionBy: return "PartitionBy";
        case NodeType::PartitionByClause: return "PartitionByClause";
        case NodeType::WindowFrame: return "WindowFrame";
        case NodeType::FrameClause: return "FrameClause";
        case NodeType::FrameBound: return "FrameBound";
        // Set operations
        case NodeType::UnionStmt: return "UnionStmt";
        case NodeType::IntersectStmt: return "IntersectStmt";
        case NodeType::ExceptStmt: return "ExceptStmt";
        // Advanced types
        case NodeType::IntervalLiteral: return "IntervalLiteral";
        case NodeType::ArrayConstructor: return "ArrayConstructor";
        case NodeType::RowConstructor: return "RowConstructor";
        case NodeType::JsonLiteral: return "JsonLiteral";
        case NodeType::CollateClause: return "CollateClause";
        case NodeType::SearchClause: return "SearchClause";
        case NodeType::CycleClause: return "CycleClause";
        case NodeType::IsolationLevel: return "IsolationLevel";
        case NodeType::TransactionMode: return "TransactionMode";
        case NodeType::MaxNodeType: return "Unknown";  // enum-count sentinel
    }
    return "Unknown";  // node_type held a value outside the enum
}

[[nodiscard]] const char* binary_op_to_string(const BinaryOp op) noexcept {
    switch (op) {
        case BinaryOp::Add: return "+";
        case BinaryOp::Subtract: return "-";
        case BinaryOp::Multiply: return "*";
        case BinaryOp::Divide: return "/";
        case BinaryOp::Modulo: return "%";
        case BinaryOp::Equal: return "=";
        case BinaryOp::NotEqual: return "!=";
        case BinaryOp::LessThan: return "<";
        case BinaryOp::LessEqual: return "<=";
        case BinaryOp::GreaterThan: return ">";
        case BinaryOp::GreaterEqual: return ">=";
        case BinaryOp::And: return "AND";
        case BinaryOp::Or: return "OR";
        case BinaryOp::Concat: return "||";
        case BinaryOp::Like: return "LIKE";
        case BinaryOp::NotLike: return "NOT LIKE";
        case BinaryOp::BitAnd: return "&";
        case BinaryOp::BitOr: return "|";
        case BinaryOp::BitXor: return "^";
        case BinaryOp::BitShiftLeft: return "<<";
        case BinaryOp::BitShiftRight: return ">>";
        case BinaryOp::Is: return "IS";
        case BinaryOp::IsNot: return "IS NOT";
        case BinaryOp::In: return "IN";
        case BinaryOp::NotIn: return "NOT IN";
        default: return "Unknown";
    }
}

[[nodiscard]] const char* unary_op_to_string(const UnaryOp op) noexcept {
    switch (op) {
        case UnaryOp::Not: return "NOT";
        case UnaryOp::Negate: return "-";
        case UnaryOp::BitwiseNot: return "~";
        case UnaryOp::IsNull: return "IS NULL";
        case UnaryOp::IsNotNull: return "IS NOT NULL";
        case UnaryOp::Exists: return "EXISTS";
        case UnaryOp::NotExists: return "NOT EXISTS";
        default: return "Unknown";
    }
}

[[nodiscard]] const char* data_type_to_string(const DataType type) noexcept {
    switch (type) {
        case DataType::Unknown: return "Unknown";
        case DataType::Boolean: return "Boolean";
        case DataType::TinyInt: return "TinyInt";
        case DataType::SmallInt: return "SmallInt";
        case DataType::Integer: return "Integer";
        case DataType::BigInt: return "BigInt";
        case DataType::Decimal: return "Decimal";
        case DataType::Real: return "Real";
        case DataType::Double: return "Double";
        case DataType::Char: return "Char";
        case DataType::VarChar: return "VarChar";
        case DataType::Text: return "Text";
        case DataType::Date: return "Date";
        case DataType::Time: return "Time";
        case DataType::Timestamp: return "Timestamp";
        case DataType::Interval: return "Interval";
        case DataType::Blob: return "Blob";
        case DataType::Array: return "Array";
        case DataType::Struct: return "Struct";
        case DataType::Map: return "Map";
        case DataType::Null: return "Null";
        case DataType::Any: return "Any";
        default: return "Unknown";
    }
}

} // namespace db25::ast