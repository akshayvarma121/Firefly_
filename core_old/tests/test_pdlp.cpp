#include "PDLPSolver.hpp"
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

void run_test(SparseProblem& p, double expected_obj, bool force_cpu) {
    PDLPSolverParams params;
    params.force_cpu = force_cpu;
    params.max_iterations = 200000;
    params.tolerance = 1e-4;
    params.report_frequency = 100000;
    
    auto res = PDLPSolver::solve(p, params);
    
    if (std::isnan(expected_obj)) {
        assert(res.status != PDLPStatus::OPTIMAL);
    } else {
        assert(res.status == PDLPStatus::OPTIMAL);
        assert_double_eq(res.objective_value, expected_obj);
    }
}

void test_pdlp(bool force_cpu) {
    // Problem 1: Standard Optimal
    {
        SparseProblem p;
        p.num_vars = 2;
        p.num_constrs = 3;
        p.obj_coeffs = {-3.0, -4.0};
        p.var_lower_bounds = {0.0, 0.0};
        p.var_upper_bounds = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
        
        p.row_ptr = {0, 2, 4, 6};
        p.col_idx = {0, 1, 0, 1, 0, 1};
        p.values = {1.0, 2.0, 3.0, -1.0, 1.0, -1.0};
        p.rhs = {14.0, 0.0, 2.0};
        p.row_senses = {'L', 'G', 'L'};

        run_test(p, -34.0, force_cpu);
    }

    // Problem 2: Infeasible
    {
        SparseProblem p;
        p.num_vars = 2;
        p.num_constrs = 2;
        p.obj_coeffs = {1.0, 1.0};
        p.var_lower_bounds = {0.0, 0.0};
        p.var_upper_bounds = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
        
        p.row_ptr = {0, 2, 4};
        p.col_idx = {0, 1, 0, 1};
        p.values = {1.0, 1.0, 1.0, 1.0};
        p.rhs = {5.0, 10.0};
        p.row_senses = {'L', 'G'};

        run_test(p, NAN, force_cpu);
    }

    // Problem 3: Unbounded
    {
        SparseProblem p;
        p.num_vars = 2;
        p.num_constrs = 1;
        p.obj_coeffs = {-1.0, -1.0};
        p.var_lower_bounds = {0.0, 0.0};
        p.var_upper_bounds = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
        
        p.row_ptr = {0, 2};
        p.col_idx = {0, 1};
        p.values = {1.0, -1.0};
        p.rhs = {5.0};
        p.row_senses = {'L'};

        run_test(p, NAN, force_cpu);
    }

    // Problem 4: Negative bounds & upper bounds
    {
        SparseProblem p;
        p.num_vars = 2;
        p.num_constrs = 1;
        p.obj_coeffs = {-1.0, 0.0};
        p.var_lower_bounds = {-5.0, 0.0};
        p.var_upper_bounds = {10.0, std::numeric_limits<double>::infinity()};
        
        p.row_ptr = {0, 2};
        p.col_idx = {0, 1};
        p.values = {1.0, 1.0};
        p.rhs = {5.0};
        p.row_senses = {'E'};

        run_test(p, -5.0, force_cpu);
    }

    // Problem 5: Degenerate (Beale's)
    {
        SparseProblem p;
        p.num_vars = 4;
        p.num_constrs = 3;
        p.obj_coeffs = {-0.75, 20.0, -0.5, 6.0};
        p.var_lower_bounds = {0.0, 0.0, 0.0, 0.0};
        p.var_upper_bounds = {
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity()
        };
        
        p.row_ptr = {0, 4, 8, 9};
        p.col_idx = {0, 1, 2, 3,  0, 1, 2, 3,  2};
        p.values = {0.25, -8.0, -1.0, 9.0,   0.5, -12.0, -0.5, 3.0,   1.0};
        p.rhs = {0.0, 0.0, 1.0};
        p.row_senses = {'L', 'L', 'L'};

        run_test(p, -1.25, force_cpu);
    }
}

int main() {
    std::cout << "Running CPU PDLP tests...\n";
    test_pdlp(true);
    std::cout << "CPU PDLP tests passed!\n";
    
#ifdef FIREFLY_USE_CUDA
    std::cout << "Running CUDA PDLP tests...\n";
    test_pdlp(false);
    std::cout << "CUDA PDLP tests passed!\n";
#endif
    
    return 0;
}
