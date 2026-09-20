#ifdef FIREFLY_USE_CUDA

#include "PDLPSolver.hpp"
#include "StandardForm.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

#include <cuda_runtime.h>
#include <cublas_v2.h>
#include <cusparse.h>

namespace firefly::internal {

struct CudaHandles {
    cublasHandle_t cublas = nullptr;
    cusparseHandle_t cusparse = nullptr;

    CudaHandles() {
        if (cublasCreate(&cublas) != CUBLAS_STATUS_SUCCESS) {
            std::cerr << "CUBLAS Error: Initialization failed" << std::endl;
        }
        if (cusparseCreate(&cusparse) != CUSPARSE_STATUS_SUCCESS) {
            std::cerr << "CUSPARSE Error: Initialization failed" << std::endl;
        }
    }

    ~CudaHandles() {
        if (cublas) cublasDestroy(cublas);
        if (cusparse) cusparseDestroy(cusparse);
    }
};

CudaHandles& get_cuda_handles() {
    static thread_local CudaHandles handles;
    return handles;
}

#define CHECK_CUDA(func)                                                       \
{                                                                              \
    cudaError_t status = (func);                                               \
    if (status != cudaSuccess) {                                               \
        std::cerr << "CUDA Error: " << cudaGetErrorString(status) << std::endl;\
        throw std::runtime_error("CUDA Error");                                \
    }                                                                          \
}

#define CHECK_CUBLAS(func)                                                     \
{                                                                              \
    cublasStatus_t status = (func);                                            \
    if (status != CUBLAS_STATUS_SUCCESS) {                                     \
        std::cerr << "CUBLAS Error: " << status << std::endl;                  \
        throw std::runtime_error("CUBLAS Error");                              \
    }                                                                          \
}

#define CHECK_CUSPARSE(func)                                                   \
{                                                                              \
    cusparseStatus_t status = (func);                                          \
    if (status != CUSPARSE_STATUS_SUCCESS) {                                   \
        std::cerr << "CUSPARSE Error: " << status << std::endl;                \
        throw std::runtime_error("CUSPARSE Error");                            \
    }                                                                          \
}

__global__ void primal_update_kernel(int n, double* x, const double* c, const double* ATy, double tau) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        double grad = c[i] - ATy[i];
        double new_x = x[i] - tau * grad;
        x[i] = new_x > 0.0 ? new_x : 0.0;
    }
}

__global__ void compute_x_ext_kernel(int n, double* x_ext, const double* x, const double* x_prev) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        x_ext[i] = 2.0 * x[i] - x_prev[i];
    }
}

__global__ void dual_update_kernel(int m, double* y, const double* Ax_ext, const double* b, double sigma) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < m) {
        y[i] = y[i] + sigma * (b[i] - Ax_ext[i]);
    }
}

__global__ void compute_dual_viol_kernel(int n, double* dual_viol, const double* c, const double* ATy) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        double res = c[i] - ATy[i];
        dual_viol[i] = res < 0.0 ? res : 0.0;
    }
}

__global__ void compute_primal_res_kernel(int m, double* primal_res, const double* Ax, const double* b) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < m) {
        primal_res[i] = Ax[i] - b[i];
    }
}

