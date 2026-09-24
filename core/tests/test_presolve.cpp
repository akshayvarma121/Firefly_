#include "firefly/presolve.h"
#include <iostream>
#include "test_utils.h"
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include <cmath>

using namespace firefly;

void assert_double_eq(double a, double b) {
    if (std::isinf(a) && std::isinf(b) && (a > 0) == (b > 0)) return;
    FIREFLY_TEST_ASSERT(std::abs(a - b) < 1e-9);
}

void test_fixed_variable() {
    Problem orig;
    orig.name = "fixed_var_test";
    
    orig.col_names = {"X1", "X2"};
    orig.col_to_index = {{"X1", 0}, {"X2", 1}};
    orig.col_lower_bounds = {5.0, 0.0};
    orig.col_upper_bounds = {5.0, 10.0}; // X1 is fixed to 5
    orig.is_integer = {false, false};
    orig.objective = {1.0, 2.0};
    
    orig.row_names = {"C1"};
    orig.row_to_index = {{"C1", 0}};
    orig.row_lower_bounds = {10.0};
    orig.row_upper_bounds = {10.0};
    orig.row_senses = {'E'};
    
    // C1: 2*X1 + X2 = 10
    orig.matrix = {
        {0, 0, 2.0},
        {0, 1, 1.0}
    };
    
    auto result = Presolver::presolve(orig);
    
    // X1 is fixed -> C1 becomes X2 = 0 -> C1 tightened to X2 and removed -> X2 fixed to 0
    FIREFLY_TEST_ASSERT(result.stats.variables_fixed == 2);
    FIREFLY_TEST_ASSERT(result.stats.rows_removed == 1);
    FIREFLY_TEST_ASSERT(result.stats.bounds_tightened >= 1);
    
    FIREFLY_TEST_ASSERT(result.problem.col_names.size() == 0);
    FIREFLY_TEST_ASSERT(result.problem.row_names.size() == 0);
    
    assert_double_eq(result.postsolve.fixed_variables[0], 5.0);
    assert_double_eq(result.postsolve.fixed_variables[1], 0.0);
    
    std::cout << "test_fixed_variable passed.\n";
}

void test_scaling() {
    Problem orig;
    orig.name = "scaling_test";
    
    orig.col_names = {"X1", "X2"};
    orig.col_to_index = {{"X1", 0}, {"X2", 1}};
    orig.col_lower_bounds = {0.0, 0.0};
    orig.col_upper_bounds = {10.0, 10.0};
    orig.is_integer = {false, false};
    orig.objective = {500.0, 10.0};
    
    orig.row_names = {"C1", "C2"};
    orig.row_to_index = {{"C1", 0}, {"C2", 1}};
    orig.row_lower_bounds = {-INF, -INF};
    orig.row_upper_bounds = {100.0, 100.0};
    orig.row_senses = {'L', 'L'};
    
    // C1: 20 X1 + X2 <= 100
    // C2: X1 + 20 X2 <= 100
    orig.matrix = {
        {0, 0, 20.0}, {0, 1, 1.0},
        {1, 0, 1.0}, {1, 1, 20.0}
    };
    
    auto result = Presolver::presolve(orig);
    
    FIREFLY_TEST_ASSERT(result.stats.scaling_applied == 2); // Two rows scaled by 20
    assert_double_eq(result.problem.row_upper_bounds[0], 5.0);
    assert_double_eq(result.problem.row_upper_bounds[1], 5.0);
    
    // After row scaling, X1 has coeffs 1 and 0.05 (max 1) -> col scale 1.0 (ignored)
    // Same for X2. Col scale not applied since max is exactly 1.
    
    std::cout << "test_scaling passed.\n";
}

void test_infeasible() {
    Problem orig;
    orig.name = "inf_test";
    
    orig.col_names = {"X1"};
    orig.col_to_index = {{"X1", 0}};
    orig.col_lower_bounds = {10.0};
    orig.col_upper_bounds = {0.0}; // L > U
    orig.is_integer = {false};
    orig.objective = {1.0};
    
    orig.row_names = {"C1"};
    orig.row_to_index = {{"C1", 0}};
    orig.row_lower_bounds = {-INF};
    orig.row_upper_bounds = {10.0};
    orig.row_senses = {'L'};
    
    orig.matrix = { {0, 0, 1.0} };
    
    try {
        Presolver::presolve(orig);
        FIREFLY_TEST_ASSERT(false && "Should have thrown InfeasibleProblemException");
    } catch (const InfeasibleProblemException& e) {
        // Expected
    }
    
    std::cout << "test_infeasible passed.\n";
}

void test_redundant_row() {
    Problem orig;
    orig.name = "redundant_row_test";
    
    orig.col_names = {"X1"};
    orig.col_to_index = {{"X1", 0}};
    orig.col_lower_bounds = {0.0};
    orig.col_upper_bounds = {10.0};
    orig.is_integer = {false};
    orig.objective = {1.0};
    
    orig.row_names = {"C1"};
    orig.row_to_index = {{"C1", 0}};
    orig.row_lower_bounds = {-INF};
    orig.row_upper_bounds = {100.0};
    orig.row_senses = {'L'};
    
    // C1: X1 <= 100. Since X1 <= 10, this is redundant.
    orig.matrix = {
        {0, 0, 1.0}
    };
    
    PresolveOptions opts;
    auto result = Presolver::presolve(orig, opts);
    
    FIREFLY_TEST_ASSERT(result.stats.rows_removed == 1);
    std::cout << "test_redundant_row passed.\n";
}

void test_bound_tightening() {
    Problem orig;
    orig.name = "bound_tightening_test";
    
    orig.col_names = {"X1"};
    orig.col_to_index = {{"X1", 0}};
    orig.col_lower_bounds = {0.0};
    orig.col_upper_bounds = {100.0};
    orig.is_integer = {false};
    orig.objective = {1.0};
    
    orig.row_names = {"C1"};
    orig.row_to_index = {{"C1", 0}};
    orig.row_lower_bounds = {-INF};
    orig.row_upper_bounds = {20.0};
    orig.row_senses = {'L'};
    
    // C1: 2*X1 <= 20  => X1 <= 10
    orig.matrix = {
        {0, 0, 2.0}
    };
    
    PresolveOptions opts;
    auto result = Presolver::presolve(orig, opts);
    
    FIREFLY_TEST_ASSERT(result.stats.bounds_tightened >= 1);
    std::cout << "test_bound_tightening passed.\n";
}

int main(int argc, char* argv[]) {
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::string test_name = "all";
    if (argc >= 3 && std::string(argv[1]) == "--test") {
        test_name = argv[2];
    }
    
    std::cout << "Running Presolver tests... (test: " << test_name << ")\n";
    try {
        if (test_name == "fixed_variable") test_fixed_variable();
        else if (test_name == "scaling") test_scaling();
        else if (test_name == "infeasible") test_infeasible();
        else if (test_name == "redundant_row") test_redundant_row();
        else if (test_name == "bound_tightening") test_bound_tightening();
        else if (test_name == "all") {
            test_fixed_variable();
            test_scaling();
            test_infeasible();
            test_redundant_row();
            test_bound_tightening();
        } else {
            throw std::runtime_error("Unknown test: " + test_name + ". Valid tests: fixed_variable, scaling, infeasible, redundant_row, bound_tightening, all");
        }
    } catch (const std::exception& e) {
        std::cerr << "Unhandled Exception: " << e.what() << "\n";
        return 1;
    }
    
    std::cout << "All Presolver tests passed.\n";
    return 0;
}
