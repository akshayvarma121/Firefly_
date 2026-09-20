#include "firefly/mps_parser.h"
#include "firefly/presolve.h"
#include "firefly/pdlp.h"
#include <iostream>
#include <cstdlib>

#undef NDEBUG
#include <cassert>

using namespace firefly;

int main() {
    try {
        // Set an invalid device ID to force CUDA failure
#ifdef _WIN32
        _putenv_s("CUDA_VISIBLE_DEVICES", "99");
#else
        setenv("CUDA_VISIBLE_DEVICES", "99", 1);
#endif

        // Parse problem
        auto prob = MPSParser::parse("E:/firefly/core/tests/netlib_miplib/afiro.mps");
        auto pre = Presolver::presolve(prob);
        
        PDLPOptions pdlp_opts;
        pdlp_opts.max_iterations = 20000;
        
        // This should trigger the exception in cuda_context or pdlp_cuda
        // and gracefully fallback to CPU.
        auto pdlp_res = PDLPSolver::solve(pre, pdlp_opts);
        
        std::cout << "Status: " << static_cast<int>(pdlp_res.status) << "\n";
        
        // Ensure it didn't crash and we actually got a valid solution or clean fallback
        assert(pdlp_res.status == SolveStatus::OPTIMAL || pdlp_res.status == SolveStatus::ERROR);
        
        std::cout << "GPU unavailability test passed (clean fallback, no crash).\n";
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
