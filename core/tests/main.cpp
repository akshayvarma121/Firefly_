#include <iostream>
#include "test_utils.h"
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include "firefly/core.h"

void test_core_version() {
    FIREFLY_TEST_ASSERT(firefly::get_core_version() == 1);
    std::cout << "test_core_version passed!" << std::endl;
}

#include <stdexcept>

void run_mps_test_by_name(const std::string& name);

int main(int argc, char* argv[]) {
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::string test_name = "all";
    if (argc >= 3 && std::string(argv[1]) == "--test") {
        test_name = argv[2];
    }

    std::cout << "Running Firefly minimal test runner... (test: " << test_name << ")" << std::endl;
    
    try {
        if (test_name == "all") {
            test_core_version();
        }
        run_mps_test_by_name(test_name);
    } catch (const std::exception& e) {
        std::cerr << "Unhandled Exception: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "All tests passed successfully." << std::endl;
    return 0;
}
