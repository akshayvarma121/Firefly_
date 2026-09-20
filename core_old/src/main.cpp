#include <iostream>

int main() {
    std::cout << "Firefly core alive\n";
    
#ifdef FIREFLY_USE_CUDA
    std::cout << "[INFO] CUDA mode is ENABLED (Targeting sm_89).\n";
#else
    std::cout << "[INFO] CUDA mode is DISABLED. Running in CPU-only mode.\n";
#endif

    return 0;
}
