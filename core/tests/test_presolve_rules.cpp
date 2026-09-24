#include "firefly/presolve.h"
#include <iostream>
#include <vector>
#include "test_utils.h"
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include <cmath>

using namespace firefly;

void assert_double_eq(double a, double b) {
    if (std::isinf(a) && std::isinf(b) && (a > 0) == (b > 0)) return;
    if (std::abs(a - b) >= 1e-6) {
        std::cerr << "Assertion failed: " << a << " != " << b << "\n";
        FIREFLY_TEST_ASSERT(false);
    }
}

// Compute objective value for a given primal solution
double compute_objective(const Problem& prob, const std::vector<double>& primal) {
    double obj = 0.0;
    for (size_t i = 0; i < prob.col_names.size(); ++i) {
        obj += prob.objective[i] * primal[i];
    }
    return obj;
}

// Mock solver that simply assigns lower bound to all variables
std::vector<double> mock_solve(const Problem& prob) {
    std::vector<double> sol(prob.col_names.size(), 0.0);
    for (size_t i = 0; i < prob.col_names.size(); ++i) {
        if (!std::isinf(prob.col_lower_bounds[i])) {
            sol[i] = prob.col_lower_bounds[i];
        } else if (!std::isinf(prob.col_upper_bounds[i])) {
            sol[i] = prob.col_upper_bounds[i];
        }
    }
    return sol;
}

void test_isolation_and_postsolve() {
    Problem prob;
    prob.name = "iso_test";
    
    // X1 fixed to 5, X2 has single-var row tightening it
    prob.col_names = {"X1", "X2", "X3"};
    prob.col_to_index = {{"X1", 0}, {"X2", 1}, {"X3", 2}};
    prob.col_lower_bounds = {5.0, 0.0, 0.0};
    prob.col_upper_bounds = {5.0, 100.0, 100.0}; // X1 fixed
    prob.is_integer = {false, false, false};
    prob.objective = {10.0, 20.0, 30.0}; // Cost vector
    
    prob.row_names = {"C1", "C2", "C3"};
    prob.row_to_index = {{"C1", 0}, {"C2", 1}, {"C3", 2}};
    prob.row_lower_bounds = {-INF, 10.0, -INF};
    prob.row_upper_bounds = {50.0, 10.0, 0.0}; // C1 is upper bounded, C2 is fixed, C3 is upper bounded
    prob.row_senses = {'L', 'E', 'L'};
    
    // C1: 2*X2 <= 50 -> X2 <= 25 (single row bound tightening)
    // C2: X1 + X2 + X3 = 10 -> C2 not empty
    // C3: 0*X1 + 0*X2 + 0*X3 <= 0 -> Empty row, valid
    prob.matrix = {
        {0, 1, 2.0},
        {1, 0, 1.0}, {1, 1, 1.0}, {1, 2, 1.0}
    };
    
    PresolveOptions opts;
    auto result_full = Presolver::presolve(prob, opts);
    
    // Verify bound tightening narrows bounds
    FIREFLY_TEST_ASSERT(result_full.stats.bounds_tightened > 0);
    // Original X2 upper bound was 100. With C1: 2*X2 <= 50, X2 upper bound should be 25.
    // In reduced problem, X2 is the first variable (since X1 is removed).
    assert_double_eq(result_full.problem.col_upper_bounds[0], 25.0);
    
    // Verify un-presolve correctly restores variable
    auto reduced_sol = mock_solve(result_full.problem); // X2=0, X3=0
    auto orig_sol = Presolver::postsolve_primal(result_full, reduced_sol);
    
    FIREFLY_TEST_ASSERT(orig_sol.size() == 3);
    assert_double_eq(orig_sol[0], 5.0); // X1 correctly restored
    assert_double_eq(orig_sol[1], reduced_sol[0] * result_full.postsolve.col_scale_factors[1]);
    
    // Confirm objective matches!
    double orig_obj = compute_objective(prob, orig_sol);
    double reduced_obj = compute_objective(result_full.problem, reduced_sol);
    assert_double_eq(orig_obj, reduced_obj + result_full.postsolve.objective_offset);
    
    // Isolate rules
    PresolveOptions opt_no_tighten;
    opt_no_tighten.enable_singleton_row_tightening = false;
    auto result_no_tighten = Presolver::presolve(prob, opt_no_tighten);
    FIREFLY_TEST_ASSERT(result_no_tighten.stats.bounds_tightened == 0); // No tightening
    
    PresolveOptions opt_no_fixed;
    opt_no_fixed.enable_fixed_variable_substitution = false;
    auto result_no_fixed = Presolver::presolve(prob, opt_no_fixed);
    FIREFLY_TEST_ASSERT(result_no_fixed.stats.variables_fixed == 0); // X1 is not removed
    
    std::cout << "test_isolation_and_postsolve passed.\n";
}

