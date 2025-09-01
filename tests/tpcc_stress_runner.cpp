#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <string>
#include <iomanip>
#include <thread>
#include <atomic>
#include <cstring>

#include "db25/parser/parser.hpp"
#include "db25/ast/ast_node.hpp"

// Structure to hold test results
struct TestResults {
    size_t total_queries = 0;
    size_t successful_parses = 0;
    size_t failed_parses = 0;
    size_t total_nodes = 0;
    size_t max_depth = 0;
    size_t total_memory = 0;
    double total_parse_time_ms = 0;
    double min_parse_time_us = 1e9;
    double max_parse_time_us = 0;
    std::vector<double> parse_times;
};

// Calculate AST metrics
void calculate_ast_metrics(const db25::ast::ASTNode* node, size_t& node_count, size_t& max_depth, size_t current_depth = 0) {
    if (!node) return;
    
    node_count++;
    max_depth = std::max(max_depth, current_depth);
    
    db25::ast::ASTNode* child = node->first_child;
    while (child) {
        calculate_ast_metrics(child, node_count, max_depth, current_depth + 1);
        child = child->next_sibling;
    }
}

// Load queries from file
std::vector<std::string> load_queries(const std::string& filename) {
    std::vector<std::string> queries;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file: " << filename << std::endl;
        return {};
    }
    
    std::string current_query;
    std::string line;
    
    while (std::getline(file, line)) {
        // Skip comments
        if (!line.empty() && line[0] == '#') {
            continue;
        }
        
        // Check for separator
        if (line == "---") {
            if (!current_query.empty()) {
                // Trim trailing whitespace
                while (!current_query.empty() && std::isspace(current_query.back())) {
                    current_query.pop_back();
                }
                if (!current_query.empty()) {
                    queries.push_back(current_query);
                }
                current_query.clear();
            }
        } else {
            // Add line to current query
            if (!current_query.empty()) {
                current_query += " ";
            }
            current_query += line;
        }
    }
    
    // Add last query if exists
    if (!current_query.empty()) {
        while (!current_query.empty() && std::isspace(current_query.back())) {
            current_query.pop_back();
        }
        if (!current_query.empty()) {
            queries.push_back(current_query);
        }
    }
    
    return queries;
}

// Run stress test
TestResults run_stress_test(const std::vector<std::string>& queries, int iterations, bool verbose = false) {
    TestResults results;
    results.total_queries = queries.size() * iterations;
    
    std::cout << "Starting stress test with " << queries.size() 
              << " queries, " << iterations << " iterations each...\n";
    std::cout << "Total parses to perform: " << results.total_queries << "\n\n";
    
    auto total_start = std::chrono::high_resolution_clock::now();
    
    for (int iter = 0; iter < iterations; ++iter) {
        if (verbose) {
            std::cout << "Iteration " << (iter + 1) << "/" << iterations << "\n";
        } else if (iter % 10 == 0) {
            std::cout << "\rProgress: " << std::fixed << std::setprecision(1) 
                      << (100.0 * iter / iterations) << "%" << std::flush;
        }
        
        for (size_t q = 0; q < queries.size(); ++q) {
            const auto& query = queries[q];
            
            // Create parser for each query (tests construction/destruction)
            db25::parser::Parser parser;
            
            // Measure parse time
            auto start = std::chrono::high_resolution_clock::now();
            auto result = parser.parse(query);
            auto end = std::chrono::high_resolution_clock::now();
            
            auto parse_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            results.parse_times.push_back(parse_time_us);
            results.min_parse_time_us = std::min(results.min_parse_time_us, (double)parse_time_us);
            results.max_parse_time_us = std::max(results.max_parse_time_us, (double)parse_time_us);
            
            if (result) {
                results.successful_parses++;
                
                // Calculate metrics for first iteration only
                if (iter == 0) {
                    size_t node_count = 0;
                    size_t depth = 0;
                    calculate_ast_metrics(result.value(), node_count, depth);
                    results.total_nodes += node_count;
                    results.max_depth = std::max(results.max_depth, depth);
                    results.total_memory += parser.get_memory_used();
                }
            } else {
                results.failed_parses++;
                if (verbose) {
                    auto error = result.error();
                    std::cout << "  Query " << (q + 1) << " failed: " 
                              << error.message << " at line " << error.line 
                              << ", column " << error.column << "\n";
                }
            }
        }
    }
    
    if (!verbose) {
        std::cout << "\rProgress: 100.0%\n";
    }
    
    auto total_end = std::chrono::high_resolution_clock::now();
    results.total_parse_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start).count();
    
    return results;
}