double estimate_norm_cuda(cublasHandle_t cublas, cusparseHandle_t cusparse, 
                          cusparseSpMatDescr_t matA, int m, int n, int iters=20) {
    double* d_x;
    double* d_y;
    CHECK_CUDA(cudaMalloc(&d_x, n * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_y, m * sizeof(double)));
    
    std::vector<double> h_x(n, 1.0 / std::sqrt(n));
    CHECK_CUDA(cudaMemcpy(d_x, h_x.data(), n * sizeof(double), cudaMemcpyHostToDevice));

    cusparseDnVecDescr_t vecX, vecY;
    CHECK_CUSPARSE(cusparseCreateDnVec(&vecX, n, d_x, CUDA_R_64F));
    CHECK_CUSPARSE(cusparseCreateDnVec(&vecY, m, d_y, CUDA_R_64F));

    double alpha = 1.0;
    double beta = 0.0;
    
    size_t bufferSize1 = 0, bufferSize2 = 0;
    void* dBuffer1 = nullptr;
    void* dBuffer2 = nullptr;

    CHECK_CUSPARSE(cusparseSpMV_bufferSize(cusparse, CUSPARSE_OPERATION_NON_TRANSPOSE,
                                           &alpha, matA, vecX, &beta, vecY, CUDA_R_64F,
                                           CUSPARSE_SPMV_ALG_DEFAULT, &bufferSize1));
    CHECK_CUDA(cudaMalloc(&dBuffer1, bufferSize1));
    
    CHECK_CUSPARSE(cusparseSpMV_bufferSize(cusparse, CUSPARSE_OPERATION_TRANSPOSE,
                                           &alpha, matA, vecY, &beta, vecX, CUDA_R_64F,
                                           CUSPARSE_SPMV_ALG_DEFAULT, &bufferSize2));
    CHECK_CUDA(cudaMalloc(&dBuffer2, bufferSize2));

    for (int i = 0; i < iters; ++i) {
        CHECK_CUSPARSE(cusparseSpMV(cusparse, CUSPARSE_OPERATION_NON_TRANSPOSE,
                                    &alpha, matA, vecX, &beta, vecY, CUDA_R_64F,
                                    CUSPARSE_SPMV_ALG_DEFAULT, dBuffer1));
        CHECK_CUSPARSE(cusparseSpMV(cusparse, CUSPARSE_OPERATION_TRANSPOSE,
                                    &alpha, matA, vecY, &beta, vecX, CUDA_R_64F,
                                    CUSPARSE_SPMV_ALG_DEFAULT, dBuffer2));
        
        double norm_x = 0;
        CHECK_CUBLAS(cublasDnrm2(cublas, n, d_x, 1, &norm_x));
        if (norm_x > 1e-12) {
            double scale = 1.0 / norm_x;
            CHECK_CUBLAS(cublasDscal(cublas, n, &scale, d_x, 1));
        }
    }
    
    CHECK_CUSPARSE(cusparseSpMV(cusparse, CUSPARSE_OPERATION_NON_TRANSPOSE,
                                &alpha, matA, vecX, &beta, vecY, CUDA_R_64F,
                                CUSPARSE_SPMV_ALG_DEFAULT, dBuffer1));
    double norm_y = 0;
    CHECK_CUBLAS(cublasDnrm2(cublas, m, d_y, 1, &norm_y));
    
    CHECK_CUDA(cudaFree(d_x));
    CHECK_CUDA(cudaFree(d_y));
    CHECK_CUDA(cudaFree(dBuffer1));
    CHECK_CUDA(cudaFree(dBuffer2));
    CHECK_CUSPARSE(cusparseDestroyDnVec(vecX));
    CHECK_CUSPARSE(cusparseDestroyDnVec(vecY));

    return norm_y;
}

