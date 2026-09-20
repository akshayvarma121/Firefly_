#include "firefly/pdlp.h"
#include <iostream>
#include <cmath>
#undef NDEBUG
#include <cassert>

using namespace firefly;

void test_ruiz_equilibration() {
    Problem prob;
    prob.name = "badly_scaled";
    prob.minimize = true;
    
    // Variables
    prob.col_names = {"x0", "x1"};
    prob.col_lower_bounds = {0.0, 0.0};
    prob.col_upper_bounds = {INF, INF};
    prob.is_integer = {false, false};
    prob.objective = {1000.0, 0.001};
    
    // Constraints
    prob.row_names = {"r0"};
    prob.row_lower_bounds = {100.0};
    prob.row_upper_bounds = {INF};
    prob.row_senses = {'G'};
    
    // Matrix
    prob.matrix.push_back({0, 0, 1e6});
    prob.matrix.push_back({0, 1, 1e-6});
    
    PresolveOptions p_opts;
    p_opts.enable_scaling = false; // Disable presolve scaling to test PDLP Ruiz
    
    auto pre = Presolver::presolve(prob, p_opts);
    
    PDLPOptions opts;
    opts.tolerance = 1e-6;
    opts.max_iterations = 50000;
    
    auto result = PDLPSolver::solve(pre, opts);
    
    std::cout << "Result status: " << static_cast<int>(result.status) << "\n";
    std::cout << "Primal Solution: x0 = " << result.primal_solution[0] << ", x1 = " << result.primal_solution[1] << "\n";
    if (result.dual_solution.size() > 0) {
        std::cout << "Dual Solution: y0 = " << result.dual_solution[0] << "\n";
    }
    
    assert(result.status == SolveStatus::OPTIMAL);
    
    // Check primal values
    assert(std::abs(result.primal_solution[0] - 1e-4) < 1e-5);
    assert(std::abs(result.primal_solution[1] - 0.0) < 1e-5);
    
    // Check dual values (should be ~1e-3)
    assert(result.dual_solution.size() > 0);
    assert(std::abs(result.dual_solution[0] - 1e-3) < 1e-4);
}

int main() {
    try {
        std::cout << "Testing Ruiz equilibration...\n";
        test_ruiz_equilibration();
        std::cout << "All Ruiz tests passed!\n";
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
