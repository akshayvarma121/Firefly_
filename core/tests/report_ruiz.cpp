#include "firefly/mps_parser.h"
#include "firefly/presolve.h"
#include "firefly/pdlp.h"
#include <iostream>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include <string>
#include <vector>
#include <iomanip>

using namespace firefly;

int main() {
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::vector<std::string> cases = {
        "E:/firefly/core/tests/netlib_miplib/afiro.mps",
        "E:/firefly/core/tests/netlib_miplib/adlittle.mps",
        "E:/firefly/core/tests/netlib_miplib/egout.mps",
        "E:/firefly/core/tests/netlib_miplib/flugpl.mps",
        "E:/firefly/core/tests/netlib_miplib/p0548.mps"
    };

    std::cout << std::left << std::setw(30) << "Instance" 
              << std::setw(15) << "Iterations" 
              << std::setw(20) << "Primal-Dual Gap" 
              << "Status\n";
    std::cout << std::string(80, '-') << "\n";

    for (const auto& filename : cases) {
        try {
            auto prob = MPSParser::parse(filename);
            auto pre = Presolver::presolve(prob);
            
            PDLPOptions pdlp_opts;
            pdlp_opts.max_iterations = 20000;
            pdlp_opts.tolerance = 1e-4;
            
            auto pdlp_res = PDLPSolver::solve(pre, pdlp_opts);
            
            std::string short_name = filename.substr(filename.find_last_of('/') + 1);
            
            std::cout << std::left << std::setw(30) << short_name 
                      << std::setw(15) << pdlp_res.phase1_iterations 
                      << std::setw(20) << pdlp_res.primal_dual_gap 
                      << (pdlp_res.status == SolveStatus::OPTIMAL ? "OPTIMAL" : (pdlp_res.status == SolveStatus::ITERATION_LIMIT ? "ITER_LIMIT" : "FAILED")) << "\n";
            
        } catch (const std::exception& e) {
            std::cerr << "Exception on " << filename << ": " << e.what() << "\n";
        }
    }

    return 0;
}
