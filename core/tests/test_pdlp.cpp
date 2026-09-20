#include "firefly/pdlp.h"
#include "firefly/mps_parser.h"
#include <iostream>
#include <cmath>

#undef NDEBUG
#include <cassert>

using namespace firefly;

void test_netlib(const std::string& filename, double expected_obj, double tolerance = 1e-1) {
    std::cout << "Testing PDLP on " << filename << "...\n";
    auto prob = MPSParser::parse(filename);
    auto pre = Presolver::presolve(prob);
    
    PDLPOptions opts;
    opts.max_iterations = 20000;
    opts.tolerance = 1e-5;
    
    auto result = PDLPSolver::solve(pre, opts);
    
    std::cout << "  Status: " << static_cast<int>(result.status) << "\n";
    std::cout << "  Iterations: " << result.phase1_iterations << "\n";
    std::cout << "  Objective: " << result.objective_value << " (Expected: " << expected_obj << ")\n";
    
    assert(result.status == SolveStatus::OPTIMAL);
    assert(std::abs(result.objective_value - expected_obj) / std::abs(expected_obj) < tolerance || 
           std::abs(result.objective_value - expected_obj) < tolerance);
}

int main() {
    try {
        test_netlib("E:/firefly/core/tests/netlib_miplib/afiro.mps", -464.75314286);
        // test_netlib("E:/firefly/core/tests/netlib_miplib/adlittle.mps", 225494.96316);
        
        std::cout << "All PDLP tests passed!\n";
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
