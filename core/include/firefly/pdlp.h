#pragma once

#include "firefly/presolve.h"
#include "firefly/simplex.h" // For SolveResult and SolveStatus
#include <functional>
#include <vector>

namespace firefly {

struct PDLPOptions {
    double tolerance = 1e-6;
    size_t max_iterations = 100000;
    
    // Adaptive step size and restart options per Applegate et al.
    bool adaptive_step_size = true;
    bool enable_restarts = true;
    
    // Output frequency for the callback
    size_t callback_frequency = 100;
    
    // Callback signature: iteration, primal_obj, dual_obj, elapsed_ms
    std::function<void(size_t, double, double, double)> iteration_callback = nullptr;
    
    // Estimate of the operator norm for initial step size
    double l_norm_estimate = -1.0;
};

class PDLPSolver {
public:
    static SolveResult solve(const PresolvedProblem& pre, const PDLPOptions& options = PDLPOptions());
};

} // namespace firefly
