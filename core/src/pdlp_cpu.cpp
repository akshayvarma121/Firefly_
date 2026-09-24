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
    double eta = 0.0;
    double omega = 1.0;
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
        eta = 0.99 / L_norm;
        tau = eta * omega;
        sigma = eta / omega;
    }
    
    std::cout << "DEBUG: L_norm = " << L_norm << ", tau = " << tau << ", sigma = " << sigma << "\n";
    
    std::cout << "PDLP init: tau=" << tau << ", sigma=" << sigma << ", L_norm=" << L_norm << "\n";
    
    size_t iter = 0;
    double p_obj = 0.0, d_obj = 0.0, gap = 1e9;
    double best_gap = gap;
    
    std::vector<double> x_sum(N, 0.0), s_sum(M, 0.0), y_sum(M, 0.0);
    double weight_sum = 0.0;
    double gap_at_last_restart = 1e9;
    size_t last_restart_iter = 0;
    
    std::vector<double> x_at_last_restart = x;
    std::vector<double> y_at_last_restart = y;

    auto compute_gap = [&](const std::vector<double>& test_x, const std::vector<double>& test_y, const std::vector<double>& test_A_trans_y, double& out_p_obj, double& out_d_obj) {
        double p_obj_tmp = 0.0;
        for (size_t j = 0; j < N; ++j) p_obj_tmp += c[j] * test_x[j];
        
        double d_obj_tmp = 0.0;
        for (size_t i = 0; i < M; ++i) {
            double y_proj = test_y[i];
            if (std::isinf(L_s[i]) && L_s[i] < 0 && y_proj > 0) y_proj = 0.0;
            if (std::isinf(U_s[i]) && U_s[i] > 0 && y_proj < 0) y_proj = 0.0;
            if (y_proj > 0) d_obj_tmp += L_s[i] * y_proj;
            else if (y_proj < 0) d_obj_tmp += U_s[i] * y_proj;
        }
        for (size_t j = 0; j < N; ++j) {
            double rc = c[j] - test_A_trans_y[j];
            double rc_proj = rc;
            if (std::isinf(L_x[j]) && L_x[j] < 0 && rc_proj > 0) rc_proj = 0.0;
            if (std::isinf(U_x[j]) && U_x[j] > 0 && rc_proj < 0) rc_proj = 0.0;
            if (rc_proj > 0) d_obj_tmp += L_x[j] * rc_proj;
            else if (rc_proj < 0) d_obj_tmp += U_x[j] * rc_proj;
        }
        out_p_obj = p_obj_tmp;
        out_d_obj = d_obj_tmp;
        return std::abs(p_obj_tmp - d_obj_tmp);
    };
    
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

        for (size_t j = 0; j < N; ++j) x_sum[j] += x[j];
        for (size_t i = 0; i < M; ++i) { s_sum[i] += s[i]; y_sum[i] += y[i]; }
        weight_sum += 1.0;
        
        if (iter % options.callback_frequency == 0 || iter == options.max_iterations - 1) {
            double cur_p_obj, cur_d_obj;
            double cur_gap = compute_gap(x, y, A_trans_y, cur_p_obj, cur_d_obj);
            
            p_obj = cur_p_obj;
            d_obj = cur_d_obj;
            gap = cur_gap;

            // Restarts (Adaptive)
            if (options.enable_restarts) {
                std::vector<double> x_avg(N), y_avg(M), s_avg(M);
                for (size_t j = 0; j < N; ++j) x_avg[j] = x_sum[j] / weight_sum;
                for (size_t i = 0; i < M; ++i) {
                    s_avg[i] = s_sum[i] / weight_sum;
                    y_avg[i] = y_sum[i] / weight_sum;
                }

                std::vector<double> A_trans_y_avg(N, 0.0);
                for (size_t j = 0; j < N; ++j) {
                    for (Eigen::SparseMatrix<double, Eigen::ColMajor>::InnerIterator it(A_col, j); it; ++it) {
                        A_trans_y_avg[j] += it.value() * y_avg[it.row()];
                    }
                }
                
                double avg_p_obj, avg_d_obj;
                double avg_gap = compute_gap(x_avg, y_avg, A_trans_y_avg, avg_p_obj, avg_d_obj);
                
                double best_metric = std::min(cur_gap, avg_gap);
                bool do_restart = false;
                
                if (gap_at_last_restart > 1e8) {
                    gap_at_last_restart = best_metric;
                } else if (best_metric < 0.5 * gap_at_last_restart) {
                    do_restart = true;
                } else if (iter - last_restart_iter >= 1000) {
                    do_restart = true;
                }
                
                if (do_restart) {
                    double delta_primal = 0.0;
                    for (size_t j = 0; j < N; ++j) {
                        double diff = x[j] - x_at_last_restart[j];
                        delta_primal += diff * diff;
                    }
                    delta_primal = std::sqrt(delta_primal);
                    
                    double delta_dual = 0.0;
                    for (size_t i = 0; i < M; ++i) {
                        double diff = y[i] - y_at_last_restart[i];
                        delta_dual += diff * diff;
                    }
                    delta_dual = std::sqrt(delta_dual);
                    
                    if (delta_dual > 1e-12) {
                        double new_omega_raw = omega * (delta_primal / delta_dual);
                        double smoothed_omega = std::exp((std::log(omega) + std::log(new_omega_raw)) / 2.0);
                        
                        const double MAX_RATIO = 10.0;
                        double clamped_omega = std::max(omega / MAX_RATIO, std::min(smoothed_omega, omega * MAX_RATIO));
                        
                        bool ratio_clamped = (clamped_omega != smoothed_omega);
                        
                        // Derive absolute limits from eta to keep tau and sigma numerically safe
                        // Limit step sizes to 1e8 max and 1e-8 min to prevent precision loss in FP64
                        const double SAFE_MAX = 1e8;
                        const double SAFE_MIN = 1e-8;
                        double OMEGA_MAX = std::min(SAFE_MAX / eta, eta / SAFE_MIN);
                        double OMEGA_MIN = std::max(SAFE_MIN / eta, eta / SAFE_MAX);
                        
                        double final_omega = std::max(OMEGA_MIN, std::min(clamped_omega, OMEGA_MAX));
                        bool bounds_clamped = (final_omega != clamped_omega);
                        
                        omega = final_omega;
                        
                        if (ratio_clamped || bounds_clamped) {
                            std::cout << "  [CLAMP] Omega update clamped. raw_smoothed=" << smoothed_omega 
                                      << " ratio_clamp=" << ratio_clamped 
                                      << " bounds_clamp=" << bounds_clamped 
                                      << " final_omega=" << omega << "\n";
                        }
                        
                        if (L_norm > 1e-12) {
                            tau = eta * omega;
                            sigma = eta / omega;
                        }
                    }

                    std::string chosen = (avg_gap < cur_gap) ? "AVERAGE" : "CURRENT";
                    std::cout << "  [RESTART] Iter: " << iter 
                              << " | Chosen: " << chosen 
                              << " | Best Metric: " << best_metric 
                              << " | Avg Gap: " << avg_gap 
                              << " | Cur Gap: " << cur_gap 
                              << " | Target: " << (0.5 * gap_at_last_restart) 
                              << " | omega: " << omega << " tau: " << tau << " sigma: " << sigma << "\n";
                    if (avg_gap < cur_gap) {
                        x = x_avg;
                        y = y_avg;
                        s = s_avg;
                        p_obj = avg_p_obj;
                        d_obj = avg_d_obj;
                        gap = avg_gap;
                        // For next iteration's evaluation, A_trans_y would need update but it's recomputed at start of loop
                    }
                    
                    for (size_t j = 0; j < N; ++j) x_sum[j] = 0.0;
                    for (size_t i = 0; i < M; ++i) { s_sum[i] = 0.0; y_sum[i] = 0.0; }
                    weight_sum = 0.0;
                    
                    gap_at_last_restart = gap;
                    last_restart_iter = iter;
                    
                    x_prev = x;
                    s_prev = s;
                    
                    x_at_last_restart = x;
                    y_at_last_restart = y;
                }
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
        result.status = SolveStatus::ITERATION_LIMIT;
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