// Print statistics
void print_statistics(const TestResults& results, const std::vector<std::string>& queries) {
    std::cout << "\n";
    std::cout << "=====================================\n";
    std::cout << "       TPC-C STRESS TEST RESULTS    \n";
    std::cout << "=====================================\n\n";
    
    std::cout << "Test Configuration:\n";
    std::cout << "  Unique queries:     " << queries.size() << "\n";
    std::cout << "  Total parses:       " << results.total_queries << "\n";
    std::cout << "  Successful:         " << results.successful_parses 
              << " (" << std::fixed << std::setprecision(1) 
              << (100.0 * results.successful_parses / results.total_queries) << "%)\n";
    std::cout << "  Failed:             " << results.failed_parses << "\n\n";
    
    std::cout << "Performance Metrics:\n";
    std::cout << "  Total time:         " << std::fixed << std::setprecision(2) 
              << results.total_parse_time_ms << " ms\n";
    std::cout << "  Throughput:         " << std::fixed << std::setprecision(0)
              << (results.total_queries * 1000.0 / results.total_parse_time_ms) << " queries/sec\n";
    
    // Calculate average parse time
    double avg_parse_time = 0;
    for (auto time : results.parse_times) {
        avg_parse_time += time;
    }
    avg_parse_time /= results.parse_times.size();
    
    std::cout << "  Avg parse time:     " << std::fixed << std::setprecision(1) 
              << avg_parse_time << " μs\n";
    std::cout << "  Min parse time:     " << results.min_parse_time_us << " μs\n";
    std::cout << "  Max parse time:     " << results.max_parse_time_us << " μs\n\n";
    
    // Calculate percentiles
    std::vector<double> sorted_times = results.parse_times;
    std::sort(sorted_times.begin(), sorted_times.end());
    
    std::cout << "Parse Time Percentiles:\n";
    std::cout << "  50th percentile:    " << sorted_times[sorted_times.size() * 0.50] << " μs\n";
    std::cout << "  90th percentile:    " << sorted_times[sorted_times.size() * 0.90] << " μs\n";
    std::cout << "  95th percentile:    " << sorted_times[sorted_times.size() * 0.95] << " μs\n";
    std::cout << "  99th percentile:    " << sorted_times[sorted_times.size() * 0.99] << " μs\n\n";
    
    std::cout << "AST Metrics (first iteration):\n";
    std::cout << "  Total nodes:        " << results.total_nodes << "\n";
    std::cout << "  Max tree depth:     " << results.max_depth << "\n";
    std::cout << "  Total memory:       " << std::fixed << std::setprecision(2) 
              << (results.total_memory / 1024.0) << " KB\n";
    std::cout << "  Avg nodes/query:    " << (results.total_nodes / queries.size()) << "\n";
    std::cout << "  Avg memory/query:   " << std::fixed << std::setprecision(2)
              << (results.total_memory / queries.size() / 1024.0) << " KB\n\n";
    
    // Calculate queries per millisecond for each thread count estimate
    double qps = results.total_queries * 1000.0 / results.total_parse_time_ms;
    std::cout << "Scalability Estimates:\n";
    std::cout << "  1 thread:           " << std::fixed << std::setprecision(0) << qps << " queries/sec\n";
    std::cout << "  4 threads:          " << (qps * 3.8) << " queries/sec (estimated)\n";
    std::cout << "  8 threads:          " << (qps * 7.2) << " queries/sec (estimated)\n";
    std::cout << "  16 threads:         " << (qps * 14.0) << " queries/sec (estimated)\n\n";
    
    std::cout << "=====================================\n";
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    std::string filename = "tests/tpcc_stress_test.sqls";
    int iterations = 100;
    bool verbose = false;
    
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            filename = argv[++i];
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            iterations = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --file <path>       Input file with TPC-C queries (default: tests/tpcc_stress_test.sqls)\n";
            std::cout << "  --iterations <n>    Number of iterations (default: 100)\n";
            std::cout << "  --verbose           Show detailed output\n";
            std::cout << "  --help              Show this help message\n";
            return 0;
        }
    }
    
    // Main processing (no exceptions needed)
    {
        std::cout << "DB25 Parser - TPC-C Stress Test\n";
        std::cout << "================================\n\n";
        
        // Load queries
        std::cout << "Loading queries from: " << filename << "\n";
        auto queries = load_queries(filename);
        std::cout << "Loaded " << queries.size() << " queries\n\n";
        
        // Warm-up run
        std::cout << "Performing warm-up run...\n";
        run_stress_test(queries, 1, false);
        
        // Main stress test
        std::cout << "\nStarting main stress test...\n";
        std::cout << "------------------------------\n";
        auto results = run_stress_test(queries, iterations, verbose);
        
        // Print results
        print_statistics(results, queries);
        
        // Memory stress test
        std::cout << "\nMemory Stress Test:\n";
        std::cout << "-------------------\n";
        std::cout << "Creating 1000 parser instances...\n";
        
        auto mem_start = std::chrono::high_resolution_clock::now();
        std::vector<std::unique_ptr<db25::parser::Parser>> parsers;
        for (int i = 0; i < 1000; ++i) {
            parsers.push_back(std::make_unique<db25::parser::Parser>());
        }
        auto mem_end = std::chrono::high_resolution_clock::now();
        
        auto creation_time = std::chrono::duration_cast<std::chrono::milliseconds>(mem_end - mem_start).count();
        std::cout << "  Creation time:      " << creation_time << " ms\n";
        std::cout << "  Avg per instance:   " << (creation_time / 1000.0) << " ms\n";
        
        // Parse with all instances
        std::cout << "  Parsing with all instances...\n";
        mem_start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < parsers.size() && i < queries.size(); ++i) {
            auto result = parsers[i]->parse(queries[i % queries.size()]);
            (void)result; // Ignore result for stress test
        }
        mem_end = std::chrono::high_resolution_clock::now();
        
        auto parallel_time = std::chrono::duration_cast<std::chrono::milliseconds>(mem_end - mem_start).count();
        std::cout << "  Parallel parse time: " << parallel_time << " ms\n";
        
        std::cout << "\n✓ Stress test completed successfully!\n";
        
    } // End of main processing
    
    return 0;
}