#include <iostream>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include <vector>
#include <chrono>
#include <iomanip>
#include <random>
#include "firefly/core.h"
#include "firefly/presolve.h"
#include "firefly/simplex.h"
#include "firefly/pdlp.h"

using namespace firefly;

Problem generate_packing_problem(int M, int N) {
    Problem p;
    p.name = "RandomPacking_" + std::to_string(M) + "x" + std::to_string(N);
    p.minimize = false; // Maximize
    p.objective_name = "OBJ";
    
    std::mt19937 gen(42);
    std::uniform_real_distribution<> val_dist(0.1, 1.0);
    std::uniform_real_distribution<> sparsity_dist(0.0, 1.0);
    double sparsity = 0.1; // 10% non-zeros

    p.row_names.resize(M);
    p.row_senses.resize(M, 'L');
    p.row_lower_bounds.resize(M, -std::numeric_limits<double>::infinity());
    p.row_upper_bounds.resize(M);
    for (int i = 0; i < M; ++i) {
        p.row_names[i] = "R" + std::to_string(i);
        p.row_to_index[p.row_names[i]] = i;
        p.row_upper_bounds[i] = val_dist(gen) * (N * sparsity * 0.5); // capacity
    }

    p.col_names.resize(N);
    p.col_lower_bounds.resize(N, 0.0);
    p.col_upper_bounds.resize(N, std::numeric_limits<double>::infinity());
    p.is_integer.resize(N, false);
    p.objective.resize(N);
    for (int j = 0; j < N; ++j) {
        p.col_names[j] = "C" + std::to_string(j);
        p.col_to_index[p.col_names[j]] = j;
        p.objective[j] = val_dist(gen); // Profit
    }

    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < M; ++i) {
            if (sparsity_dist(gen) < sparsity) {
                p.matrix.push_back({(size_t)i, (size_t)j, val_dist(gen)});
            }
        }
    }
    
    return p;
}

int main(int argc, char** argv) {
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::vector<std::pair<int, int>> sizes = {
        {100, 100},
        {500, 500},
        {1000, 1000},
        {2000, 2000},
        {5000, 5000}
    };

    std::cout << "Generating and running benchmarks...\n";
    std::cout << std::left << std::setw(15) << "Size (MxN)"
              << std::setw(15) << "Simplex (ms)"
              << std::setw(15) << "PDLP GPU (ms)"
              << std::setw(10) << "Speedup" << "\n";
    std::cout << std::string(55, '-') << "\n";

    for (auto size : sizes) {
        int M = size.first;
        int N = size.second;
        Problem p = generate_packing_problem(M, N);
        auto pre = Presolver::presolve(p);

        // Run Simplex
        SimplexOptions sx_opts;
        sx_opts.max_iterations = 50000;
        auto start_sx = std::chrono::high_resolution_clock::now();
        auto sx_res = SimplexSolver::solve(pre, sx_opts);
        auto end_sx = std::chrono::high_resolution_clock::now();
        double sx_ms = std::chrono::duration<double, std::milli>(end_sx - start_sx).count();

        // Run PDLP GPU
        PDLPOptions pdlp_opts;
        pdlp_opts.max_iterations = 20000;
        pdlp_opts.tolerance = 1e-4;
        pdlp_opts.enable_restarts = false; // Using fixed optimal step size from previous fix
        auto start_pdlp = std::chrono::high_resolution_clock::now();
        auto pdlp_res = PDLPSolver::solve(pre, pdlp_opts);
        auto end_pdlp = std::chrono::high_resolution_clock::now();
        double pdlp_ms = std::chrono::duration<double, std::milli>(end_pdlp - start_pdlp).count();

        if (sx_res.status != SolveStatus::OPTIMAL) {
            std::cout << std::left << std::setw(15) << (std::to_string(M) + "x" + std::to_string(N))
                      << "Simplex failed: " << (sx_res.status == SolveStatus::ITERATION_LIMIT ? "Iteration limit" : "Error") << "\n";
            continue;
        }
        if (pdlp_res.status != SolveStatus::OPTIMAL) {
            std::cout << std::left << std::setw(15) << (std::to_string(M) + "x" + std::to_string(N))
                      << "PDLP failed: " << pdlp_res.message << "\n";
            continue;
        }

        double speedup = sx_ms / pdlp_ms;

        std::cout << std::left << std::setw(15) << (std::to_string(M) + "x" + std::to_string(N))
                  << std::setw(15) << sx_ms
                  << std::setw(15) << pdlp_ms
                  << std::setw(10) << speedup << "\n";
    }

    // 100 consecutive solves for memory leak check
    std::cout << "\nRunning 100 consecutive solves on 1000x1000 to check GPU memory stability...\n";
    Problem p1k = generate_packing_problem(1000, 1000);
    auto pre1k = Presolver::presolve(p1k);
    
    for (int i = 0; i < 100; ++i) {
        PDLPOptions pdlp_opts;
        pdlp_opts.max_iterations = 500; // Fast termination for repeated calls
        pdlp_opts.tolerance = 1e-4;
        pdlp_opts.enable_restarts = false;
        auto res = PDLPSolver::solve(pre1k, pdlp_opts);
        if (i % 20 == 0) {
            std::cout << "  Finished solve " << i << "...\n";
        }
    }
    std::cout << "Done 100 solves.\n";

    return 0;
}