void test_scaling_objective_invariance() {
    // Parse one of the real instances, e.g., adlittle
    std::string path = "E:/firefly/core/tests/netlib_miplib/adlittle.mps";
    Problem prob = MPSParser::parse(path);
    
    PresolveOptions opt_scale;
    opt_scale.enable_scaling = true;
    auto result_scale = Presolver::presolve(prob, opt_scale);
    
    PresolveOptions opt_no_scale;
    opt_no_scale.enable_scaling = false;
    auto result_no_scale = Presolver::presolve(prob, opt_no_scale);
    
    // We can't solve it yet, but we can verify that for any dummy solution, the objective maps back correctly.
    auto dummy_sol_scale = mock_solve(result_scale.problem);
    auto restored_sol = Presolver::postsolve_primal(result_scale, dummy_sol_scale);
    
    double orig_obj = compute_objective(prob, restored_sol);
    double reduced_obj = compute_objective(result_scale.problem, dummy_sol_scale);
    
    assert_double_eq(orig_obj, reduced_obj + result_scale.postsolve.objective_offset);
    
    std::cout << "test_scaling_objective_invariance passed. Scaling doesn't change mapped mathematical objective.\n";
}

void test_feasibility_invariance() {
    // Confirm presolve never flips feasible <-> infeasible on small known status problems
    // We simulate "feasible" by ensuring presolve() doesn't throw InfeasibleProblemException on good problems.
    std::vector<std::string> good_files = {
        "afiro.mps", "adlittle.mps", "israel.mps", "greenbea.mps"
    };
    
    for (const auto& f : good_files) {
        std::string path = "E:/firefly/core/tests/netlib_miplib/" + f;
        Problem prob = MPSParser::parse(path);
        
        try {
            Presolver::presolve(prob);
        } catch (const InfeasibleProblemException& e) {
            std::cerr << "Presolve incorrectly flagged " << f << " as infeasible!\n";
            FIREFLY_TEST_ASSERT(false);
        }
    }
    
    std::cout << "test_feasibility_invariance passed.\n";
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

    std::cout << "--- Advanced Presolve Rules Tests --- (test: " << test_name << ")\n";
    try {
        if (test_name == "isolation_and_postsolve") test_isolation_and_postsolve();
        else if (test_name == "scaling_objective_invariance") test_scaling_objective_invariance();
        else if (test_name == "feasibility_invariance") test_feasibility_invariance();
        else if (test_name == "all") {
            test_isolation_and_postsolve();
            test_scaling_objective_invariance();
            test_feasibility_invariance();
        } else {
            throw std::runtime_error("Unknown test: " + test_name + ". Valid tests: isolation_and_postsolve, scaling_objective_invariance, feasibility_invariance, all");
        }
    } catch (const std::exception& e) {
        std::cerr << "Unhandled Exception: " << e.what() << "\n";
        return 1;
    }
    
    std::cout << "All advanced presolve tests passed.\n";
    return 0;
}
