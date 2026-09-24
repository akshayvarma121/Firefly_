#pragma once

#include <iostream>
#include <cstdlib>

#define FIREFLY_TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "Assertion failed: " << #expr \
                      << ", file " << __FILE__ \
                      << ", line " << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (false)
