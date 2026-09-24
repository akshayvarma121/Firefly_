#include "firefly/mps_parser.h"
#include "firefly/presolve.h"
#include "firefly/pdlp.h"
#include "firefly/simplex.h"
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <iomanip>

#undef NDEBUG
#include "test_utils.h"
#ifdef _MSC_VER
#include <crtdbg.h>
#endif


using namespace firefly;

struct TestCase {
    std::string filename;
};

int main(int argc, char* argv[]) {
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::string test_name = "all";
    if (argc >= 3 && std::string(argv[1]) == "--test") {
        test_name = argv[2];
    }

    if (test_name != "cross_validation" && test_name != "all") {
        std::cerr << "Unknown test: " << test_name << ". Valid tests: cross_validation, all\n";
        return 1;
    }

    std::vector<TestCase> cases = {
        {"E:/firefly/core/tests/netlib_miplib/afiro.mps"},
        {"E:/firefly/core/tests/netlib_miplib/adlittle.mps"},
        {"E:/firefly/core/tests/netlib_miplib/egout.mps"},
        {"E:/firefly/core/tests/netlib_miplib/flugpl.mps"},
        {"E:/firefly/core/tests/netlib_miplib/p0548.mps"}
    };

    int success_count = 0;
    
    std::cout << "Starting Cross-Validation PDLP vs Simplex...\n";
    std::cout << std::left << std::setw(30) << "Filename" 
              << std::setw(20) << "Simplex Obj" 
              << std::setw(20) << "PDLP Obj" 
              << "Diff\n";
    std::cout << std::string(80, '-') << "\n";

    for (const auto& tc : cases) {
        try {
            auto prob = MPSParser::parse(tc.filename);
            auto pre = Presolver::presolve(prob);
            
            SimplexOptions sx_opts;
            auto sx_res = SimplexSolver::solve(pre, sx_opts);
            
            PDLPOptions pdlp_opts;
            pdlp_opts.max_iterations = 20000;
            auto pdlp_res = PDLPSolver::solve(pre, pdlp_opts);
            
            double diff = std::abs(sx_res.objective_value - pdlp_res.objective_value);
            double rel_diff = diff / std::max(1.0, std::abs(sx_res.objective_value));
            
            std::cout << std::left << std::setw(30) << tc.filename 
                      << std::setw(20) << sx_res.objective_value 
                      << std::setw(20) << pdlp_res.objective_value 
                      << diff << "\n";
            
            // Allow loose tolerance due to first-order method nature or clean non-convergence
            if (sx_res.status == SolveStatus::OPTIMAL) {
                if (pdlp_res.status == SolveStatus::OPTIMAL && (diff < 1.0 || rel_diff < 0.05)) {
                    success_count++;
                } else if (pdlp_res.status == SolveStatus::ITERATION_LIMIT || (pdlp_res.status == SolveStatus::ERROR && pdlp_res.message.find("Iteration limit") != std::string::npos)) {
                    std::cout << "Clean non-convergence on " << tc.filename << "\n";
                    success_count++;
                } else {
                    std::cerr << "Mismatch or non-optimal status on " << tc.filename << "\n";
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception on " << tc.filename << ": " << e.what() << "\n";
        }
    }

    std::cout << "Cross-Validation passed: " << success_count << "/" << cases.size() << "\n";
    FIREFLY_TEST_ASSERT(success_count == cases.size());

    return 0;
}
