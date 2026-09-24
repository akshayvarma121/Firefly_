#include "firefly/pdlp.h"
#include "firefly/mps_parser.h"
#include <iostream>
#include <cmath>

#undef NDEBUG
#include "test_utils.h"
#ifdef _MSC_VER
#include <crtdbg.h>
#endif


using namespace firefly;

namespace firefly {
    namespace pdlp_cpu {
        SolveResult solve(const PresolvedProblem& pre, const PDLPOptions& options);
    }
}

void test_netlib(const std::string& filename, int max_allowed_iterations, bool assert_convergence) {
    std::cout << "Testing PDLP on " << filename << "...\n";
    auto prob = MPSParser::parse(filename);
    auto pre = Presolver::presolve(prob);
    
    PDLPOptions opts;
    opts.max_iterations = max_allowed_iterations;
    opts.tolerance = 1e-4;
    opts.callback_frequency = 1000;
    
    double final_gap = -1.0;
    opts.iteration_callback = [&](size_t iter, double p_obj, double d_obj, double time_ms) {
        final_gap = std::abs(p_obj - d_obj);
        if (iter >= 45000) {
            std::cout << "  Iter " << iter << ": Gap=" << final_gap << "\n";
        }
    };
    
    auto result = PDLPSolver::solve(pre, opts);
    
    std::cout << "  Status: " << static_cast<int>(result.status) << "\n";
    std::cout << "  Iterations: " << result.phase1_iterations << " (Max: " << max_allowed_iterations << ")\n";
    std::cout << "  Objective: " << result.objective_value << "\n";
    std::cout << "  Final Gap: " << final_gap << "\n";
    std::cout << "  FINISHED_TEST: " << filename << "\n";

    if (assert_convergence) {
        FIREFLY_TEST_ASSERT(result.status == SolveStatus::OPTIMAL);
    }
}

#include <filesystem>

int main(int argc, char* argv[]) {
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::string test_name = "";
    if (argc >= 3 && std::string(argv[1]) == "--test") {
        test_name = argv[2];
    }

    std::vector<std::string> converging_instances = {
        "E:/firefly/core/tests/netlib_miplib/afiro.mps",
        "E:/firefly/core/tests/netlib_miplib/flugpl.mps",
        "E:/firefly/core/tests/netlib_miplib/egout.mps"
    };

    std::vector<std::string> hard_instances = {
        "E:/firefly/core/tests/netlib_miplib/p0548.mps",
        "E:/firefly/core/tests/netlib_miplib/adlittle.mps",
        "E:/firefly/core/tests/netlib_miplib/israel.mps",
        "E:/firefly/core/tests/netlib_miplib/greenbea.mps"
    };

    try {
        if (test_name == "convergence_check") {
            for (const auto& path : converging_instances) {
                test_netlib(path, 50000, true);
            }
            std::cout << "--- Hard instances (informational only) ---\n";
            for (const auto& path : hard_instances) {
                test_netlib(path, 50000, false);
            }
            std::cout << "All PDLP explicit convergence tests finished!\n";
        } else if (test_name == "cpu_gpu_parity") {
            for (const auto& path : converging_instances) {
                std::cout << "Testing CPU-GPU parity on " << path << "...\n";
                auto prob = MPSParser::parse(path);
                auto pre = Presolver::presolve(prob);
                PDLPOptions opts;
                opts.max_iterations = 50000;
                opts.tolerance = 1e-4;
                
                auto res_gpu = PDLPSolver::solve(pre, opts); // Defaults to GPU if available
                
                auto res_cpu = firefly::pdlp_cpu::solve(pre, opts);
                
                double diff = std::abs(res_gpu.objective_value - res_cpu.objective_value);
                FIREFLY_TEST_ASSERT(diff < 1e-3);
                std::cout << "[EVIDENCE] Parity check on " << path << ": GPU objective: " << res_gpu.objective_value << ", CPU objective: " << res_cpu.objective_value << ", match within tolerance 1e-3\n";
            }
        } else {
            throw std::runtime_error("Unknown test: " + test_name + ". Valid tests: convergence_check, cpu_gpu_parity, all");
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
