#include "firefly/simplex.h"
#include "firefly/mps_parser.h"
#include <iostream>
#include "test_utils.h"
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include <cmath>

using namespace firefly;

#define ASSERT_EQ(a, b) if ((a) != (b)) { throw std::runtime_error("Assertion failed: " + std::to_string(static_cast<int>(a)) + " != " + std::to_string(static_cast<int>(b))); }
#define ASSERT_TRUE(cond) if (!(cond)) { throw std::runtime_error("Assertion failed: " #cond); }

void assert_double_eq(double a, double b) {
    if (std::isinf(a) && std::isinf(b) && (a > 0) == (b > 0)) return;
    double diff = std::abs(a - b);
    double rel_diff = diff / std::max(1.0, std::max(std::abs(a), std::abs(b)));
    if (diff >= 1e-6 && rel_diff >= 1e-6) {
        std::cerr << "Assertion failed: " << a << " != " << b << "\n";
        throw std::runtime_error("Double assertion failed");
    }
}

void test_optimal() {
    Problem p;
    p.name = "opt";
    p.col_names = {"x1", "x2"};
    p.col_lower_bounds = {0, 0};
    p.col_upper_bounds = {INF, INF};
    p.is_integer = {false, false};
    p.objective = {-3, -2}; // max 3x1 + 2x2 => min -3x1 - 2x2
    
    p.row_names = {"c1", "c2", "c3"};
    p.row_lower_bounds = {-INF, -INF, -INF};
    p.row_upper_bounds = {4, 6, 18};
    p.row_senses = {'L', 'L', 'L'};
    
    // c1: x1 + x2 <= 4
    // c2: x1 - x2 <= 6 (not binding)
    // c3: 2x1 + x2 <= 18 (not binding)
    p.matrix = {
        {0, 0, 1.0}, {0, 1, 1.0},
        {1, 0, 1.0}, {1, 1, -1.0},
        {2, 0, 2.0}, {2, 1, 1.0}
    };
    
    PresolveOptions popts; popts.enable_scaling = false; popts.enable_empty_row_removal = false; popts.enable_singleton_row_tightening = false;
    auto pre = Presolver::presolve(p, popts);
    
    SimplexOptions sopts;
    auto res = SimplexSolver::solve(pre, sopts);
    ASSERT_EQ(res.status, SolveStatus::OPTIMAL);
    assert_double_eq(res.objective_value, -12.0);
    assert_double_eq(res.primal_solution[0], 4.0);
    assert_double_eq(res.primal_solution[1], 0.0);
    
    std::cout << "test_optimal passed.\n";
}

void test_infeasible() {
    Problem p;
    p.name = "inf";
    p.col_names = {"x1"};
    p.col_lower_bounds = {0};
    p.col_upper_bounds = {INF};
    p.is_integer = {false};
    p.objective = {1};
    
    p.row_names = {"c1", "c2"};
    p.row_lower_bounds = {5, -INF};
    p.row_upper_bounds = {INF, 3};
    p.row_senses = {'G', 'L'};
    // x1 >= 5
    // x1 <= 3
    
    p.matrix = {
        {0, 0, 1.0},
        {1, 0, 1.0}
    };
    
    PresolveOptions popts; 
    popts.enable_singleton_row_tightening = false; // Disable presolve fixing it to test Simplex Phase I
    auto pre = Presolver::presolve(p, popts);
    
    auto res = SimplexSolver::solve(pre);
    ASSERT_EQ(res.status, SolveStatus::INFEASIBLE);
    
    std::cout << "test_infeasible passed.\n";
}

