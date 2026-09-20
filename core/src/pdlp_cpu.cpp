#include "firefly/pdlp.h"
#include <Eigen/Sparse>
#include <chrono>
#include <cmath>
#include <iostream>
#include <algorithm>

namespace firefly {
namespace pdlp_cpu {

SolveResult solve(const PresolvedProblem& pre, const PDLPOptions& options) {
    SolveResult result;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    size_t M = pre.stats.reduced_rows;
    size_t N = pre.stats.reduced_cols;
    
    // Construct A matrix for CPU using Eigen
    Eigen::SparseMatrix<double, Eigen::RowMajor> A(M, N);
    std::vector<Eigen::Triplet<double>> triplets;
    for (const auto& t : pre.problem.matrix) {
        triplets.push_back(Eigen::Triplet<double>(t.row, t.col, t.value));
    }
    A.setFromTriplets(triplets.begin(), triplets.end());
    Eigen::SparseMatrix<double, Eigen::ColMajor> A_col = A;
    Eigen::SparseMatrix<double, Eigen::RowMajor> A_trans = A_col.transpose();
    
    // Variables: x (size N), s (size M)
    std::vector<double> x(N, 0.0), s(M, 0.0), y(M, 0.0);
    std::vector<double> x_prev(N, 0.0), s_prev(M, 0.0);
    
    // Bounds for x
    const auto& L_x = pre.problem.col_lower_bounds;
    const auto& U_x = pre.problem.col_upper_bounds;
    const auto& c = pre.problem.objective;
    
    // Bounds for s (row bounds)
    const auto& L_s = pre.problem.row_lower_bounds;
    const auto& U_s = pre.problem.row_upper_bounds;
    
    // Initial step sizes
    double tau = 1.0;
    double sigma = 1.0;
    // Estimate norm of A' = [A, -I]
    // A simple estimate for 2-norm bound is sqrt( max_col_norm1 * max_row_norm1 )
    double max_col_norm = 1.0;
    for (size_t j = 0; j < N; ++j) {
        double norm = 0;
        for (Eigen::SparseMatrix<double, Eigen::ColMajor>::InnerIterator it(A_col, j); it; ++it) {
            norm += std::abs(it.value());
        }
        max_col_norm = std::max(max_col_norm, norm);
    }
    double max_row_norm = 1.0;
    for (size_t i = 0; i < M; ++i) {
        double norm = 1.0; // due to -I for slack
        for (Eigen::SparseMatrix<double, Eigen::RowMajor>::InnerIterator it(A, i); it; ++it) {
            norm += std::abs(it.value());
        }
        max_row_norm = std::max(max_row_norm, norm);
    }
    double L_norm = (options.l_norm_estimate > 0.0) ? options.l_norm_estimate : std::sqrt(max_col_norm * max_row_norm);
    if (L_norm > 1e-12) {
        tau = 0.99 / L_norm;
        sigma = 0.99 / L_norm;
    }
    
    std::cout << "DEBUG: L_norm = " << L_norm << ", tau = " << tau << ", sigma = " << sigma << "\n";
    
    std::cout << "PDLP init: tau=" << tau << ", sigma=" << sigma << ", L_norm=" << L_norm << "\n";
    
    size_t iter = 0;
    double p_obj = 0.0, d_obj = 0.0, gap = 1e9;
    double best_gap = gap;
    
    // Arrays for matrix products
    std::vector<double> A_trans_y(N, 0.0);
    std::vector<double> A_delta_x(M, 0.0);
    
    for (; iter < options.max_iterations; ++iter) {
        // 1. A^T y
        for (size_t j = 0; j < N; ++j) A_trans_y[j] = 0.0;
        for (size_t j = 0; j < N; ++j) {
            for (Eigen::SparseMatrix<double, Eigen::ColMajor>::InnerIterator it(A_col, j); it; ++it) {
                A_trans_y[j] += it.value() * y[it.row()];
            }
        }
        
        // 2. Primal update x, s
        x_prev = x;
        s_prev = s;
        for (size_t j = 0; j < N; ++j) {
            double x_temp = x[j] - tau * c[j] + tau * A_trans_y[j];
            // project
            x[j] = std::max(L_x[j], std::min(U_x[j], x_temp));
        }
        for (size_t i = 0; i < M; ++i) {
            double s_temp = s[i] - tau * y[i]; // A'^T y = -y for slack
            // project
            s[i] = std::max(L_s[i], std::min(U_s[i], s_temp));
        }
        
        // 3. A (2x - x_prev)
        for (size_t i = 0; i < M; ++i) A_delta_x[i] = 0.0;
        for (size_t i = 0; i < M; ++i) {
            for (Eigen::SparseMatrix<double, Eigen::RowMajor>::InnerIterator it(A, i); it; ++it) {
                A_delta_x[i] += it.value() * (2.0 * x[it.col()] - x_prev[it.col()]);
            }
        }
        
        // 4. Dual update y
        for (size_t i = 0; i < M; ++i) {
            // Gradient ascent on y: y = y + sigma * (- (Ax - s)) 
            // Evaluated at extrapolated point (2x^{k+1} - x^k)
            double delta_s = 2.0 * s[i] - s_prev[i];
            y[i] = y[i] - sigma * (A_delta_x[i] - delta_s); // b=0
        }
        
        if (iter % options.callback_frequency == 0 || iter == options.max_iterations - 1) {
            p_obj = 0.0;
            for (size_t j = 0; j < N; ++j) p_obj += c[j] * x[j];
            
            d_obj = 0.0;
            for (size_t i = 0; i < M; ++i) {
                double y_proj = y[i];
                if (std::isinf(L_s[i]) && L_s[i] < 0 && y_proj > 0) y_proj = 0.0;
                if (std::isinf(U_s[i]) && U_s[i] > 0 && y_proj < 0) y_proj = 0.0;
                if (y_proj > 0) d_obj += L_s[i] * y_proj;
                else if (y_proj < 0) d_obj += U_s[i] * y_proj;
            }
            for (size_t j = 0; j < N; ++j) {
                double rc = c[j] - A_trans_y[j];
                double rc_proj = rc;
                if (std::isinf(L_x[j]) && L_x[j] < 0 && rc_proj > 0) rc_proj = 0.0;
                if (std::isinf(U_x[j]) && U_x[j] > 0 && rc_proj < 0) rc_proj = 0.0;
                if (rc_proj > 0) d_obj += L_x[j] * rc_proj;
                else if (rc_proj < 0) d_obj += U_x[j] * rc_proj;
            }
            
            // Proxy gap measure
            double cur_gap = std::abs(p_obj - d_obj);
            gap = cur_gap;

            // Restarts (Adaptive)
            if (options.enable_restarts) {
                // Adaptive step sizes removed for constant optimal step size testing
            }
            
            if (options.iteration_callback) {
                auto now = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
                options.iteration_callback(iter, p_obj + pre.postsolve.objective_offset, d_obj + pre.postsolve.objective_offset, ms);
            }
            
            if (cur_gap < options.tolerance) {
                break;
            }
        }
    }
    
    if (gap <= options.tolerance) {
        result.status = SolveStatus::OPTIMAL;
    } else {
        result.status = SolveStatus::ERROR;
        result.message = "Iteration limit reached without convergence. Final gap: " + std::to_string(gap);
    }
    result.phase1_iterations = iter;
    
    p_obj = 0.0;
    for (size_t j = 0; j < N; ++j) p_obj += c[j] * x[j];
    result.objective_value = p_obj + pre.postsolve.objective_offset;
    result.primal_dual_gap = gap;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    result.solve_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    // Reconstruct primal solution
    result.primal_solution = Presolver::postsolve_primal(pre, x);
    // Reconstruct dual solution
    result.dual_solution = Presolver::postsolve_dual(pre, y);
    
    return result;
}

} // namespace pdlp_cpu
} // namespace firefly
