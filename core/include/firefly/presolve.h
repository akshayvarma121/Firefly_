#pragma once

#include "firefly/mps_parser.h"
#include <vector>
#include <unordered_map>
#include <stdexcept>

namespace firefly {

class InfeasibleProblemException : public std::runtime_error {
public:
    explicit InfeasibleProblemException(const std::string& message)
        : std::runtime_error("Infeasible Problem: " + message) {}
};

struct PresolveStats {
    size_t original_rows = 0;
    size_t original_cols = 0;
    size_t reduced_rows = 0;
    size_t reduced_cols = 0;
    
    size_t rows_removed = 0;
    size_t bounds_tightened = 0;
    size_t variables_fixed = 0;
    size_t scaling_applied = 0; // Number of variables/constraints scaled
};

struct PostsolveMap {
    // Map reduced column index back to original column index
    std::vector<size_t> reduced_col_to_orig_col;
    
    // Map reduced row index back to original row index
    std::vector<size_t> reduced_row_to_orig_row;
    
    // Map original column index to its fixed value (for removed variables)
    std::unordered_map<size_t, double> fixed_variables;
    
    // Column scaling factors applied during equilibration.
    // Reconstruct primal values: orig_val = reduced_val * col_scale_factors[orig_col]
    std::vector<double> col_scale_factors; 
    
    // Row scaling factors applied during equilibration.
    // Reconstruct dual values if needed later: orig_dual = reduced_dual / row_scale_factors[orig_row]
    std::vector<double> row_scale_factors;
    
    // Constant shift to the objective value caused by fixing variables
    double objective_offset = 0.0;
};

struct PresolvedProblem {
    Problem problem;          // The reduced problem
    PresolveStats stats;      // Tracking numbers
    PostsolveMap postsolve;   // Data needed to reconstruct original solution
};

struct PresolveOptions {
    bool enable_empty_row_removal = true;
    bool enable_empty_col_removal = true;
    bool enable_fixed_variable_substitution = true;
    bool enable_singleton_row_tightening = true;
    bool enable_scaling = true;
};

class Presolver {
public:
    // Apply presolve reductions to the original problem using the given options.
    // Throws InfeasibleProblemException if trivially infeasible bounds are detected.
    static PresolvedProblem presolve(const Problem& orig, const PresolveOptions& options = PresolveOptions());
    
    // Reconstruct the original primal solution from the reduced primal solution
    static std::vector<double> postsolve_primal(const PresolvedProblem& pre, const std::vector<double>& reduced_primal);
    // Reconstruct the original dual solution from the reduced dual solution
    static std::vector<double> postsolve_dual(const PresolvedProblem& pre, const std::vector<double>& reduced_dual);
};

} // namespace firefly
