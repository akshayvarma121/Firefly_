#include "firefly/mps_parser.h"
#include "firefly/presolve.h"
#include "firefly/simplex.h"
#include <iostream>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include <vector>
#include <string>

using namespace firefly;

#include <chrono>

int main() {
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

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
            
            // Solve Without Presolve skipped to prevent hanging
            std::cout << "  [Without Presolve]\n"
                      << "    Parsed in: " << parse_time << "ms\n"
                      << "    Size:      " << orig_prob.row_names.size() << " rows, " << orig_prob.col_names.size() << " cols\n";
                      
            start = std::chrono::high_resolution_clock::now();
            auto presolved = Presolver::presolve(orig_prob);
            end = std::chrono::high_resolution_clock::now();
            auto presolve_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            
            std::cout << "  [With Presolve]\n"
                      << "    Presolved in: " << presolve_time << "ms\n"
                      << "    Stats:        " << presolved.stats.rows_removed << " rows rem, "
                      << presolved.stats.variables_fixed << " vars fix, "
                      << presolved.stats.bounds_tightened << " bnds tight, "
                      << presolved.stats.scaling_applied << " scales\n"
                      << "    Size:         " << presolved.stats.reduced_rows << " rows, " 
                      << presolved.stats.reduced_cols << " cols\n";
            std::cout << "\n";
            std::cout.flush();
        } catch (const std::exception& e) {
            std::cout << "  FAILED\n  Error: " << e.what() << "\n\n";
        }
    }
    return 0;
}
