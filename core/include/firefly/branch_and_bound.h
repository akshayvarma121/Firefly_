#pragma once

#include "firefly/mps_parser.h"
#include "firefly/simplex.h" // For SolveResult and SolveStatus
#include <functional>
#include <vector>
#include <memory>

namespace firefly {

enum class NodeOrdering {
    BEST_FIRST,
    DEPTH_FIRST
};

enum class LPSolverChoice {
    PDLP,
    SIMPLEX
};

struct BBSolverOptions {
    NodeOrdering ordering = NodeOrdering::BEST_FIRST;
    LPSolverChoice lp_solver = LPSolverChoice::PDLP;
    
    // Limits
    long long time_limit_ms = 60000; // Default 60s
    size_t node_limit = 100000;
    
    // Tolerances
    double integrality_tol = 1e-6;
    double obj_tol = 1e-6;
    
    // Callback
    size_t callback_frequency = 100; // Invoke every K nodes
    
    // Signature: node_count, best_incumbent, best_bound, elapsed_ms
    std::function<void(size_t, double, double, double)> progress_callback = nullptr;
};

struct BBSolverResult : public SolveResult {
    size_t node_count = 0;
    double best_bound = 0.0;
    
    // Performance measurements
    double avg_presolve_ms_per_node = 0.0;
    double avg_lp_solve_ms_per_node = 0.0;
};

class BranchAndBoundSolver {
public:
    static BBSolverResult solve(const Problem& prob, const BBSolverOptions& options = BBSolverOptions());
};

} // namespace firefly