void test_unbounded() {
    Problem p;
    p.name = "unb";
    p.col_names = {"x1"};
    p.col_lower_bounds = {0};
    p.col_upper_bounds = {INF};
    p.is_integer = {false};
    p.objective = {-1}; // min -x1
    
    p.row_names = {"c1"};
    p.row_lower_bounds = {-INF};
    p.row_upper_bounds = {INF}; // no upper bound constraint
    p.row_senses = {'L'};
    
    p.matrix = { {0, 0, 1.0} };
    
    PresolveOptions popts; 
    popts.enable_empty_row_removal = false; 
    popts.enable_singleton_row_tightening = false;
    auto pre = Presolver::presolve(p, popts);
    
    auto res = SimplexSolver::solve(pre);
    ASSERT_EQ(res.status, SolveStatus::UNBOUNDED);
    
    std::cout << "test_unbounded passed.\n";
}

void test_bland_cycling() {
    // Beale's cycling LP
    // max 3/4 x1 - 20 x2 + 1/2 x3 - 6 x4
    // x1 - 8 x2 - x3 + 9 x4 <= 0
    // 1/2 x1 - 12 x2 - 1/2 x3 + 3 x4 <= 0
    // x3 <= 1
    // x >= 0
    
    Problem p;
    p.name = "beale";
    p.col_names = {"x1", "x2", "x3", "x4"};
    p.col_lower_bounds = {0, 0, 0, 0};
    p.col_upper_bounds = {INF, INF, INF, INF};
    p.is_integer = {false, false, false, false};
    p.objective = {-0.75, 20.0, -0.5, 6.0}; // min -3/4 x1 + 20 x2 - 1/2 x3 + 6 x4
    
    p.row_names = {"c1", "c2", "c3"};
    p.row_lower_bounds = {-INF, -INF, -INF};
    p.row_upper_bounds = {0.0, 0.0, 1.0};
    
    p.matrix = {
        {0, 0, 1.0}, {0, 1, -8.0}, {0, 2, -1.0}, {0, 3, 9.0},
        {1, 0, 0.5}, {1, 1, -12.0}, {1, 2, -0.5}, {1, 3, 3.0},
        {2, 2, 1.0}
    };
    
    PresolveOptions popts;
    popts.enable_singleton_row_tightening = false;
    popts.enable_scaling = false;
    auto pre = Presolver::presolve(p, popts);
    
    SimplexOptions sopts;
    sopts.max_iterations = 1000;
    auto res = SimplexSolver::solve(pre, sopts);
    
    FIREFLY_TEST_ASSERT(res.status == SolveStatus::OPTIMAL);
    // Beale's problem optimal is x1=1, x3=1, obj = -1.25 for min (-5/4)
    assert_double_eq(res.objective_value, -1.25);
    
    std::cout << "test_bland_cycling passed. (Simplex finished in " << res.phase2_iterations << " Phase 2 iterations)\n";
}

void test_regression_infeasible() {
    auto p = MPSParser::parse("E:/firefly/core/tests/regression/infeasible.mps");
    PresolveOptions popts;
    popts.enable_scaling = false;
    popts.enable_empty_row_removal = false;
    popts.enable_empty_col_removal = false;
    popts.enable_singleton_row_tightening = false;
    popts.enable_fixed_variable_substitution = false;
    auto pre = Presolver::presolve(p, popts);
    SimplexOptions sopts;
    auto res = SimplexSolver::solve(pre, sopts);
    ASSERT_EQ(res.status, SolveStatus::INFEASIBLE);
    std::cout << "test_regression_infeasible passed.\n";
}

void test_regression_unbounded() {
    auto p = MPSParser::parse("E:/firefly/core/tests/regression/unbounded.mps");
    PresolveOptions popts;
    popts.enable_scaling = false;
    popts.enable_empty_row_removal = false;
    popts.enable_empty_col_removal = false;
    popts.enable_singleton_row_tightening = false;
    popts.enable_fixed_variable_substitution = false;
    auto pre = Presolver::presolve(p, popts);
    SimplexOptions sopts;
    auto res = SimplexSolver::solve(pre, sopts);
    ASSERT_EQ(res.status, SolveStatus::UNBOUNDED);
    std::cout << "test_regression_unbounded passed.\n";
}