PDLPResult pdlp_cuda_solve(const SparseProblem& prob, const PDLPSolverParams& params, PDLPCallback cb) {
    auto [std_prob, mapping] = Standardizer::apply(prob);

    int m = std_prob.num_constrs;
    int n = std_prob.num_vars;
    int nnz = std_prob.values.size();

    if (m == 0 || n == 0) return {PDLPStatus::INFEASIBLE, 0.0, {}, 0};

    int* d_row_ptr;
    int* d_col_idx;
    double* d_values;
    CHECK_CUDA(cudaMalloc(&d_row_ptr, (m + 1) * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&d_col_idx, nnz * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&d_values, nnz * sizeof(double)));

    CHECK_CUDA(cudaMemcpy(d_row_ptr, std_prob.row_ptr.data(), (m + 1) * sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_col_idx, std_prob.col_idx.data(), nnz * sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_values, std_prob.values.data(), nnz * sizeof(double), cudaMemcpyHostToDevice));

    double* d_b;
    double* d_c;
    CHECK_CUDA(cudaMalloc(&d_b, m * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_c, n * sizeof(double)));
    CHECK_CUDA(cudaMemcpy(d_b, std_prob.b.data(), m * sizeof(double), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_c, std_prob.c.data(), n * sizeof(double), cudaMemcpyHostToDevice));

    double* d_x;
    double* d_x_prev;
    double* d_x_ext;
    double* d_y;
    double* d_ATy;
    double* d_Ax_ext;
    double* d_primal_res;
    double* d_dual_viol;

    CHECK_CUDA(cudaMalloc(&d_x, n * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_x_prev, n * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_x_ext, n * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_y, m * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_ATy, n * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_Ax_ext, m * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_primal_res, m * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_dual_viol, n * sizeof(double)));

    CHECK_CUDA(cudaMemset(d_x, 0, n * sizeof(double)));
    CHECK_CUDA(cudaMemset(d_x_prev, 0, n * sizeof(double)));
    CHECK_CUDA(cudaMemset(d_y, 0, m * sizeof(double)));

    CudaHandles& handles = get_cuda_handles();
    cublasHandle_t cublas = handles.cublas;
    cusparseHandle_t cusparse = handles.cusparse;

    cusparseSpMatDescr_t matA;
    CHECK_CUSPARSE(cusparseCreateCsr(&matA, m, n, nnz, d_row_ptr, d_col_idx, d_values, 
                                     CUSPARSE_INDEX_32I, CUSPARSE_INDEX_32I, 
                                     CUSPARSE_INDEX_BASE_ZERO, CUDA_R_64F));
    
    cusparseDnVecDescr_t vecX, vecXExt, vecY, vecATy, vecAxExt;
    CHECK_CUSPARSE(cusparseCreateDnVec(&vecX, n, d_x, CUDA_R_64F));
    CHECK_CUSPARSE(cusparseCreateDnVec(&vecXExt, n, d_x_ext, CUDA_R_64F));
    CHECK_CUSPARSE(cusparseCreateDnVec(&vecY, m, d_y, CUDA_R_64F));
    CHECK_CUSPARSE(cusparseCreateDnVec(&vecATy, n, d_ATy, CUDA_R_64F));
    CHECK_CUSPARSE(cusparseCreateDnVec(&vecAxExt, m, d_Ax_ext, CUDA_R_64F));

    double alpha = 1.0;
    double beta = 0.0;
    
    size_t bufferATy = 0;
    CHECK_CUSPARSE(cusparseSpMV_bufferSize(cusparse, CUSPARSE_OPERATION_TRANSPOSE,
                                           &alpha, matA, vecY, &beta, vecATy, CUDA_R_64F,
                                           CUSPARSE_SPMV_ALG_DEFAULT, &bufferATy));
    
    size_t bufferAxExt = 0;
    CHECK_CUSPARSE(cusparseSpMV_bufferSize(cusparse, CUSPARSE_OPERATION_NON_TRANSPOSE,
                                           &alpha, matA, vecXExt, &beta, vecAxExt, CUDA_R_64F,
                                           CUSPARSE_SPMV_ALG_DEFAULT, &bufferAxExt));

    void* dBuffer = nullptr;
    CHECK_CUDA(cudaMalloc(&dBuffer, std::max(bufferATy, bufferAxExt)));

    double norm_A = estimate_norm_cuda(cublas, cusparse, matA, m, n);
    if (norm_A < 1e-12) norm_A = 1.0;

    double eta = 1.0 / norm_A;
    double omega = 1.0;
    double tau = eta * omega;
    double sigma = eta / omega;

    int blockSize = 256;
    int numBlocksN = (n + blockSize - 1) / blockSize;
    int numBlocksM = (m + blockSize - 1) / blockSize;

    double b_norm = 0;
    double c_norm = 0;
    CHECK_CUBLAS(cublasDnrm2(cublas, m, d_b, 1, &b_norm));
    CHECK_CUBLAS(cublasDnrm2(cublas, n, d_c, 1, &c_norm));

    auto cleanup = [&]() {
        cudaFree(d_row_ptr); cudaFree(d_col_idx); cudaFree(d_values);
        cudaFree(d_b); cudaFree(d_c); cudaFree(d_x); cudaFree(d_x_prev);
        cudaFree(d_x_ext); cudaFree(d_y); cudaFree(d_ATy); cudaFree(d_Ax_ext);
        cudaFree(d_primal_res); cudaFree(d_dual_viol); cudaFree(dBuffer);
        cusparseDestroyDnVec(vecX); cusparseDestroyDnVec(vecXExt); cusparseDestroyDnVec(vecY);
        cusparseDestroyDnVec(vecATy); cusparseDestroyDnVec(vecAxExt);
        cusparseDestroySpMat(matA);
    };

    for (int iter = 0; iter < params.max_iterations; ++iter) {
        CHECK_CUDA(cudaMemcpy(d_x_prev, d_x, n * sizeof(double), cudaMemcpyDeviceToDevice));
        
        CHECK_CUSPARSE(cusparseSpMV(cusparse, CUSPARSE_OPERATION_TRANSPOSE,
                                    &alpha, matA, vecY, &beta, vecATy, CUDA_R_64F,
                                    CUSPARSE_SPMV_ALG_DEFAULT, dBuffer));
        
        primal_update_kernel<<<numBlocksN, blockSize>>>(n, d_x, d_c, d_ATy, tau);
        
        compute_x_ext_kernel<<<numBlocksN, blockSize>>>(n, d_x_ext, d_x, d_x_prev);
        
        CHECK_CUSPARSE(cusparseSpMV(cusparse, CUSPARSE_OPERATION_NON_TRANSPOSE,
                                    &alpha, matA, vecXExt, &beta, vecAxExt, CUDA_R_64F,
                                    CUSPARSE_SPMV_ALG_DEFAULT, dBuffer));
        
        dual_update_kernel<<<numBlocksM, blockSize>>>(m, d_y, d_Ax_ext, d_b, sigma);

        if (iter % params.report_frequency == 0 || iter == params.max_iterations - 1) {
            double c_dot_x = 0;
            double b_dot_y = 0;
            CHECK_CUBLAS(cublasDdot(cublas, n, d_c, 1, d_x, 1, &c_dot_x));
            CHECK_CUBLAS(cublasDdot(cublas, m, d_b, 1, d_y, 1, &b_dot_y));
            
            double primal_obj = std_prob.obj_offset + c_dot_x;
            double dual_obj = std_prob.obj_offset + b_dot_y;

            CHECK_CUSPARSE(cusparseSpMV(cusparse, CUSPARSE_OPERATION_NON_TRANSPOSE,
                                        &alpha, matA, vecX, &beta, vecAxExt, CUDA_R_64F,
                                        CUSPARSE_SPMV_ALG_DEFAULT, dBuffer));
            compute_primal_res_kernel<<<numBlocksM, blockSize>>>(m, d_primal_res, d_Ax_ext, d_b);
            
            CHECK_CUSPARSE(cusparseSpMV(cusparse, CUSPARSE_OPERATION_TRANSPOSE,
                                        &alpha, matA, vecY, &beta, vecATy, CUDA_R_64F,
                                        CUSPARSE_SPMV_ALG_DEFAULT, dBuffer));
            compute_dual_viol_kernel<<<numBlocksN, blockSize>>>(n, d_dual_viol, d_c, d_ATy);

            double p_res_norm = 0;
            double d_viol_norm = 0;
            CHECK_CUBLAS(cublasDnrm2(cublas, m, d_primal_res, 1, &p_res_norm));
            CHECK_CUBLAS(cublasDnrm2(cublas, n, d_dual_viol, 1, &d_viol_norm));

            double p_err = p_res_norm / (1.0 + b_norm);
            double d_err = d_viol_norm / (1.0 + c_norm);
            double gap = std::abs(primal_obj - dual_obj) / (1.0 + std::abs(primal_obj) + std::abs(dual_obj));

            if (cb) cb(iter, primal_obj, dual_obj);

            if (p_err < params.tolerance && d_err < params.tolerance && gap < params.tolerance) {
                std::vector<double> std_sol(n);
                CHECK_CUDA(cudaMemcpy(std_sol.data(), d_x, n * sizeof(double), cudaMemcpyDeviceToHost));
                std::vector<double> orig_sol = mapping.reconstruct(std_sol);
                cleanup();
                return {PDLPStatus::OPTIMAL, primal_obj, orig_sol, iter};
            }
        }
    }

    std::vector<double> std_sol(n);
    CHECK_CUDA(cudaMemcpy(std_sol.data(), d_x, n * sizeof(double), cudaMemcpyDeviceToHost));
    std::vector<double> orig_sol = mapping.reconstruct(std_sol);
    
    double final_obj = std_prob.obj_offset;
    for (int i = 0; i < n; ++i) final_obj += std_prob.c[i] * std_sol[i];

    cleanup();
    return {PDLPStatus::MAX_ITERATIONS, final_obj, orig_sol, params.max_iterations};
}

} // namespace firefly::internal
#endif // FIREFLY_USE_CUDA
