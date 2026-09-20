#include <iostream>
#include <cassert>
#include "firefly/core.h"

void test_core_version() {
    assert(firefly::get_core_version() == 1);
    std::cout << "test_core_version passed!" << std::endl;
}

#include <stdexcept>

void run_all_mps_tests();

int main() {
    std::cout << "Running Firefly minimal test runner..." << std::endl;
    
    try {
        test_core_version();
        run_all_mps_tests();
    } catch (const std::exception& e) {
        std::cerr << "Unhandled Exception: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "All tests passed successfully." << std::endl;
    return 0;
}
