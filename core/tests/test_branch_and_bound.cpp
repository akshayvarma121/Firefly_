#include "firefly/mps_parser.h"
#include "firefly/branch_and_bound.h"
#include <iostream>
#include <vector>
#include <string>
#include <cassert>

using namespace firefly;

void test_simple_mip() {
    std::cout << "Running Simple MIP Test (Branching Correctness)\n";
    // Maximize 5x + 8y
    // s.t. x + y <= 6
    //      5x + 9y <= 45
    //      x, y >= 0, integer
    // Equivalent to Minimize -5x - 8y
    Problem p;
    p.minimize = true;
    p.col_names = {"x", "y"};
    p.col_to_index = {{"x", 0}, {"y", 1}};
    p.col_lower_bounds = {0.0, 0.0};
    p.col_upper_bounds = {INF, INF};
    p.is_integer = {true, true};
    p.objective = {-5.0, -8.0};
    
    p.row_names = {"c1", "c2"};
    p.row_to_index = {{"c1", 0}, {"c2", 1}};
    p.row_senses = {'L', 'L'};
    p.row_lower_bounds = {-INF, -INF};
    p.row_upper_bounds = {6.0, 45.0};
    
    p.matrix = {
        {0, 0, 1.0}, {0, 1, 1.0},
        {1, 0, 5.0}, {1, 1, 9.0}
    };
    
    BBSolverOptions opts;
    opts.lp_solver = LPSolverChoice::SIMPLEX; // Use simplex for exactness in small test
    
    auto res = BranchAndBoundSolver::solve(p, opts);
    std::cout << "Simple MIP Best Obj: " << res.objective_value << "\n";
    std::cout << "Solution: x=" << res.primal_solution[0] << " y=" << res.primal_solution[1] << "\n";
    
    assert(res.status == SolveStatus::OPTIMAL);
    assert(std::abs(res.objective_value - (-40.0)) < 1e-5); // x=0, y=5 -> -40
    std::cout << "Simple MIP Test Passed\n\n";
}

void test_pruning_infeasible() {
    std::cout << "Running Infeasible Node Pruning Test\n";
    // Minimize x
    // x >= 0.5, integer
    // x <= 0.5
    Problem p;
    p.minimize = true;
    p.col_names = {"x"};
    p.col_to_index = {{"x", 0}};
    p.col_lower_bounds = {0.5};
    p.col_upper_bounds = {0.5};
    p.is_integer = {true};
    p.objective = {1.0};
    
    p.row_names = {};
    p.row_to_index = {};
    p.row_senses = {};
    p.row_lower_bounds = {};
    p.row_upper_bounds = {};
    p.matrix = {};
    
    BBSolverOptions opts;
    opts.lp_solver = LPSolverChoice::SIMPLEX;
    
    auto res = BranchAndBoundSolver::solve(p, opts);
    std::cout << "Infeasible MIP Status: " << (int)res.status << "\n";
    assert(res.status == SolveStatus::INFEASIBLE);
    std::cout << "Infeasible Node Pruning Test Passed\n\n";
}

void stress_test_miplib() {
    std::cout << "--- Phase 5.6 large-MIPLIB stress test ---\n";
    std::vector<std::string> files = {
        "p0548.mps", "flugpl.mps", "egout.mps"
    };
    
    for (const auto& f : files) {
        std::string path = "E:/firefly/core/tests/netlib_miplib/" + f;
        std::cout << "File: " << f << "\n";
        try {
            auto prob = MPSParser::parse(path);
            
            BBSolverOptions opts;
            opts.lp_solver = LPSolverChoice::SIMPLEX;
            opts.time_limit_ms = 5000; // 5 second limit for quick stress test check
            opts.ordering = NodeOrdering::BEST_FIRST;
            
            opts.progress_callback = [](size_t n, double inc, double bnd, double ms) {
                if (n % 100 == 0) {
                    std::cout << "  Node: " << n << " | Incumbent: " << inc << " | Bound: " << bnd << " | Time: " << ms << "ms\n";
                }
            };
            
            auto res = BranchAndBoundSolver::solve(prob, opts);
            
            std::cout << "  Solve Status: " << (int)res.status << "\n";
            std::cout << "  Total Nodes: " << res.node_count << "\n";
            std::cout << "  Avg Presolve Time per Node: " << res.avg_presolve_ms_per_node << " ms\n";
            std::cout << "  Avg LP Solve Time per Node: " << res.avg_lp_solve_ms_per_node << " ms\n";
            std::cout << "  Best Incumbent: " << res.objective_value << "\n";
            
        } catch (const std::exception& e) {
            std::cout << "  FAILED: " << e.what() << "\n";
        }
        std::cout << "\n";
    }
}

int main() {
    std::cout << std::unitbuf;
    try {
        test_simple_mip();
        // test_pruning_infeasible();
        // stress_test_miplib();
    } catch (const std::exception& e) {
        std::cerr << "Fatal Exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown Fatal Exception!" << std::endl;
        return 1;
    }
    return 0;
}
