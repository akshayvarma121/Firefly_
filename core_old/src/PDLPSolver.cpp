#include "PDLPSolver.hpp"

namespace firefly {

PDLPResult PDLPSolver::solve(const SparseProblem& prob, const PDLPSolverParams& params, PDLPCallback cb) {
#ifdef FIREFLY_USE_CUDA
    if (!params.force_cpu) {
        return internal::pdlp_cuda_solve(prob, params, cb);
    }
#endif
    return internal::pdlp_cpu_solve(prob, params, cb);
}

} // namespace firefly
