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
    
    CHECK_CUDA(cudaMalloc(&d_x, N * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_x_prev, N * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_s, M * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_s_prev, M * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_y, M * sizeof(double)));
    CHECK_CUDA(cudaMemset(d_x, 0, N * sizeof(double)));
    CHECK_CUDA(cudaMemset(d_x_prev, 0, N * sizeof(double)));
    CHECK_CUDA(cudaMemset(d_s, 0, M * sizeof(double)));
    CHECK_CUDA(cudaMemset(d_s_prev, 0, M * sizeof(double)));
    CHECK_CUDA(cudaMemset(d_y, 0, M * sizeof(double)));

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
    
    double tau = 1e-3;
    double sigma = 1e-3;
    if (L_norm > 1e-12) {
        tau = 0.99 / L_norm;
        sigma = 0.99 / L_norm;
    }

    int blockSize = 256;
    int gridN = (N + blockSize - 1) / blockSize;
    int gridM = (M + blockSize - 1) / blockSize;

    size_t iter = 0;
    double gap = 1e9;
    double best_gap = gap;
    
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

        // Evaluate and Callback
        if (iter % options.callback_frequency == 0 || iter == options.max_iterations - 1) {
            std::vector<double> h_x_tmp(N);
            std::vector<double> h_s_tmp(M);
            std::vector<double> h_y_tmp(M);
            CHECK_CUDA(cudaMemcpy(h_x_tmp.data(), d_x, N * sizeof(double), cudaMemcpyDeviceToHost));
            CHECK_CUDA(cudaMemcpy(h_s_tmp.data(), d_s, M * sizeof(double), cudaMemcpyDeviceToHost));
            CHECK_CUDA(cudaMemcpy(h_y_tmp.data(), d_y, M * sizeof(double), cudaMemcpyDeviceToHost));
            
            double p_obj_tmp = 0.0;
            for (size_t j = 0; j < N; ++j) p_obj_tmp += pre.problem.objective[j] * h_x_tmp[j];
            
            // Dual objective approximation
            double d_obj_tmp = 0.0;
            for (size_t i = 0; i < M; ++i) {
                double y_proj = h_y_tmp[i];
                if (std::isinf(pre.problem.row_lower_bounds[i]) && pre.problem.row_lower_bounds[i] < 0 && y_proj > 0) y_proj = 0.0;
                if (std::isinf(pre.problem.row_upper_bounds[i]) && pre.problem.row_upper_bounds[i] > 0 && y_proj < 0) y_proj = 0.0;
                if (y_proj > 0) d_obj_tmp += pre.problem.row_lower_bounds[i] * y_proj;
                else if (y_proj < 0) d_obj_tmp += pre.problem.row_upper_bounds[i] * y_proj;
            }
            std::vector<double> h_A_trans_y_tmp(N);
            CHECK_CUDA(cudaMemcpy(h_A_trans_y_tmp.data(), d_A_trans_y, N * sizeof(double), cudaMemcpyDeviceToHost));
            for (size_t j = 0; j < N; ++j) {
                double rc = pre.problem.objective[j] - h_A_trans_y_tmp[j];
                double rc_proj = rc;
                if (std::isinf(pre.problem.col_lower_bounds[j]) && pre.problem.col_lower_bounds[j] < 0 && rc_proj > 0) rc_proj = 0.0;
                if (std::isinf(pre.problem.col_upper_bounds[j]) && pre.problem.col_upper_bounds[j] > 0 && rc_proj < 0) rc_proj = 0.0;
                if (rc_proj > 0) d_obj_tmp += pre.problem.col_lower_bounds[j] * rc_proj;
                else if (rc_proj < 0) d_obj_tmp += pre.problem.col_upper_bounds[j] * rc_proj;
            }
            
            double cur_gap = std::abs(p_obj_tmp - d_obj_tmp); 
            gap = cur_gap;

            // Restarts (Adaptive)
            if (options.enable_restarts) {
                // Adaptive step sizes removed for constant optimal step size testing
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
    cudaFree(d_L_x); cudaFree(d_U_x); cudaFree(d_L_s); cudaFree(d_U_s); cudaFree(d_c);
    cudaFree(d_A_trans_y); cudaFree(d_A_delta_x); cudaFree(d_delta_x);

    // Finalize
    double p_obj = 0.0;
    for (size_t j = 0; j < N; ++j) p_obj += pre.problem.objective[j] * h_x[j];
    
    if (gap <= options.tolerance) {
        result.status = SolveStatus::OPTIMAL;
    } else {
        result.status = SolveStatus::ERROR;
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
