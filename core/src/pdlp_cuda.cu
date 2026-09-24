#include "firefly/pdlp.h"
#include "firefly/cuda_context.h"
#include <cuda_runtime.h>
#include <cublas_v2.h>
#include <cusparse.h>
#include <iostream>
#include <chrono>
#include <mutex>
#include <cmath>

namespace firefly {
namespace pdlp_cuda {

#define CHECK_CUDA(func) \
{ \
    cudaError_t status = (func); \
    if (status != cudaSuccess) { \
        std::cerr << "CUDA Error at " << __FILE__ << ":" << __LINE__ \
                  << " code=" << status << " \"" << cudaGetErrorString(status) << "\"" << std::endl; \
        throw std::runtime_error("CUDA Error"); \
    } \
}

#define CHECK_CUSPARSE(func) \
{ \
    cusparseStatus_t status = (func); \
    if (status != CUSPARSE_STATUS_SUCCESS) { \
        std::cerr << "cuSPARSE Error at " << __FILE__ << ":" << __LINE__ \
                  << " code=" << status << std::endl; \
        throw std::runtime_error("cuSPARSE Error"); \
    } \
}

__global__ void project_kernel(double* x, const double* L, const double* U, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        double val = x[i];
        if (val < L[i]) val = L[i];
        if (val > U[i]) val = U[i];
        x[i] = val;
    }
}

__global__ void update_x_temp_kernel(double* x, const double* c, const double* A_trans_y, double tau, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        x[i] = x[i] - tau * c[i] + tau * A_trans_y[i];
    }
}

__global__ void update_s_temp_kernel(double* s, const double* y, double tau, int m) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < m) {
        s[i] = s[i] - tau * y[i];
    }
}

__global__ void update_y_kernel(double* y, const double* A_delta_x, const double* s, const double* s_prev, double sigma, int m) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < m) {
        double delta_s = 2.0 * s[i] - s_prev[i];
        y[i] = y[i] - sigma * (A_delta_x[i] - delta_s);
    }
}

__global__ void calc_delta_x_kernel(double* delta_x, const double* x, const double* x_prev, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        delta_x[i] = 2.0 * x[i] - x_prev[i];
    }
}

__global__ void add_to_sum_kernel(double* sum, const double* val, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        sum[i] += val[i];
    }
}

