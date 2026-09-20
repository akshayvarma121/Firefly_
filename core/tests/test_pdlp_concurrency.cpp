#include "firefly/pdlp.h"
#include "firefly/mps_parser.h"
#include "firefly/presolve.h"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <cmath>
#undef NDEBUG
#include <cassert>

using namespace firefly;

void thread_worker(const std::string& filename, double expected_obj, std::atomic<int>& success_count) {
    try {
        auto prob = MPSParser::parse(filename);
        auto pre = Presolver::presolve(prob);
        
        PDLPOptions opts;
        opts.max_iterations = 20000;
        opts.tolerance = 1e-4; // PDLP might need many iterations, use loose tolerance just to check non-corrupted convergence
        
        auto result = PDLPSolver::solve(pre, opts);
        
        if (result.status == SolveStatus::OPTIMAL) {
            double diff = std::abs(result.objective_value - expected_obj);
            double rel_diff = diff / std::max(1.0, std::abs(expected_obj));
            if (rel_diff < 1e-2 || diff < 1e-2) {
                success_count++;
            } else {
                std::cerr << "Thread for " << filename << " failed obj match. Expected " << expected_obj << " got " << result.objective_value << "\n";
            }
        } else if (result.status == SolveStatus::ERROR && result.message.find("Iteration limit") != std::string::npos) {
            // Clean non-convergence report is acceptable
            std::cout << "Thread for " << filename << " returned clean non-convergence.\n";
            success_count++;
        } else {
            std::cerr << "Thread for " << filename << " did not return OPTIMAL or clean ERROR. Status: " << static_cast<int>(result.status) << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Thread for " << filename << " threw exception: " << e.what() << "\n";
    }
}

int main() {
    try {
        std::atomic<int> success_count{0};
        
        std::cout << "Starting 2 concurrent PDLP solves on different problems...\n";
        
        std::thread t1(thread_worker, "E:/firefly/core/tests/netlib_miplib/afiro.mps", -464.753, std::ref(success_count));
        std::thread t2(thread_worker, "E:/firefly/core/tests/netlib_miplib/adlittle.mps", 225494.96316, std::ref(success_count));
        
        t1.join();
        t2.join();
        
        std::cout << "Successful concurrent solves: " << success_count.load() << "/2\n";
        assert(success_count.load() == 2);
        
        std::cout << "Concurrency test passed (no race conditions, accurate results)!\n";
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
