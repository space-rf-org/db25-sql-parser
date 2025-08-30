#include <benchmark/benchmark.h>
#include "db25/memory/arena.hpp"
#include "db25/ast/ast_node.hpp"

using namespace db25;
using namespace db25::ast;

static void BM_ArenaAllocation(benchmark::State& state) {
    const size_t allocation_size = state.range(0);
    
    for (auto _ : state) {
        Arena arena;
        for (int i = 0; i < 1000; ++i) {
            [[maybe_unused]] void* ptr = arena.allocate(allocation_size);
        }
    }
    
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_ArenaAllocation)->Arg(8)->Arg(16)->Arg(32)->Arg(64)->Arg(128);

static void BM_ASTNodeConstruction(benchmark::State& state) {
    Arena arena;
    
    for (auto _ : state) {
        auto* node = arena.allocate_object<ASTNode>(NodeType::SelectStmt);
        benchmark::DoNotOptimize(node);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ASTNodeConstruction);

static void BM_ASTTreeConstruction(benchmark::State& state) {
    const int tree_depth = state.range(0);
    
    for (auto _ : state) {
        Arena arena;
        auto* root = arena.allocate_object<ASTNode>(NodeType::SelectStmt);
        
        std::vector<ASTNode*> current_level = {root};
        
        for (int depth = 1; depth < tree_depth; ++depth) {
            std::vector<ASTNode*> next_level;
            
            for (auto* parent : current_level) {
                for (int i = 0; i < 3; ++i) {
                    auto* child = arena.allocate_object<ASTNode>(NodeType::BinaryExpr);
                    parent->add_child(child);
                    next_level.push_back(child);
                }
            }
            
            current_level = std::move(next_level);
        }
        
        benchmark::DoNotOptimize(root);
    }
}
BENCHMARK(BM_ASTTreeConstruction)->Arg(3)->Arg(5)->Arg(7);

static void BM_ASTTraversal(benchmark::State& state) {
    Arena arena;
    
    auto* root = arena.allocate_object<ASTNode>(NodeType::SelectStmt);
    std::vector<ASTNode*> all_nodes = {root};
    
    for (int i = 0; i < 100; ++i) {
        auto* child = arena.allocate_object<ASTNode>(NodeType::BinaryExpr);
        root->add_child(child);
        all_nodes.push_back(child);
        
        for (int j = 0; j < 10; ++j) {
            auto* grandchild = arena.allocate_object<ASTNode>(NodeType::IntegerLiteral);
            child->add_child(grandchild);
            all_nodes.push_back(grandchild);
        }
    }
    
    int node_count = 0;
    
    for (auto _ : state) {
        std::function<void(const ASTNode*)> traverse = [&](const ASTNode* node) {
            ++node_count;
            auto children = node->get_children();
            for (auto* child : children) {
                traverse(child);
            }
        };
        
        traverse(root);
        benchmark::DoNotOptimize(node_count);
    }
    
    state.SetItemsProcessed(state.iterations() * all_nodes.size());
}
BENCHMARK(BM_ASTTraversal);

BENCHMARK_MAIN();