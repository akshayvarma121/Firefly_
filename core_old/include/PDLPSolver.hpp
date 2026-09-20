#pragma once

#include "SparseProblem.hpp"
#include <vector>
#include <functional>

namespace firefly {

enum class PDLPStatus {
    OPTIMAL,
    INFEASIBLE,
    UNBOUNDED,
    MAX_ITERATIONS
};

struct PDLPResult {
    PDLPStatus status;
    double objective_value;
    std::vector<double> solution;
    int iterations;
};

struct PDLPSolverParams {
    double tolerance = 1e-4;
    int max_iterations = 10000;
    int report_frequency = 100;
    bool force_cpu = false; // Forces CPU fallback even if CUDA is available
};

using PDLPCallback = std::function<void(int iter, double primal_obj, double dual_obj)>;

class PDLPSolver {
public:
    static PDLPResult solve(const SparseProblem& prob, const PDLPSolverParams& params = {}, PDLPCallback cb = nullptr);
};

// Internal implementations
namespace internal {
    PDLPResult pdlp_cpu_solve(const SparseProblem& prob, const PDLPSolverParams& params, PDLPCallback cb);
    
#ifdef FIREFLY_USE_CUDA
    PDLPResult pdlp_cuda_solve(const SparseProblem& prob, const PDLPSolverParams& params, PDLPCallback cb);
#endif
}

} // namespace firefly
