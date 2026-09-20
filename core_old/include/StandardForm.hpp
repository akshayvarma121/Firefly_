#pragma once

#include "SparseProblem.hpp"
#include <vector>

namespace firefly {

struct StandardForm {
    int num_vars;
    int num_constrs;
    std::vector<double> c; // Objective
    std::vector<int> row_ptr;
    std::vector<int> col_idx;
    std::vector<double> values;
    std::vector<double> b; // RHS (guaranteed >= 0)
    double obj_offset = 0.0;
};

struct StandardizerMapping {
    int orig_num_vars;
    struct VarMap {
        int pos_idx = -1;
        int neg_idx = -1;
        double shift = 0.0;
        double multiplier = 1.0;
    };
    std::vector<VarMap> orig_to_std;

    std::vector<double> reconstruct(const std::vector<double>& std_sol) const;
};

class Standardizer {
public:
    static std::pair<StandardForm, StandardizerMapping> apply(const SparseProblem& prob);
};

} // namespace firefly
