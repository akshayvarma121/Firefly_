#pragma once

#ifdef FIREFLY_WITH_CUDA

#include <cublas_v2.h>
#include <cusparse.h>
#include <stdexcept>
#include <memory>
#include <iostream>
#include <mutex>

namespace firefly {

class CudaContext {
private:
    cublasHandle_t cublas_handle;
    cusparseHandle_t cusparse_handle;
    std::mutex ctx_mutex;

    CudaContext() {
        if (cublasCreate(&cublas_handle) != CUBLAS_STATUS_SUCCESS) {
            throw std::runtime_error("Failed to initialize cuBLAS");
        }
        if (cusparseCreate(&cusparse_handle) != CUSPARSE_STATUS_SUCCESS) {
            cublasDestroy(cublas_handle);
            throw std::runtime_error("Failed to initialize cuSPARSE");
        }
    }

    ~CudaContext() {
        cusparseDestroy(cusparse_handle);
        cublasDestroy(cublas_handle);
    }

    // Disable copy/move
    CudaContext(const CudaContext&) = delete;
    CudaContext& operator=(const CudaContext&) = delete;

public:
    static CudaContext& get_instance() {
        // static local ensures lazy initialization exactly once in a thread-safe manner (C++11 magic statics)
        static CudaContext instance;
        return instance;
    }

    cublasHandle_t get_cublas() const { return cublas_handle; }
    cusparseHandle_t get_cusparse() const { return cusparse_handle; }
    std::mutex& get_mutex() { return ctx_mutex; }
};

} // namespace firefly

#endif // FIREFLY_WITH_CUDA
