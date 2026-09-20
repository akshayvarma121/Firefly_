#pragma once

#include "SparseProblem.hpp"
#include <vector>
#include <unordered_map>
#include <utility>

namespace firefly {

struct PostsolveMapping {
    int orig_num_vars;
    // Maps reduced col index to orig col index
    std::vector<int> reduced_to_orig_vars;
    // Map of orig col index -> fixed value
    std::unordered_map<int, double> fixed_vars;
    // Scaling factors applied to columns
    std::vector<double> col_scaling;

    // Reconstructs the full original variable array from the reduced solved array
    std::vector<double> reconstruct(const std::vector<double>& reduced_sol) const;
};

class Presolve {
public:
    // Returns the reduced problem and the mapping to reconstruct the solution
    static std::pair<SparseProblem, PostsolveMapping> apply(const SparseProblem& prob);
};

} // namespace firefly
