#pragma once

#include <vector>
#include <string>

namespace firefly {

struct SparseProblem {
    int num_vars = 0;
    int num_constrs = 0;
    std::string name;

    // Objective
    std::vector<double> obj_coeffs;
    double obj_offset = 0.0;

    // Constraints matrix in CSR format
    std::vector<int> row_ptr;
    std::vector<int> col_idx;
    std::vector<double> values;

    // Row senses ('L' for <=, 'G' for >=, 'E' for ==)
    std::vector<char> row_senses;

    // RHS values
    std::vector<double> rhs;

    // Variable bounds
    std::vector<double> var_lower_bounds;
    std::vector<double> var_upper_bounds;

    // Integrality
    std::vector<bool> is_integer;
};

} // namespace firefly