SolveResult solve(const PresolvedProblem& pre, const PDLPOptions& options) {
    SolveResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    size_t M = pre.stats.reduced_rows;
    size_t N = pre.stats.reduced_cols;
    size_t nnz = pre.problem.matrix.size();

    // Get global CUDA context handles and lock
    auto& ctx = CudaContext::get_instance();
    std::lock_guard<std::mutex> lock(ctx.get_mutex());
    cublasHandle_t cublas_handle = ctx.get_cublas();
    cusparseHandle_t cusparse_handle = ctx.get_cusparse();

    // Convert Triplets to CSC format on CPU
    std::vector<int> csc_col_ptr(N + 1, 0);
    for (const auto& t : pre.problem.matrix) {
        csc_col_ptr[t.col + 1]++;
    }
    for (size_t j = 0; j < N; ++j) {
        csc_col_ptr[j + 1] += csc_col_ptr[j];
    }
    
    std::vector<int> csc_row_ind(nnz, 0);
    std::vector<double> h_val(nnz, 0.0);
    std::vector<int> cur_col_ptr = csc_col_ptr;
    for (const auto& t : pre.problem.matrix) {
        int idx = cur_col_ptr[t.col]++;
        csc_row_ind[idx] = static_cast<int>(t.row);
        h_val[idx] = t.value;
    }

    // Allocate Device Memory
    int* d_row_ptr;
    int* d_col_ind;
    double* d_val;
    CHECK_CUDA(cudaMalloc(&d_row_ptr, (M + 1) * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&d_col_ind, nnz * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&d_val, nnz * sizeof(double)));

    int* d_csc_col_ptr;
    int* d_csc_row_ind;
    CHECK_CUDA(cudaMalloc(&d_csc_col_ptr, (N + 1) * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&d_csc_row_ind, nnz * sizeof(int)));
    
    // Copy data to device
    CHECK_CUDA(cudaMemcpy(d_csc_col_ptr, csc_col_ptr.data(), (N + 1) * sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_csc_row_ind, csc_row_ind.data(), nnz * sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_val, h_val.data(), nnz * sizeof(double), cudaMemcpyHostToDevice));

    // Allocate variable vectors
    double *d_x, *d_x_prev, *d_s, *d_s_prev, *d_y;
    double *d_L_x, *d_U_x, *d_L_s, *d_U_s, *d_c;
    double *d_A_trans_y, *d_A_delta_x, *d_delta_x;
    double *d_x_sum, *d_s_sum, *d_y_sum;
    
    CHECK_CUDA(cudaMalloc(&d_x, N * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_x_prev, N * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_s, M * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_s_prev, M * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_y, M * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_x_sum, N * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_s_sum, M * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_y_sum, M * sizeof(double)));
    CHECK_CUDA(cudaMemset(d_x, 0, N * sizeof(double)));
    CHECK_CUDA(cudaMemset(d_x_prev, 0, N * sizeof(double)));
    CHECK_CUDA(cudaMemset(d_s, 0, M * sizeof(double)));
    CHECK_CUDA(cudaMemset(d_s_prev, 0, M * sizeof(double)));
    CHECK_CUDA(cudaMemset(d_y, 0, M * sizeof(double)));
    CHECK_CUDA(cudaMemset(d_x_sum, 0, N * sizeof(double)));
    CHECK_CUDA(cudaMemset(d_s_sum, 0, M * sizeof(double)));
    CHECK_CUDA(cudaMemset(d_y_sum, 0, M * sizeof(double)));

    CHECK_CUDA(cudaMalloc(&d_L_x, N * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_U_x, N * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_L_s, M * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_U_s, M * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_c, N * sizeof(double)));
    
    CHECK_CUDA(cudaMemcpy(d_L_x, pre.problem.col_lower_bounds.data(), N * sizeof(double), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_U_x, pre.problem.col_upper_bounds.data(), N * sizeof(double), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_L_s, pre.problem.row_lower_bounds.data(), M * sizeof(double), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_U_s, pre.problem.row_upper_bounds.data(), M * sizeof(double), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_c, pre.problem.objective.data(), N * sizeof(double), cudaMemcpyHostToDevice));

    CHECK_CUDA(cudaMalloc(&d_A_trans_y, N * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_A_delta_x, M * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_delta_x, N * sizeof(double)));

    // Create cuSPARSE matrix and vector descriptors
    cusparseSpMatDescr_t matA;
    CHECK_CUSPARSE(cusparseCreateCsc(&matA, M, N, nnz, d_csc_col_ptr, d_csc_row_ind, d_val, CUSPARSE_INDEX_32I, CUSPARSE_INDEX_32I, CUSPARSE_INDEX_BASE_ZERO, CUDA_R_64F));
    
    cusparseDnVecDescr_t vecY, vecA_trans_y, vecDeltaX, vecA_delta_x;
    CHECK_CUSPARSE(cusparseCreateDnVec(&vecY, M, d_y, CUDA_R_64F));
    CHECK_CUSPARSE(cusparseCreateDnVec(&vecA_trans_y, N, d_A_trans_y, CUDA_R_64F));
    CHECK_CUSPARSE(cusparseCreateDnVec(&vecDeltaX, N, d_delta_x, CUDA_R_64F));
    CHECK_CUSPARSE(cusparseCreateDnVec(&vecA_delta_x, M, d_A_delta_x, CUDA_R_64F));

    double alpha = 1.0, beta = 0.0;
    
    // Allocate buffer for SpMV
    void* dBuffer = nullptr;
    size_t bufferSize = 0;
    CHECK_CUSPARSE(cusparseSpMV_bufferSize(cusparse_handle, CUSPARSE_OPERATION_TRANSPOSE, &alpha, matA, vecY, &beta, vecA_trans_y, CUDA_R_64F, CUSPARSE_SPMV_ALG_DEFAULT, &bufferSize));
    size_t bufferSize2 = 0;
    CHECK_CUSPARSE(cusparseSpMV_bufferSize(cusparse_handle, CUSPARSE_OPERATION_NON_TRANSPOSE, &alpha, matA, vecDeltaX, &beta, vecA_delta_x, CUDA_R_64F, CUSPARSE_SPMV_ALG_DEFAULT, &bufferSize2));
    size_t finalBufferSize = std::max(bufferSize, bufferSize2);
    if (finalBufferSize > 0) {
        CHECK_CUDA(cudaMalloc(&dBuffer, finalBufferSize));
    }

    // Simple fixed step sizes based on Infinity/1 norms (Pock-Chambolle)
    double max_col_norm = 1.0; 
    std::vector<double> col_norms(N, 0.0);
    std::vector<double> row_norms(M, 1.0);
    for (const auto& t : pre.problem.matrix) {
        col_norms[t.col] += std::abs(t.value);
        row_norms[t.row] += std::abs(t.value);
    }
    for (size_t j = 0; j < N; ++j) {
        max_col_norm = std::max(max_col_norm, col_norms[j]);
    }
    double max_row_norm = 1.0;
    for (size_t i = 0; i < M; ++i) {
        max_row_norm = std::max(max_row_norm, row_norms[i]);
    }
    double L_norm = (options.l_norm_estimate > 0.0) ? options.l_norm_estimate : std::sqrt(max_col_norm * max_row_norm);
    
    double eta = 0.0;
    double omega = 1.0;
    double tau = 1e-3;
    double sigma = 1e-3;
    if (L_norm > 1e-12) {
        eta = 0.99 / L_norm;
        tau = eta * omega;
        sigma = eta / omega;
    }

    int blockSize = 256;
    int gridN = (N + blockSize - 1) / blockSize;
    int gridM = (M + blockSize - 1) / blockSize;

    size_t iter = 0;
    double gap = 1e9;
    double best_gap = gap;
    
    double weight_sum = 0.0;
    double gap_at_last_restart = 1e9;
    size_t last_restart_iter = 0;
    
    std::vector<double> h_x_at_last_restart(N, 0.0);
    std::vector<double> h_y_at_last_restart(M, 0.0);
    
    auto compute_gap_host = [&](const std::vector<double>& test_x, const std::vector<double>& test_y, const std::vector<double>& test_A_trans_y, double& out_p_obj, double& out_d_obj) {
        double p_obj_tmp = 0.0;
        for (size_t j = 0; j < N; ++j) p_obj_tmp += pre.problem.objective[j] * test_x[j];
        
        double d_obj_tmp = 0.0;
        for (size_t i = 0; i < M; ++i) {
            double y_proj = test_y[i];
            if (std::isinf(pre.problem.row_lower_bounds[i]) && pre.problem.row_lower_bounds[i] < 0 && y_proj > 0) y_proj = 0.0;
            if (std::isinf(pre.problem.row_upper_bounds[i]) && pre.problem.row_upper_bounds[i] > 0 && y_proj < 0) y_proj = 0.0;
            if (y_proj > 0) d_obj_tmp += pre.problem.row_lower_bounds[i] * y_proj;
            else if (y_proj < 0) d_obj_tmp += pre.problem.row_upper_bounds[i] * y_proj;
        }
        for (size_t j = 0; j < N; ++j) {
            double rc = pre.problem.objective[j] - test_A_trans_y[j];
            double rc_proj = rc;
            if (std::isinf(pre.problem.col_lower_bounds[j]) && pre.problem.col_lower_bounds[j] < 0 && rc_proj > 0) rc_proj = 0.0;
            if (std::isinf(pre.problem.col_upper_bounds[j]) && pre.problem.col_upper_bounds[j] > 0 && rc_proj < 0) rc_proj = 0.0;
            if (rc_proj > 0) d_obj_tmp += pre.problem.col_lower_bounds[j] * rc_proj;
            else if (rc_proj < 0) d_obj_tmp += pre.problem.col_upper_bounds[j] * rc_proj;
        }
        out_p_obj = p_obj_tmp;
        out_d_obj = d_obj_tmp;
        return std::abs(p_obj_tmp - d_obj_tmp);
    };
    
    for (; iter < options.max_iterations; ++iter) {
        // 1. A^T y
        CHECK_CUSPARSE(cusparseSpMV(cusparse_handle, CUSPARSE_OPERATION_TRANSPOSE, &alpha, matA, vecY, &beta, vecA_trans_y, CUDA_R_64F, CUSPARSE_SPMV_ALG_DEFAULT, dBuffer));
        
        // Save x_prev, s_prev
        CHECK_CUDA(cudaMemcpy(d_x_prev, d_x, N * sizeof(double), cudaMemcpyDeviceToDevice));
        CHECK_CUDA(cudaMemcpy(d_s_prev, d_s, M * sizeof(double), cudaMemcpyDeviceToDevice));

        // 2. Primal Update
        update_x_temp_kernel<<<gridN, blockSize>>>(d_x, d_c, d_A_trans_y, tau, N);
        project_kernel<<<gridN, blockSize>>>(d_x, d_L_x, d_U_x, N);
        
        update_s_temp_kernel<<<gridM, blockSize>>>(d_s, d_y, tau, M);
        project_kernel<<<gridM, blockSize>>>(d_s, d_L_s, d_U_s, M);

        // 3. A (2x - x_prev)
        calc_delta_x_kernel<<<gridN, blockSize>>>(d_delta_x, d_x, d_x_prev, N);
        CHECK_CUSPARSE(cusparseSpMV(cusparse_handle, CUSPARSE_OPERATION_NON_TRANSPOSE, &alpha, matA, vecDeltaX, &beta, vecA_delta_x, CUDA_R_64F, CUSPARSE_SPMV_ALG_DEFAULT, dBuffer));
        
        // 4. Dual Update
        update_y_kernel<<<gridM, blockSize>>>(d_y, d_A_delta_x, d_s, d_s_prev, sigma, M);

        add_to_sum_kernel<<<gridN, blockSize>>>(d_x_sum, d_x, N);
        add_to_sum_kernel<<<gridM, blockSize>>>(d_s_sum, d_s, M);
        add_to_sum_kernel<<<gridM, blockSize>>>(d_y_sum, d_y, M);
        weight_sum += 1.0;

        // Evaluate and Callback
        if (iter % options.callback_frequency == 0 || iter == options.max_iterations - 1) {
            std::vector<double> h_x_tmp(N);
            std::vector<double> h_s_tmp(M);
            std::vector<double> h_y_tmp(M);
            std::vector<double> h_A_trans_y_tmp(N);
            CHECK_CUDA(cudaMemcpy(h_x_tmp.data(), d_x, N * sizeof(double), cudaMemcpyDeviceToHost));
            CHECK_CUDA(cudaMemcpy(h_s_tmp.data(), d_s, M * sizeof(double), cudaMemcpyDeviceToHost));
            CHECK_CUDA(cudaMemcpy(h_y_tmp.data(), d_y, M * sizeof(double), cudaMemcpyDeviceToHost));
            CHECK_CUDA(cudaMemcpy(h_A_trans_y_tmp.data(), d_A_trans_y, N * sizeof(double), cudaMemcpyDeviceToHost));
            
            double p_obj_tmp, d_obj_tmp;
            double cur_gap = compute_gap_host(h_x_tmp, h_y_tmp, h_A_trans_y_tmp, p_obj_tmp, d_obj_tmp);
            gap = cur_gap;

            // Restarts (Adaptive)
            if (options.enable_restarts) {
                std::vector<double> h_x_sum(N), h_s_sum(M), h_y_sum(M);
                CHECK_CUDA(cudaMemcpy(h_x_sum.data(), d_x_sum, N * sizeof(double), cudaMemcpyDeviceToHost));
                CHECK_CUDA(cudaMemcpy(h_s_sum.data(), d_s_sum, M * sizeof(double), cudaMemcpyDeviceToHost));
                CHECK_CUDA(cudaMemcpy(h_y_sum.data(), d_y_sum, M * sizeof(double), cudaMemcpyDeviceToHost));

                std::vector<double> h_x_avg(N), h_s_avg(M), h_y_avg(M);
                for (size_t j = 0; j < N; ++j) h_x_avg[j] = h_x_sum[j] / weight_sum;
                for (size_t i = 0; i < M; ++i) {
                    h_s_avg[i] = h_s_sum[i] / weight_sum;
                    h_y_avg[i] = h_y_sum[i] / weight_sum;
                }

                std::vector<double> h_A_trans_y_avg(N, 0.0);
                for (size_t j = 0; j < N; ++j) {
                    for (int idx = csc_col_ptr[j]; idx < csc_col_ptr[j + 1]; ++idx) {
                        h_A_trans_y_avg[j] += h_val[idx] * h_y_avg[csc_row_ind[idx]];
                    }
                }
                
                double avg_p_obj, avg_d_obj;
                double avg_gap = compute_gap_host(h_x_avg, h_y_avg, h_A_trans_y_avg, avg_p_obj, avg_d_obj);
                
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
                        double diff = h_x_tmp[j] - h_x_at_last_restart[j];
                        delta_primal += diff * diff;
                    }
                    delta_primal = std::sqrt(delta_primal);
                    
                    double delta_dual = 0.0;
                    for (size_t i = 0; i < M; ++i) {
                        double diff = h_y_tmp[i] - h_y_at_last_restart[i];
                        delta_dual += diff * diff;
                    }
                    delta_dual = std::sqrt(delta_dual);
                    
                    if (delta_dual > 1e-12) {
                        double new_omega_raw = omega * (delta_primal / delta_dual);
                        double smoothed_omega = std::exp((std::log(omega) + std::log(new_omega_raw)) / 2.0);
                        
                        const double MAX_RATIO = 10.0;
                        double clamped_omega = std::max(omega / MAX_RATIO, std::min(smoothed_omega, omega * MAX_RATIO));
                        
                        bool ratio_clamped = (clamped_omega != smoothed_omega);
                        
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
                        CHECK_CUDA(cudaMemcpy(d_x, h_x_avg.data(), N * sizeof(double), cudaMemcpyHostToDevice));
                        CHECK_CUDA(cudaMemcpy(d_s, h_s_avg.data(), M * sizeof(double), cudaMemcpyHostToDevice));
                        CHECK_CUDA(cudaMemcpy(d_y, h_y_avg.data(), M * sizeof(double), cudaMemcpyHostToDevice));
                        p_obj_tmp = avg_p_obj;
                        d_obj_tmp = avg_d_obj;
                        gap = avg_gap;
                        // SpMV will be recalculated next iteration, which will correct A_trans_y
                    }
                    
                    CHECK_CUDA(cudaMemset(d_x_sum, 0, N * sizeof(double)));
                    CHECK_CUDA(cudaMemset(d_s_sum, 0, M * sizeof(double)));
                    CHECK_CUDA(cudaMemset(d_y_sum, 0, M * sizeof(double)));
                    weight_sum = 0.0;
                    
                    gap_at_last_restart = gap;
                    last_restart_iter = iter;
                    
                    CHECK_CUDA(cudaMemcpy(d_x_prev, d_x, N * sizeof(double), cudaMemcpyDeviceToDevice));
                    CHECK_CUDA(cudaMemcpy(d_s_prev, d_s, M * sizeof(double), cudaMemcpyDeviceToDevice));
                    
                    CHECK_CUDA(cudaMemcpy(h_x_at_last_restart.data(), d_x, N * sizeof(double), cudaMemcpyDeviceToHost));
                    CHECK_CUDA(cudaMemcpy(h_y_at_last_restart.data(), d_y, M * sizeof(double), cudaMemcpyDeviceToHost));
                }
            }
            
            if (options.iteration_callback) {
                auto now = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
                options.iteration_callback(iter, p_obj_tmp + pre.postsolve.objective_offset, d_obj_tmp + pre.postsolve.objective_offset, ms);
            }
            
            if (cur_gap < options.tolerance) {
                break;
            }
        }
    }
    cudaDeviceSynchronize();

    std::vector<double> h_x(N, 0.0);
    CHECK_CUDA(cudaMemcpy(h_x.data(), d_x, N * sizeof(double), cudaMemcpyDeviceToHost));
    
    std::vector<double> h_y(M, 0.0);
    CHECK_CUDA(cudaMemcpy(h_y.data(), d_y, M * sizeof(double), cudaMemcpyDeviceToHost));
    
    // Free resources
    if (dBuffer) cudaFree(dBuffer);
    cusparseDestroyDnVec(vecY);
    cusparseDestroyDnVec(vecA_trans_y);
    cusparseDestroyDnVec(vecDeltaX);
    cusparseDestroyDnVec(vecA_delta_x);
    cusparseDestroySpMat(matA);

    cudaFree(d_row_ptr); cudaFree(d_col_ind); cudaFree(d_val);
    cudaFree(d_csc_col_ptr); cudaFree(d_csc_row_ind);
    cudaFree(d_x); cudaFree(d_x_prev); cudaFree(d_s); cudaFree(d_s_prev); cudaFree(d_y);
    cudaFree(d_x_sum); cudaFree(d_s_sum); cudaFree(d_y_sum);
    cudaFree(d_L_x); cudaFree(d_U_x); cudaFree(d_L_s); cudaFree(d_U_s); cudaFree(d_c);
    cudaFree(d_A_trans_y); cudaFree(d_A_delta_x); cudaFree(d_delta_x);

    // Finalize
    double p_obj = 0.0;
    for (size_t j = 0; j < N; ++j) p_obj += pre.problem.objective[j] * h_x[j];
    
    if (gap <= options.tolerance) {
        result.status = SolveStatus::OPTIMAL;
    } else {
        result.status = SolveStatus::ITERATION_LIMIT;
        result.message = "Iteration limit reached without convergence. Final gap: " + std::to_string(gap);
    }
    result.objective_value = p_obj + pre.postsolve.objective_offset;
    result.primal_dual_gap = gap;
    result.phase1_iterations = iter;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    result.solve_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    result.primal_solution = Presolver::postsolve_primal(pre, h_x);
    result.dual_solution = Presolver::postsolve_dual(pre, h_y);

    return result;
}

} // namespace pdlp_cuda
} // namespace firefly
