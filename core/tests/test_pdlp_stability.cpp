#include "firefly/mps_parser.h"
#include "firefly/presolve.h"
#include "firefly/pdlp.h"
#include <iostream>

#undef NDEBUG
#include <cassert>

using namespace firefly;

int main() {
    try {
        auto prob = MPSParser::parse("E:/firefly/core/tests/regression/ill_conditioned.mps");
        auto pre = Presolver::presolve(prob);
        
        PDLPOptions pdlp_opts;
        pdlp_opts.max_iterations = 10000;
        
        auto pdlp_res = PDLPSolver::solve(pre, pdlp_opts);
        
        std::cout << "Status: " << static_cast<int>(pdlp_res.status) << "\n";
        std::cout << "Objective: " << pdlp_res.objective_value << "\n";
        
        // Assert it reached iteration limit and exited cleanly without crashing
        assert(pdlp_res.status == SolveStatus::OPTIMAL || (pdlp_res.status == SolveStatus::ERROR && pdlp_res.message.find("Iteration limit") != std::string::npos));
        
        // Assert no NaN or corrupted values
        assert(std::isfinite(pdlp_res.objective_value));
        
        std::cout << "Stability test passed on ill-conditioned file.\n";
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
