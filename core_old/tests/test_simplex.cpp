#include "SimplexSolver.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace firefly;

void assert_double_eq(double a, double b) {
    assert(std::abs(a - b) < 1e-5);
}

void test_simplex_1() {
    // Problem 1: Standard Optimal
    // Maximize 3x + 4y  => Minimize -3x - 4y
    // s.t. x + 2y <= 14
    //      3x - y >= 0
    //      x - y <= 2
    // x, y >= 0
    // Optimal: x = 6, y = 4, Obj = -34
    
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

    auto res = SimplexSolver::solve(p);
    assert(res.status == SimplexStatus::OPTIMAL);
    assert_double_eq(res.objective_value, -34.0);
    assert_double_eq(res.solution[0], 6.0);
    assert_double_eq(res.solution[1], 4.0);
}

void test_simplex_2() {
    // Problem 2: Infeasible
    // Min x + y
    // s.t. x + y <= 5
    //      x + y >= 10
    // x, y >= 0
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

    auto res = SimplexSolver::solve(p);
    assert(res.status == SimplexStatus::INFEASIBLE);
}

void test_simplex_3() {
    // Problem 3: Unbounded
    // Min -x - y
    // s.t. x - y <= 5
    // x, y >= 0
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

    auto res = SimplexSolver::solve(p);
    assert(res.status == SimplexStatus::UNBOUNDED);
}

void test_simplex_4() {
    // Problem 4: Negative bounds & upper bounds
    // Min -x
    // s.t. x + y = 5
    // -5 <= x <= 10
    // y >= 0
    // Optimal: x = 5 (since x+y=5 and y>=0), Obj = -5
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

    auto res = SimplexSolver::solve(p);
    assert(res.status == SimplexStatus::OPTIMAL);
    assert_double_eq(res.objective_value, -5.0);
    assert_double_eq(res.solution[0], 5.0);
    assert_double_eq(res.solution[1], 0.0);
}

void test_simplex_5() {
    // Problem 5: Degenerate (Bland's rule prevents cycling)
    // Famous Beale's cycling example:
    // Min -0.75x1 + 20x2 - 0.5x3 + 6x4
    // s.t. 0.25x1 - 8x2 - x3 + 9x4 <= 0
    //      0.5x1 - 12x2 - 0.5x3 + 3x4 <= 0
    //      x3 <= 1
    // all >= 0
    // Optimal: x1=1, x2=0, x3=1, x4=0, Obj = -1.25
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

    auto res = SimplexSolver::solve(p);
    assert(res.status == SimplexStatus::OPTIMAL);
    assert_double_eq(res.objective_value, -1.25);
}

int main() {
    std::cout << "Running SimplexSolver tests...\n";
    test_simplex_1();
    std::cout << "test_simplex_1 passed.\n";
    test_simplex_2();
    std::cout << "test_simplex_2 passed.\n";
    test_simplex_3();
    std::cout << "test_simplex_3 passed.\n";
    test_simplex_4();
    std::cout << "test_simplex_4 passed.\n";
    test_simplex_5();
    std::cout << "test_simplex_5 passed.\n";
    std::cout << "All SimplexSolver tests passed!\n";
    return 0;
}
