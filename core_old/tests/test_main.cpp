#include "MPSParser.hpp"
#include <iostream>
#include <cassert>
#include <cmath>
#include <limits>

using namespace firefly;

void assert_double_eq(double a, double b) {
    if (std::isinf(a) && std::isinf(b)) {
        assert(std::signbit(a) == std::signbit(b));
    } else {
        assert(std::abs(a - b) < 1e-9);
    }
}

void test_mps_parser_1() {
    std::string mps = R"(NAME          TEST1
ROWS
 N  OBJ
 L  R1
 G  R2
COLUMNS
    X1        OBJ       -1.0
    X1        R1        1.0
    X1        R2        1.0
    X2        OBJ       -1.0
    X2        R1        1.0
RHS
    RHS1      R1        10.0
    RHS1      R2        2.0
BOUNDS
 UP BND       X1        5.0
ENDATA
)";

    auto prob = MPSParser::parse_string(mps);

    assert(prob.name == "TEST1");
    assert(prob.num_vars == 2);
    assert(prob.num_constrs == 2);

    // Obj
    assert_double_eq(prob.obj_coeffs[0], -1.0);
    assert_double_eq(prob.obj_coeffs[1], -1.0);

    // Senses
    assert(prob.row_senses[0] == 'L');
    assert(prob.row_senses[1] == 'G');

    // RHS
    assert_double_eq(prob.rhs[0], 10.0);
    assert_double_eq(prob.rhs[1], 2.0);

    // Bounds
    assert_double_eq(prob.var_lower_bounds[0], 0.0);
    assert_double_eq(prob.var_upper_bounds[0], 5.0);
    assert_double_eq(prob.var_lower_bounds[1], 0.0);
    assert_double_eq(prob.var_upper_bounds[1], std::numeric_limits<double>::infinity());

    // CSR
    assert(prob.row_ptr == std::vector<int>({0, 2, 3}));
    assert(prob.col_idx == std::vector<int>({0, 1, 0}));
    assert_double_eq(prob.values[0], 1.0);
    assert_double_eq(prob.values[1], 1.0);
    assert_double_eq(prob.values[2], 1.0);
}

void test_mps_parser_2() {
    std::string mps = R"(NAME          TEST2
ROWS
 N  OBJ
 E  R1
 L  R2
COLUMNS
    X1        OBJ       1.0
    X1        R1        1.0
    X2        R1        2.0
    X2        R2        1.0
RHS
    RHS1      R1        5.0
    RHS1      R2        20.0
BOUNDS
 FR BND       X1
ENDATA
)";

    auto prob = MPSParser::parse_string(mps);

    assert(prob.name == "TEST2");
    assert(prob.num_vars == 2);
    assert(prob.num_constrs == 2);

    assert_double_eq(prob.obj_coeffs[0], 1.0);
    assert_double_eq(prob.obj_coeffs[1], 0.0);

    assert(prob.row_senses[0] == 'E');
    assert(prob.row_senses[1] == 'L');

    assert_double_eq(prob.rhs[0], 5.0);
    assert_double_eq(prob.rhs[1], 20.0);

    assert_double_eq(prob.var_lower_bounds[0], -std::numeric_limits<double>::infinity());
    assert_double_eq(prob.var_upper_bounds[0], std::numeric_limits<double>::infinity());
    assert_double_eq(prob.var_lower_bounds[1], 0.0);
    assert_double_eq(prob.var_upper_bounds[1], std::numeric_limits<double>::infinity());

    assert(prob.row_ptr == std::vector<int>({0, 2, 3}));
    assert(prob.col_idx == std::vector<int>({0, 1, 1}));
    assert_double_eq(prob.values[0], 1.0);
    assert_double_eq(prob.values[1], 2.0);
    assert_double_eq(prob.values[2], 1.0);
}

void test_mps_parser_ranges() {
    std::string mps = R"(NAME          RANGES_TEST
ROWS
 N  OBJ
 L  RL
 G  RG
 E  RE1
 E  RE2
COLUMNS
    X         OBJ       1.0
    X         RL        1.0
    X         RG        1.0
    X         RE1       1.0
    X         RE2       1.0
RHS
    RHS1      RL        10.0
    RHS1      RG        20.0
    RHS1      RE1       30.0
    RHS1      RE2       40.0
RANGES
    RNG1      RL        5.0
    RNG1      RG        5.0
    RNG1      RE1       5.0
    RNG1      RE2       -5.0
BOUNDS
ENDATA
)";

    auto prob = MPSParser::parse_string(mps);

    assert(prob.num_vars == 5); // 1 original + 4 slacks
    assert(prob.num_constrs == 4);

    // All rows should be converted to equalities
    for (char s : prob.row_senses) {
        assert(s == 'E');
    }

    // RHS values should be unchanged
    assert_double_eq(prob.rhs[0], 10.0);
    assert_double_eq(prob.rhs[1], 20.0);
    assert_double_eq(prob.rhs[2], 30.0);
    assert_double_eq(prob.rhs[3], 40.0);

    // X bounds: 0 to inf
    assert_double_eq(prob.var_lower_bounds[0], 0.0);
    assert(std::isinf(prob.var_upper_bounds[0]));

    // Slack for RL (L range=5.0) -> coeff=1.0, bound=[0, 5.0]
    assert_double_eq(prob.var_lower_bounds[1], 0.0);
    assert_double_eq(prob.var_upper_bounds[1], 5.0);
    
    // Slack for RG (G range=5.0) -> coeff=-1.0, bound=[0, 5.0]
    assert_double_eq(prob.var_lower_bounds[2], 0.0);
    assert_double_eq(prob.var_upper_bounds[2], 5.0);

    // Slack for RE1 (E positive range=5.0) -> coeff=-1.0, bound=[0, 5.0]
    assert_double_eq(prob.var_lower_bounds[3], 0.0);
    assert_double_eq(prob.var_upper_bounds[3], 5.0);

    // Slack for RE2 (E negative range=-5.0) -> coeff=1.0, bound=[0, 5.0]
    assert_double_eq(prob.var_lower_bounds[4], 0.0);
    assert_double_eq(prob.var_upper_bounds[4], 5.0);

    // Check matrix entries for slacks (row_ptr and col_idx and values)
    assert(prob.row_ptr[1] - prob.row_ptr[0] == 2);
    assert_double_eq(prob.values[prob.row_ptr[0] + 1], 1.0); // Slack1 coeff
    
    assert(prob.row_ptr[2] - prob.row_ptr[1] == 2);
    assert_double_eq(prob.values[prob.row_ptr[1] + 1], -1.0); // Slack2 coeff
    
    assert(prob.row_ptr[3] - prob.row_ptr[2] == 2);
    assert_double_eq(prob.values[prob.row_ptr[2] + 1], -1.0); // Slack3 coeff
    
    assert(prob.row_ptr[4] - prob.row_ptr[3] == 2);
    assert_double_eq(prob.values[prob.row_ptr[3] + 1], 1.0); // Slack4 coeff
}

int main() {
    std::cout << "Running MPSParser tests...\n";
    
    test_mps_parser_1();
    std::cout << "test_mps_parser_1 passed.\n";

    test_mps_parser_2();
    std::cout << "test_mps_parser_2 passed.\n";

    test_mps_parser_ranges();
    std::cout << "test_mps_parser_ranges passed.\n";

    std::cout << "All MPSParser tests passed!\n";
    return 0;
}
