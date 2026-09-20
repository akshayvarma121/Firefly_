#include "firefly/mps_parser.h"
#include "firefly/presolve.h"
#include "firefly/simplex.h"
#include <iostream>
#include <vector>
#include <string>

using namespace firefly;

#include <chrono>

int main() {
    std::vector<std::string> files = {
        "afiro.mps", "adlittle.mps", "israel.mps", "greenbea.mps", "woodinfe.mps",
        "p0548.mps", "flugpl.mps", "egout.mps"
    };
    
    std::cout << "--- Real World MPS Parsing Test (Structural Check) ---\n";
    for (const auto& f : files) {
        std::string path = "E:/firefly/core/tests/netlib_miplib/" + f;
        std::cout << "File: " << f << "\n";
        try {
            auto start = std::chrono::high_resolution_clock::now();
            auto orig_prob = MPSParser::parse(path);
            auto end = std::chrono::high_resolution_clock::now();
            auto parse_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            
            // Solve Without Presolve
            PresolveOptions no_pre;
            no_pre.enable_empty_row_removal = false;
            no_pre.enable_empty_col_removal = false;
            no_pre.enable_singleton_row_tightening = false;
            no_pre.enable_fixed_variable_substitution = false;
            no_pre.enable_scaling = false;
            auto unpresolved = Presolver::presolve(orig_prob, no_pre);
            
            SimplexOptions sopts;
            sopts.max_iterations = 200000;
            
            auto res_nopre = SimplexSolver::solve(unpresolved, sopts);
            
            std::cout << "  [Without Presolve]\n"
                      << "    Parsed in: " << parse_time << "ms\n"
                      << "    Size:      " << orig_prob.row_names.size() << " rows, " << orig_prob.col_names.size() << " cols\n"
                      << "    Solve:     Status " << static_cast<int>(res_nopre.status) 
                      << " | Obj: " << res_nopre.objective_value
                      << " | Iters: " << res_nopre.phase1_iterations << " (P1) + " << res_nopre.phase2_iterations << " (P2)"
                      << " | Time: " << res_nopre.solve_time_ms << "ms\n";
            if (res_nopre.status == SolveStatus::ERROR) std::cout << "    Error msg: " << res_nopre.message << "\n";
                      
            start = std::chrono::high_resolution_clock::now();
            auto presolved = Presolver::presolve(orig_prob);
            end = std::chrono::high_resolution_clock::now();
            auto presolve_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            
            auto res_pre = SimplexSolver::solve(presolved, sopts);
            
            std::cout << "  [With Presolve]\n"
                      << "    Presolved in: " << presolve_time << "ms\n"
                      << "    Stats:        " << presolved.stats.rows_removed << " rows rem, "
                      << presolved.stats.variables_fixed << " vars fix, "
                      << presolved.stats.bounds_tightened << " bnds tight, "
                      << presolved.stats.scaling_applied << " scales\n"
                      << "    Size:         " << presolved.stats.reduced_rows << " rows, " 
                      << presolved.stats.reduced_cols << " cols\n"
                      << "    Solve:     Status " << static_cast<int>(res_pre.status)
                      << " | Obj: " << res_pre.objective_value
                      << " | Iters: " << res_pre.phase1_iterations << " (P1) + " << res_pre.phase2_iterations << " (P2)"
                      << " | Time: " << res_pre.solve_time_ms << "ms\n";
            if (res_pre.status == SolveStatus::ERROR) std::cout << "    Error msg: " << res_pre.message << "\n";
            std::cout << "\n";
        } catch (const std::exception& e) {
            std::cout << "  FAILED\n  Error: " << e.what() << "\n\n";
        }
    }
    return 0;
}
