#include "firefly/pdlp.h"
#include <iostream>
#include <cmath>
#include <algorithm>

namespace firefly {

namespace pdlp_cpu {
    SolveResult solve(const PresolvedProblem& pre, const PDLPOptions& options);
}

#ifdef FIREFLY_WITH_CUDA
namespace pdlp_cuda {
    SolveResult solve(const PresolvedProblem& pre, const PDLPOptions& options);
}
#endif

SolveResult PDLPSolver::solve(const PresolvedProblem& pre, const PDLPOptions& options) {
    // 1. Make a copy of the presolved problem to apply Ruiz Equilibration
    PresolvedProblem scaled_pre = pre;
    
    size_t M = scaled_pre.stats.reduced_rows;
    size_t N = scaled_pre.stats.reduced_cols;
    
    std::vector<double> cum_r(M, 1.0);
    std::vector<double> cum_c(N, 1.0);
    
    // Ruiz equilibration (typically 5-10 iterations)
    const int RUIZ_ITERS = 10;
    for (int iter = 0; iter < RUIZ_ITERS; ++iter) {
        std::vector<double> r_max(M, 0.0);
        std::vector<double> c_max(N, 0.0);
        
        for (const auto& t : scaled_pre.problem.matrix) {
            double val = std::abs(t.value);
            r_max[t.row] = std::max(r_max[t.row], val);
            c_max[t.col] = std::max(c_max[t.col], val);
        }
        
        std::vector<double> r_factor(M, 1.0);
        std::vector<double> c_factor(N, 1.0);
        
        for (size_t i = 0; i < M; ++i) {
            if (r_max[i] > 1e-12) {
                r_factor[i] = 1.0 / std::sqrt(r_max[i]);
                cum_r[i] *= r_factor[i];
            }
        }
        for (size_t j = 0; j < N; ++j) {
            if (c_max[j] > 1e-12) {
                c_factor[j] = 1.0 / std::sqrt(c_max[j]);
                cum_c[j] *= c_factor[j];
            }
        }
        
        // Scale matrix
        for (auto& t : scaled_pre.problem.matrix) {
            t.value *= r_factor[t.row] * c_factor[t.col];
        }
    }
    
    // Scale bounds and objective
    for (size_t i = 0; i < M; ++i) {
        if (!std::isinf(scaled_pre.problem.row_lower_bounds[i])) {
            scaled_pre.problem.row_lower_bounds[i] *= cum_r[i];
        }
        if (!std::isinf(scaled_pre.problem.row_upper_bounds[i])) {
            scaled_pre.problem.row_upper_bounds[i] *= cum_r[i];
        }
    }
    
    for (size_t j = 0; j < N; ++j) {
        if (!std::isinf(scaled_pre.problem.col_lower_bounds[j])) {
            scaled_pre.problem.col_lower_bounds[j] /= cum_c[j];
        }
        if (!std::isinf(scaled_pre.problem.col_upper_bounds[j])) {
            scaled_pre.problem.col_upper_bounds[j] /= cum_c[j];
        }
        scaled_pre.problem.objective[j] *= cum_c[j];
    }
    
    // Update postsolve scaling factors to ensure unscaling works correctly
    for (size_t j = 0; j < N; ++j) {
        size_t orig_j = scaled_pre.postsolve.reduced_col_to_orig_col[j];
        scaled_pre.postsolve.col_scale_factors[orig_j] *= cum_c[j];
    }
    for (size_t i = 0; i < M; ++i) {
        size_t orig_i = scaled_pre.postsolve.reduced_row_to_orig_row[i];
        scaled_pre.postsolve.row_scale_factors[orig_i] /= cum_r[i];
    }

    // DIAGNOSTIC LOGGING: Compute max row and column norms of scaled matrix
    double max_row_norm = 0.0;
    double min_row_norm = 1e9;
    double max_col_norm = 0.0;
    double min_col_norm = 1e9;
    if (M > 0 && N > 0) {
        std::vector<double> r_norms(M, 0.0);
        std::vector<double> c_norms(N, 0.0);
        for (const auto& t : scaled_pre.problem.matrix) {
            double val = std::abs(t.value);
            r_norms[t.row] = std::max(r_norms[t.row], val);
            c_norms[t.col] = std::max(c_norms[t.col], val);
        }
        for (size_t i = 0; i < M; ++i) {
            max_row_norm = std::max(max_row_norm, r_norms[i]);
            min_row_norm = std::min(min_row_norm, r_norms[i]);
        }
        for (size_t j = 0; j < N; ++j) {
            max_col_norm = std::max(max_col_norm, c_norms[j]);
            min_col_norm = std::min(min_col_norm, c_norms[j]);
        }
    }
    std::cout << "[DIAGNOSTIC] After Ruiz:\n";
    std::cout << "  Row norms (min-max): " << min_row_norm << " to " << max_row_norm << "\n";
    std::cout << "  Col norms (min-max): " << min_col_norm << " to " << max_col_norm << "\n";


    // Power iteration to estimate ||A||_2 of the scaled matrix
    double lambda_max = 0.0;
    if (M > 0 && N > 0) {
        std::vector<double> x(N, 1.0 / std::sqrt(N));
        std::vector<double> y(M, 0.0);
        std::vector<double> x_new(N, 0.0);
        
        for (int k = 0; k < 10; ++k) {
            // y = A * x
            std::fill(y.begin(), y.end(), 0.0);
            for (const auto& t : scaled_pre.problem.matrix) {
                y[t.row] += t.value * x[t.col];
            }
            
            // x_new = A^T * y
            std::fill(x_new.begin(), x_new.end(), 0.0);
            for (const auto& t : scaled_pre.problem.matrix) {
                x_new[t.col] += t.value * y[t.row];
            }
            
            double norm_x_new = 0.0;
            for (size_t j = 0; j < N; ++j) {
                norm_x_new += x_new[j] * x_new[j];
            }
            norm_x_new = std::sqrt(norm_x_new);
            
            lambda_max = norm_x_new;
            if (norm_x_new > 1e-12) {
                for (size_t j = 0; j < N; ++j) {
                    x[j] = x_new[j] / norm_x_new;
                }
            } else {
                break; // Matrix is essentially zero
            }
        }
    }
    
    // Operator norm of [A; -I] is sqrt(||A||_2^2 + 1). We know ||A||_2^2 is approx lambda_max.
    double l_norm = std::sqrt(lambda_max + 1.0);
    
    PDLPOptions modified_options = options;
    modified_options.l_norm_estimate = l_norm;

#ifdef FIREFLY_WITH_CUDA
    try {
        return pdlp_cuda::solve(scaled_pre, modified_options);
    } catch (const std::exception& e) {
        std::cerr << "[PDLP] CUDA solver failed (" << e.what() << "). Falling back to CPU.\n";
        return pdlp_cpu::solve(scaled_pre, modified_options);
    }
#else
    return pdlp_cpu::solve(scaled_pre, modified_options);
#endif
}

} // namespace firefly
