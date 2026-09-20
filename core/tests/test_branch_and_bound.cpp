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

void test_regression_marker_int() {
    std::cout << "--- test_regression_marker_int ---\n";
    auto prob = MPSParser::parse("E:/firefly/core/tests/regression/marker_int.mps");
    
    // Verify it detected the integer variable correctly
    assert(prob.is_integer[0] == true);
    
    BBSolverOptions opts;
    opts.lp_solver = LPSolverChoice::SIMPLEX;
    auto res = BranchAndBoundSolver::solve(prob, opts);
    
    std::cout << "marker_int Best Obj: " << res.objective_value << "\n";
    assert(res.status == SolveStatus::OPTIMAL);
    assert(std::abs(res.objective_value - (-10.0)) < 1e-5);
    std::cout << "test_regression_marker_int passed.\n\n";
}

void test_miplib_flugpl() {
    std::cout << "--- test_miplib_flugpl (MIPLIB) ---\n";
    std::string path = "E:/firefly/core/tests/netlib_miplib/flugpl.mps";
    auto prob = MPSParser::parse(path);
    
    BBSolverOptions opts_best;
    opts_best.lp_solver = LPSolverChoice::SIMPLEX;
    opts_best.ordering = NodeOrdering::BEST_FIRST;
    auto res_best = BranchAndBoundSolver::solve(prob, opts_best);
    
    std::cout << "Best-First Strategy:\n";
    std::cout << "  Solve Status: " << (int)res_best.status << "\n";
    std::cout << "  Total Nodes: " << res_best.node_count << "\n";
    std::cout << "  Best Incumbent: " << res_best.objective_value << "\n\n";
    
    BBSolverOptions opts_depth;
    opts_depth.lp_solver = LPSolverChoice::SIMPLEX;
    opts_depth.ordering = NodeOrdering::DEPTH_FIRST;
    auto res_depth = BranchAndBoundSolver::solve(prob, opts_depth);
    
    std::cout << "Depth-First Strategy:\n";
    std::cout << "  Solve Status: " << (int)res_depth.status << "\n";
    std::cout << "  Total Nodes: " << res_depth.node_count << "\n";
    std::cout << "  Best Incumbent: " << res_depth.objective_value << "\n\n";
    
    // Assert optimal for both
    assert(res_best.status == SolveStatus::OPTIMAL);
    assert(res_depth.status == SolveStatus::OPTIMAL);
    
    // Published optimal for flugpl is 1201500
    double expected_obj = 1201500.0;
    assert(std::abs(res_best.objective_value - expected_obj) < 1e-1);
    assert(std::abs(res_depth.objective_value - expected_obj) < 1e-1);
    
    std::cout << "test_miplib_flugpl passed.\n\n";
}

int main() {
    std::cout << std::unitbuf;
    try {
        test_regression_marker_int();
        test_miplib_flugpl();
    } catch (const std::exception& e) {
        std::cerr << "Fatal Exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown Fatal Exception!" << std::endl;
        return 1;
    }
    return 0;
}
