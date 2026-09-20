#include "BranchAndBoundSolver.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace firefly;

void assert_double_eq(double a, double b, double tol = 1e-3) {
    if (std::abs(a - b) >= tol) {
        std::cerr << "Mismatch: " << a << " vs " << b << "\n";
        std::abort();
    }
}

void run_test(SparseProblem& p, double expected_obj, bool best_first, bool use_gomory) {
    BnBParams params;
    params.depth_first = !best_first;
    params.root_gomory_cuts = use_gomory;
    
    auto res = BranchAndBoundSolver::solve(p, params);
    
    if (std::isnan(expected_obj)) {
        assert(res.status != BnBStatus::OPTIMAL);
    } else {
        assert(res.status == BnBStatus::OPTIMAL);
        assert_double_eq(res.objective_value, expected_obj);
    }
}

void test_bnb() {
    // Problem 1: Simple 2-variable integer knapsack
    // Maximize 5x + 8y subject to x + y <= 6, 5x + 9y <= 45, x, y >= 0 integer
    // -> min -5x - 8y
    {
        SparseProblem p;
        p.num_vars = 2;
        p.num_constrs = 2;
        p.obj_coeffs = {-5.0, -8.0};
        p.var_lower_bounds = {0.0, 0.0};
        p.var_upper_bounds = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
        p.is_integer = {true, true};
        
        p.row_ptr = {0, 2, 4};
        p.col_idx = {0, 1, 0, 1};
        p.values = {1.0, 1.0, 5.0, 9.0};
        p.rhs = {6.0, 45.0};
        p.row_senses = {'L', 'L'};

        // LP optimum: x = 0, y = 5, obj = -40
        // Oh wait, LP: x=0, y=5 is integral! No branching needed.
        // Let's modify to force fractional.
        // Maximize 5.5x + 2.1y -> min -5.5x - 2.1y
        // s.t. x + y <= 5.2 => x=5.2, y=0. obj=-28.6
        // IP opt: x=5, y=0, obj=-27.5
        p.obj_coeffs = {-5.5, -2.1};
        p.rhs = {5.2, 45.0};

        run_test(p, -27.5, true, false);
        run_test(p, -27.5, false, false);
    }

    // Problem 2: Fractional LP requiring branching
    // min -x - y
    // s.t. -2x + 2y >= 1 => 2x - 2y <= -1
    //      2x - 2y >= 1 => -2x + 2y <= -1
    // Wait, that's infeasible.
    // Let's use:
    // min -y
    // s.t. 3x + 2y <= 6
    //     -3x + 2y <= 0
    // x, y >= 0 integer
    // LP max y: intersection of 3x+2y=6 and -3x+2y=0 => 6x = 6 => x=1, 2y=3 => y=1.5
    // obj = -1.5
    // IP opt: x=1, y=1, obj = -1.0
    {
        SparseProblem p;
        p.num_vars = 2;
        p.num_constrs = 2;
        p.obj_coeffs = {0.0, -1.0};
        p.var_lower_bounds = {0.0, 0.0};
        p.var_upper_bounds = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
        p.is_integer = {true, true};
        
        p.row_ptr = {0, 2, 4};
        p.col_idx = {0, 1, 0, 1};
        p.values = {3.0, 2.0, -3.0, 2.0};
        p.rhs = {6.0, 0.0};
        p.row_senses = {'L', 'L'};

        run_test(p, -1.0, true, false);
        run_test(p, -1.0, false, false);
        
        // Test with Gomory cuts
        run_test(p, -1.0, true, true);
    }
}

int main() {
    std::cout << "Running Branch and Bound tests...\n";
    test_bnb();
    std::cout << "Branch and Bound tests passed!\n";
    return 0;
}