void test_netlib_reference() {
    std::vector<std::pair<std::string, double>> tests = {
        {"afiro.mps", -464.75314286},
        {"adlittle.mps", 225494.96316},
        {"israel.mps", -896644.82186},
        {"flugpl.mps", 1167185.7256}
    };
    
    for (const auto& test : tests) {
        auto p = MPSParser::parse("E:/firefly/core/tests/netlib_miplib/" + test.first);
        auto pre = Presolver::presolve(p);
        SimplexOptions sopts;
        auto res = SimplexSolver::solve(pre, sopts);
        ASSERT_EQ(res.status, SolveStatus::OPTIMAL);
        assert_double_eq(res.objective_value, test.second);
        std::cout << "test_netlib_reference passed for " << test.first << "\n";
    }
}

void test_netlib_infeasible() {
    auto p = MPSParser::parse("E:/firefly/core/tests/netlib_miplib/woodinfe.mps");
    try {
        auto pre = Presolver::presolve(p);
        SimplexOptions sopts;
        auto res = SimplexSolver::solve(pre, sopts);
        ASSERT_EQ(res.status, SolveStatus::INFEASIBLE);
    } catch(const InfeasibleProblemException& e) {
        // Presolve caught it
    }
    std::cout << "test_netlib_infeasible passed for woodinfe.mps\n";
}

void test_feasibility_self_check() {
    Problem p;
    p.name = "feas_check";
    p.col_names = {"x1", "x2"};
    p.col_lower_bounds = {0, 0};
    p.col_upper_bounds = {INF, INF};
    p.is_integer = {false, false};
    p.objective = {-1, -1};
    
    p.row_names = {"c1"};
    p.row_lower_bounds = {-INF};
    p.row_upper_bounds = {10};
    p.row_senses = {'L'};
    
    p.matrix = {
        {0, 0, 1.0}, {0, 1, 1.0}
    };
    
    PresolveOptions popts;
    popts.enable_scaling = false;
    auto pre = Presolver::presolve(p, popts);
    
    SimplexOptions sopts;
    auto res = SimplexSolver::solve(pre, sopts);
    ASSERT_EQ(res.status, SolveStatus::OPTIMAL);
    
    // Verify that the solution strictly honors the feasibility self-check tolerances
    double c1_val = res.primal_solution[0] + res.primal_solution[1];
    ASSERT_TRUE(c1_val <= 10.0 + 1e-6);
    ASSERT_TRUE(res.primal_solution[0] >= -1e-6);
    ASSERT_TRUE(res.primal_solution[1] >= -1e-6);
    
    std::cout << "test_feasibility_self_check passed.\n";
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
    
    std::cout << "--- CPU Simplex Tests --- (test: " << test_name << ")\n" << std::flush;
    try {
        if (test_name == "optimal") test_optimal();
        else if (test_name == "infeasible") { test_infeasible(); test_regression_infeasible(); test_netlib_infeasible(); }
        else if (test_name == "unbounded") { test_unbounded(); test_regression_unbounded(); }
        else if (test_name == "bland_cycling") test_bland_cycling();
        else if (test_name == "netlib_reference") test_netlib_reference();
        else if (test_name == "feasibility_self_check") test_feasibility_self_check();
        else if (test_name == "all") {
            test_optimal();
            test_infeasible();
            test_regression_infeasible();
            test_netlib_infeasible();
            test_unbounded();
            test_regression_unbounded();
            test_bland_cycling();
            test_netlib_reference();
            test_feasibility_self_check();
        } else {
            throw std::runtime_error("Unknown test: " + test_name + ". Valid tests: optimal, infeasible, unbounded, bland_cycling, netlib_reference, feasibility_self_check, all");
        }
        std::cout << "All CPU Simplex tests passed.\n" << std::flush;
    } catch(const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
