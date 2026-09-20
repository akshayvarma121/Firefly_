#include "Presolve.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace firefly;

void assert_double_eq(double a, double b) {
    if (std::isinf(a) && std::isinf(b)) {
        if (std::signbit(a) != std::signbit(b)) {
            std::cerr << "Mismatch inf: " << a << " vs " << b << "\n";
            std::abort();
        }
    } else {
        if (std::abs(a - b) >= 1e-6) {
            std::cerr << "Mismatch: " << a << " vs " << b << "\n";
            std::abort();
        }
    }
}

// A simple algebraic solver for dense, tiny matrices for testing purposes
std::vector<double> dummy_solve(const SparseProblem& prob) {
    // If the problem is empty or 1x1, solve it trivially.
    // For our specific test case, we know it's gonna be 1 variable after presolve
    assert(prob.num_vars == 1);
    assert(prob.num_constrs <= 1);
    
    // We just want to find a feasible solution for testing the mapping.
    // Let's set the variable to its lower bound.
    std::vector<double> sol(prob.num_vars, 0.0);
    if (!std::isinf(prob.var_lower_bounds[0])) {
        sol[0] = prob.var_lower_bounds[0];
    }
    return sol;
}

void test_presolve() {
    SparseProblem prob;
    prob.num_vars = 3;
    prob.num_constrs = 3;
    
    // Variables: x0, x1, x2
    // Constraints:
    // C0: x0 = 5  (singleton, fixes x0)
    // C1: 0 * x0 + 0 * x1 + 0 * x2 <= 0 (empty row)
    // C2: 2 * x0 + x1 + 1000 * x2 = 12 
    
    prob.row_ptr = {0, 1, 1, 4};
    prob.col_idx = {0, 0, 1, 2};
    prob.values = {1.0, 2.0, 1.0, 1000.0};
    
    prob.rhs = {5.0, 0.0, 12.0};
    prob.row_senses = {'E', 'L', 'E'};
    
    prob.obj_coeffs = {1.0, 2.0, 3.0};
    prob.var_lower_bounds = {0.0, 0.0, 0.0};
    prob.var_upper_bounds = {10.0, 10.0, 10.0};

    auto [reduced, mapping] = Presolve::apply(prob);

    // After C0 tightening, x0 is fixed to 5.
    // C0 and C1 are removed.
    // C2 becomes: x1 + 1000 * x2 = 12 - 2(5) = 2.
    // Reduced variables: x1, x2 (indices 0, 1). 
    // Wait, since C2 is an equality and might be processed again... 
    // Wait, the loop runs until no changes. 
    // Is C2 a singleton after x0 is removed? No, it has x1 and x2.
    // So the reduced problem has 2 variables and 1 constraint.
    
    assert(reduced.num_vars == 2);
    assert(reduced.num_constrs == 1);
    assert(mapping.orig_num_vars == 3);
    assert(mapping.fixed_vars.at(0) == 5.0);
    assert(mapping.reduced_to_orig_vars[0] == 1);
    assert(mapping.reduced_to_orig_vars[1] == 2);

    // Let's create a fake solution for the reduced problem:
    // x1' = 2.0 / col_scale[0], x2' = 0.0
    // so x1 = 2.0, x2 = 0.0
    std::vector<double> reduced_sol = { 2.0 / mapping.col_scaling[0], 0.0 };
    std::vector<double> orig_sol = mapping.reconstruct(reduced_sol);

    // Original solution should be x0=5, x1=2, x2=0
    assert_double_eq(orig_sol[0], 5.0);
    assert_double_eq(orig_sol[1], 2.0);
    assert_double_eq(orig_sol[2], 0.0);
    
    // Check objective offset:
    // Fixed x0=5, obj_coeff=1.0 -> offset should be 5.0
    assert_double_eq(reduced.obj_offset, 5.0);
    
    // Check Ruiz Scaling on reduced problem
    double max_val = std::max(std::abs(reduced.values[0]), std::abs(reduced.values[1]));
    std::cout << "Ruiz scaling max val: " << max_val << " (v0: " << reduced.values[0] << ", v1: " << reduced.values[1] << ")\n";
}

int main() {
    std::cout << "Running Presolve tests...\n";
    test_presolve();
    std::cout << "test_presolve passed.\n";
    std::cout << "All Presolve tests passed!\n";
    return 0;
}
