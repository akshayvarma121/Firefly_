#include "StandardForm.hpp"
#include <cmath>
#include <stdexcept>
#include <iostream>

namespace firefly {

std::vector<double> StandardizerMapping::reconstruct(const std::vector<double>& std_sol) const {
    std::vector<double> orig_sol(orig_num_vars, 0.0);
    for (int i = 0; i < orig_num_vars; ++i) {
        double val = orig_to_std[i].shift;
        if (orig_to_std[i].pos_idx != -1) val += std_sol[orig_to_std[i].pos_idx];
        if (orig_to_std[i].neg_idx != -1) val -= std_sol[orig_to_std[i].neg_idx];
        orig_sol[i] = val * orig_to_std[i].multiplier;
    }
    return orig_sol;
}

std::pair<StandardForm, StandardizerMapping> Standardizer::apply(const SparseProblem& prob) {
    StandardForm std_prob;
    StandardizerMapping mapping;
    mapping.orig_num_vars = prob.num_vars;
    mapping.orig_to_std.resize(prob.num_vars);

    std_prob.obj_offset = prob.obj_offset;

    int current_std_vars = 0;
    
    // 1. Process variables
    struct BoundConstr { int std_var_idx; double limit; };
    std::vector<BoundConstr> extra_constrs;

    for (int i = 0; i < prob.num_vars; ++i) {
        double l = prob.var_lower_bounds[i];
        double u = prob.var_upper_bounds[i];

        if (std::isinf(l) && std::signbit(l) && std::isinf(u)) {
            // Free variable
            mapping.orig_to_std[i].pos_idx = current_std_vars++;
            mapping.orig_to_std[i].neg_idx = current_std_vars++;
            std_prob.c.push_back(prob.obj_coeffs[i]);
            std_prob.c.push_back(-prob.obj_coeffs[i]);
        } else if (std::isinf(l) && std::signbit(l)) {
            // l = -inf, u = finite -> replace x with u - x'
            mapping.orig_to_std[i].pos_idx = current_std_vars++;
            mapping.orig_to_std[i].shift = u;
            mapping.orig_to_std[i].multiplier = -1.0;
            std_prob.c.push_back(-prob.obj_coeffs[i]);
            std_prob.obj_offset += u * prob.obj_coeffs[i];
        } else {
            // l is finite. Shift by l.
            mapping.orig_to_std[i].pos_idx = current_std_vars++;
            mapping.orig_to_std[i].shift = l;
            std_prob.c.push_back(prob.obj_coeffs[i]);
            std_prob.obj_offset += l * prob.obj_coeffs[i];

            if (!std::isinf(u)) {
                // Add upper bound constraint: x' + s = u - l
                extra_constrs.push_back({mapping.orig_to_std[i].pos_idx, u - l});
            }
        }
    }

    int orig_constrs = prob.num_constrs;
    int total_constrs = orig_constrs + extra_constrs.size();
    std_prob.num_constrs = total_constrs;
    std_prob.b.resize(total_constrs, 0.0);

    // To build CSR, we must do it row by row.
    std_prob.row_ptr.push_back(0);

    int current_slack_var = current_std_vars;

    // First, process original constraints
    for (int r = 0; r < orig_constrs; ++r) {
        char sense = prob.row_senses[r];
        double rhs = prob.rhs[r];
        
        // Shift RHS due to variable lower bounds
        for (int ptr = prob.row_ptr[r]; ptr < prob.row_ptr[r+1]; ++ptr) {
            int c = prob.col_idx[ptr];
            double v = prob.values[ptr];
            double shift = mapping.orig_to_std[c].shift;
            double mult = mapping.orig_to_std[c].multiplier;
            rhs -= v * shift * mult;
        }

        // If RHS is negative, flip the constraint
        double row_mult = 1.0;
        if (rhs < 0) {
            row_mult = -1.0;
            rhs = -rhs;
            if (sense == 'L') sense = 'G';
            else if (sense == 'G') sense = 'L';
        }
        std_prob.b[r] = rhs;

        // Add variables for this row
        for (int ptr = prob.row_ptr[r]; ptr < prob.row_ptr[r+1]; ++ptr) {
            int c = prob.col_idx[ptr];
            double v = prob.values[ptr] * row_mult * mapping.orig_to_std[c].multiplier;
            if (std::abs(v) > 1e-12) {
                std_prob.col_idx.push_back(mapping.orig_to_std[c].pos_idx);
                std_prob.values.push_back(v);
                
                if (mapping.orig_to_std[c].neg_idx != -1) {
                    std_prob.col_idx.push_back(mapping.orig_to_std[c].neg_idx);
                    std_prob.values.push_back(-v);
                }
            }
        }

        // Add slack/surplus
        if (sense == 'L') {
            std_prob.col_idx.push_back(current_slack_var++);
            std_prob.values.push_back(1.0);
        } else if (sense == 'G') {
            std_prob.col_idx.push_back(current_slack_var++);
            std_prob.values.push_back(-1.0);
        }

        std_prob.row_ptr.push_back(std_prob.values.size());
    }

    // Process extra constraints for bounds
    for (int r = 0; r < extra_constrs.size(); ++r) {
        int std_var = extra_constrs[r].std_var_idx;
        double limit = extra_constrs[r].limit;

        std_prob.b[orig_constrs + r] = limit;
        
        std_prob.col_idx.push_back(std_var);
        std_prob.values.push_back(1.0);
        
        std_prob.col_idx.push_back(current_slack_var++);
        std_prob.values.push_back(1.0);

        std_prob.row_ptr.push_back(std_prob.values.size());
    }

    std_prob.num_vars = current_slack_var;
    std_prob.c.resize(std_prob.num_vars, 0.0);

    return {std_prob, mapping};
}

} // namespace firefly
