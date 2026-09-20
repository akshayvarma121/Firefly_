#include "Presolve.hpp"
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <iostream>

namespace firefly {

std::vector<double> PostsolveMapping::reconstruct(const std::vector<double>& reduced_sol) const {
    std::vector<double> orig_sol(orig_num_vars, 0.0);
    
    // Map the reduced solution back to the original variables, applying the column scaling
    for (size_t i = 0; i < reduced_to_orig_vars.size(); ++i) {
        int orig_idx = reduced_to_orig_vars[i];
        orig_sol[orig_idx] = reduced_sol[i] * col_scaling[i];
    }
    
    // Fill in fixed variables
    for (const auto& [orig_idx, val] : fixed_vars) {
        orig_sol[orig_idx] = val;
    }
    
    return orig_sol;
}

std::pair<SparseProblem, PostsolveMapping> Presolve::apply(const SparseProblem& prob) {
    SparseProblem reduced = prob;
    PostsolveMapping mapping;
    mapping.orig_num_vars = prob.num_vars;

    std::vector<bool> row_removed(prob.num_constrs, false);
    std::vector<bool> col_removed(prob.num_vars, false);

    bool changed = true;
    while (changed) {
        changed = false;

        // 1. Identify empty and singleton rows
        for (int r = 0; r < reduced.num_constrs; ++r) {
            if (row_removed[r]) continue;

            int nnz = 0;
            int last_c = -1;
            double last_v = 0.0;

            for (int ptr = reduced.row_ptr[r]; ptr < reduced.row_ptr[r+1]; ++ptr) {
                int c = reduced.col_idx[ptr];
                if (!col_removed[c]) {
                    nnz++;
                    last_c = c;
                    last_v = reduced.values[ptr];
                }
            }

            if (nnz == 0) {
                // Empty row. Check feasibility.
                double b = reduced.rhs[r];
                char sense = reduced.row_senses[r];
                bool feasible = true;
                if (sense == 'E' && std::abs(b) > 1e-9) feasible = false;
                if (sense == 'L' && 0.0 > b + 1e-9) feasible = false;
                if (sense == 'G' && 0.0 < b - 1e-9) feasible = false;
                
                if (!feasible) {
                    throw std::runtime_error("Infeasible problem: empty row violated");
                }
                
                row_removed[r] = true;
                changed = true;
            } else if (nnz == 1) {
                // Singleton row tightening
                double b = reduced.rhs[r];
                char sense = reduced.row_senses[r];
                double a = last_v;
                int c = last_c;

                double bound_val = b / a;
                if (a > 0) {
                    if (sense == 'L' || sense == 'E') {
                        reduced.var_upper_bounds[c] = std::min(reduced.var_upper_bounds[c], bound_val);
                    }
                    if (sense == 'G' || sense == 'E') {
                        reduced.var_lower_bounds[c] = std::max(reduced.var_lower_bounds[c], bound_val);
                    }
                } else {
                    if (sense == 'L' || sense == 'E') {
                        reduced.var_lower_bounds[c] = std::max(reduced.var_lower_bounds[c], bound_val);
                    }
                    if (sense == 'G' || sense == 'E') {
                        reduced.var_upper_bounds[c] = std::min(reduced.var_upper_bounds[c], bound_val);
                    }
                }

                if (reduced.var_lower_bounds[c] > reduced.var_upper_bounds[c] + 1e-9) {
                    throw std::runtime_error("Infeasible problem: conflicting bounds");
                }

                row_removed[r] = true;
                changed = true;
            }
        }

        // 2. Fixed-variable substitution
        for (int c = 0; c < reduced.num_vars; ++c) {
            if (col_removed[c]) continue;
            
            if (std::abs(reduced.var_upper_bounds[c] - reduced.var_lower_bounds[c]) < 1e-9) {
                double fixed_val = reduced.var_lower_bounds[c];
                mapping.fixed_vars[c] = fixed_val;
                col_removed[c] = true;
                changed = true;

                // Update objective offset
                reduced.obj_offset += fixed_val * reduced.obj_coeffs[c];

                // Update RHS for all rows
                for (int r = 0; r < reduced.num_constrs; ++r) {
                    if (row_removed[r]) continue;
                    for (int ptr = reduced.row_ptr[r]; ptr < reduced.row_ptr[r+1]; ++ptr) {
                        if (reduced.col_idx[ptr] == c) {
                            reduced.rhs[r] -= reduced.values[ptr] * fixed_val;
                            break; // Assuming unique column indices per row
                        }
                    }
                }
            }
        }
    }

    // Matrix Reconstruction
    SparseProblem final_prob;
    final_prob.name = prob.name + "_presolved";
    final_prob.obj_offset = reduced.obj_offset;

    std::vector<int> old_to_new_col(prob.num_vars, -1);
    for (int c = 0; c < prob.num_vars; ++c) {
        if (!col_removed[c]) {
            old_to_new_col[c] = mapping.reduced_to_orig_vars.size();
            mapping.reduced_to_orig_vars.push_back(c);
            final_prob.obj_coeffs.push_back(reduced.obj_coeffs[c]);
            final_prob.var_lower_bounds.push_back(reduced.var_lower_bounds[c]);
            final_prob.var_upper_bounds.push_back(reduced.var_upper_bounds[c]);
        }
    }

    final_prob.num_vars = mapping.reduced_to_orig_vars.size();
    final_prob.row_ptr.push_back(0);

    for (int r = 0; r < prob.num_constrs; ++r) {
        if (!row_removed[r]) {
            for (int ptr = reduced.row_ptr[r]; ptr < reduced.row_ptr[r+1]; ++ptr) {
                int c = reduced.col_idx[ptr];
                if (!col_removed[c]) {
                    final_prob.col_idx.push_back(old_to_new_col[c]);
                    final_prob.values.push_back(reduced.values[ptr]);
                }
            }
            final_prob.row_ptr.push_back(final_prob.values.size());
            final_prob.rhs.push_back(reduced.rhs[r]);
            final_prob.row_senses.push_back(reduced.row_senses[r]);
        }
    }
    final_prob.num_constrs = final_prob.row_ptr.size() - 1;

    // Ruiz Equilibration (Coefficient Scaling)
    int max_ruiz_iters = 5;
    mapping.col_scaling.assign(final_prob.num_vars, 1.0);
    std::vector<double> row_scaling(final_prob.num_constrs, 1.0);

    for (int iter = 0; iter < max_ruiz_iters; ++iter) {
        std::vector<double> row_max(final_prob.num_constrs, 0.0);
        std::vector<double> col_max(final_prob.num_vars, 0.0);

        for (int r = 0; r < final_prob.num_constrs; ++r) {
            for (int ptr = final_prob.row_ptr[r]; ptr < final_prob.row_ptr[r+1]; ++ptr) {
                int c = final_prob.col_idx[ptr];
                double v = std::abs(final_prob.values[ptr]);
                row_max[r] = std::max(row_max[r], v);
                col_max[c] = std::max(col_max[c], v);
            }
        }

        // Apply
        for (int r = 0; r < final_prob.num_constrs; ++r) {
            double rs = row_max[r] > 1e-12 ? 1.0 / std::sqrt(row_max[r]) : 1.0;
            row_scaling[r] *= rs;
            final_prob.rhs[r] *= rs;
        }

        for (int c = 0; c < final_prob.num_vars; ++c) {
            double cs = col_max[c] > 1e-12 ? 1.0 / std::sqrt(col_max[c]) : 1.0;
            mapping.col_scaling[c] *= cs;
            final_prob.obj_coeffs[c] *= cs;
            if (!std::isinf(final_prob.var_lower_bounds[c])) final_prob.var_lower_bounds[c] /= cs;
            if (!std::isinf(final_prob.var_upper_bounds[c])) final_prob.var_upper_bounds[c] /= cs;
        }

        for (int r = 0; r < final_prob.num_constrs; ++r) {
            for (int ptr = final_prob.row_ptr[r]; ptr < final_prob.row_ptr[r+1]; ++ptr) {
                int c = final_prob.col_idx[ptr];
                // Multiply the matrix value by both row scale factor and col scale factor applied this iter
                double rs = row_max[r] > 1e-12 ? 1.0 / std::sqrt(row_max[r]) : 1.0;
                double cs = col_max[c] > 1e-12 ? 1.0 / std::sqrt(col_max[c]) : 1.0;
                final_prob.values[ptr] *= (rs * cs);
            }
        }
    }

    return {final_prob, mapping};
}

} // namespace firefly
