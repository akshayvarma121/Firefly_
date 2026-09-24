#pragma once

#include "firefly/presolve.h"
#include <vector>
#include <string>

namespace firefly {

enum class SolveStatus {
    OPTIMAL,
    INFEASIBLE,
    UNBOUNDED,
    FEASIBLE,
    TIME_LIMIT,
    NODE_LIMIT,
    ITERATION_LIMIT,
    ERROR
};

struct SolveResult {
    SolveStatus status;
    std::string solver_used;
    double objective_value = 0.0;
    
    // The primal solution mapped back to the ORIGINAL problem variables
    std::vector<double> primal_solution;
    
    // The dual solution mapped back to the ORIGINAL problem rows
    std::vector<double> dual_solution;
    
    // Primal-Dual gap achieved (used primarily by PDLP/interior point solvers)
    double primal_dual_gap = 0.0;
    
    // Statistics
    size_t phase1_iterations = 0;
    size_t phase2_iterations = 0;
    long long solve_time_ms = 0;
    
    // Detailed error message if status == ERROR
    std::string message;
};

struct SimplexOptions {
    double tolerance = 1e-6;
    size_t max_iterations = 1000000;
};

class SimplexSolver {
public:
    // Main entry point: Takes a PresolvedProblem, solves it, 
    // runs post-solve mappings, and self-checks constraints.
    static SolveResult solve(const PresolvedProblem& pre, const SimplexOptions& options = SimplexOptions());
};

} // namespace firefly
