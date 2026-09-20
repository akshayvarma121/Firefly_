#include "firefly/mps_parser.h"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace firefly;

void assert_double_eq(double a, double b) {
    if (std::isinf(a) && std::isinf(b) && (a > 0) == (b > 0)) return;
    assert(std::abs(a - b) < 1e-9);
}

void test_marker_int() {
    auto prob = MPSParser::parse("../../tests/regression/marker_int.mps");
    assert(prob.is_integer[prob.col_to_index.at("X1")] == true);
    std::cout << "test_marker_int passed.\n";
}

void test_ranges() {
    auto prob = MPSParser::parse("../../tests/regression/ranges.mps");
    size_t r1 = prob.row_to_index.at("C1_LE");
    size_t r2 = prob.row_to_index.at("C2_GE");
    size_t r3 = prob.row_to_index.at("C3_EQ_POS");
    size_t r4 = prob.row_to_index.at("C4_EQ_NEG");

    assert_double_eq(prob.row_lower_bounds[r1], 5.0);
    assert_double_eq(prob.row_upper_bounds[r1], 10.0);

    assert_double_eq(prob.row_lower_bounds[r2], 20.0);
    assert_double_eq(prob.row_upper_bounds[r2], 25.0);

    assert_double_eq(prob.row_lower_bounds[r3], 30.0);
    assert_double_eq(prob.row_upper_bounds[r3], 35.0);

    assert_double_eq(prob.row_lower_bounds[r4], 35.0);
    assert_double_eq(prob.row_upper_bounds[r4], 40.0);
    
    std::cout << "test_ranges passed.\n";
}

void test_all_bounds() {
    auto prob = MPSParser::parse("../../tests/regression/all_bounds.mps");
    size_t x1 = prob.col_to_index.at("X1");
    size_t x2 = prob.col_to_index.at("X2");
    size_t x3 = prob.col_to_index.at("X3");
    size_t x4 = prob.col_to_index.at("X4");
    size_t x5 = prob.col_to_index.at("X5");
    size_t x6 = prob.col_to_index.at("X6");
    size_t x7 = prob.col_to_index.at("X7");

    assert_double_eq(prob.col_lower_bounds[x1], 0.0);
    assert_double_eq(prob.col_upper_bounds[x1], 10.0);

    assert_double_eq(prob.col_lower_bounds[x2], 2.0);
    assert_double_eq(prob.col_upper_bounds[x2], INF);

    assert_double_eq(prob.col_lower_bounds[x3], 5.0);
    assert_double_eq(prob.col_upper_bounds[x3], 5.0);

    assert_double_eq(prob.col_lower_bounds[x4], -INF);
    assert_double_eq(prob.col_upper_bounds[x4], INF);

    assert_double_eq(prob.col_lower_bounds[x5], -INF);
    assert_double_eq(prob.col_upper_bounds[x5], INF);

    assert_double_eq(prob.col_lower_bounds[x6], 0.0);
    assert_double_eq(prob.col_upper_bounds[x6], INF);

    assert_double_eq(prob.col_lower_bounds[x7], 0.0);
    assert_double_eq(prob.col_upper_bounds[x7], 1.0);
    assert(prob.is_integer[x7]);
    
    std::cout << "test_all_bounds passed.\n";
}

void test_ill_conditioned() {
    auto prob = MPSParser::parse("../../tests/regression/ill_conditioned.mps");
    
    // Check that scientific notation and varied scales parsed correctly
    size_t x1 = prob.col_to_index.at("X1");
    size_t x2 = prob.col_to_index.at("X2");
    size_t c1 = prob.row_to_index.at("C1");

    assert_double_eq(prob.objective[x1], 1000000.0);
    assert_double_eq(prob.objective[x2], 0.000001);

    // Matrix parsing verification
    double v_x1_c1 = 0, v_x2_c1 = 0;
    for (const auto& trip : prob.matrix) {
        if (trip.row == c1 && trip.col == x1) v_x1_c1 = trip.value;
        if (trip.row == c1 && trip.col == x2) v_x2_c1 = trip.value;
    }
    
    assert_double_eq(v_x1_c1, 0.000001);
    assert_double_eq(v_x2_c1, 1000000.0);

    std::cout << "test_ill_conditioned passed.\n";
}

void test_maximize() {
    auto prob = MPSParser::parse("../../tests/regression/maximize.mps");
    assert(prob.minimize == false);
    std::cout << "test_maximize passed.\n";
}

void test_malformed_missing_endata() {
    try {
        MPSParser::parse("../../tests/regression/malformed.mps");
        assert(false && "Should have thrown MPSParseException for missing ENDATA");
    } catch (const MPSParseException& e) {
        std::string msg = e.what();
        assert(msg.find("Missing ENDATA") != std::string::npos);
        // Ensure line number is valid context
        assert(e.line_number() > 0);
    }
    std::cout << "test_malformed_missing_endata passed.\n";
}

void test_malformed_unknown_section() {
    try {
        MPSParser::parse("../../tests/regression/malformed_unknown_section.mps");
        assert(false && "Should have thrown MPSParseException");
    } catch (const MPSParseException& e) {
        std::string msg = e.what();
        assert(msg.find("Unknown section header: FAKE_SECTION") != std::string::npos);
        assert(e.line_number() == 4);
    }
    std::cout << "test_malformed_unknown_section passed.\n";
}

void test_malformed_undeclared_row() {
    try {
        MPSParser::parse("../../tests/regression/malformed_undeclared_row.mps");
        assert(false && "Should have thrown MPSParseException");
    } catch (const MPSParseException& e) {
        std::string msg = e.what();
        assert(msg.find("Reference to undeclared row: BAD_ROW") != std::string::npos);
        assert(e.line_number() == 5);
    }
    std::cout << "test_malformed_undeclared_row passed.\n";
}

void test_malformed_empty() {
    try {
        MPSParser::parse("../../tests/regression/malformed_empty.mps");
        assert(false && "Should have thrown MPSParseException");
    } catch (const MPSParseException& e) {
        std::string msg = e.what();
        assert(msg.find("Empty file") != std::string::npos);
        assert(e.line_number() == 0);
    }
    std::cout << "test_malformed_empty passed.\n";
}

void run_all_mps_tests() {
    test_marker_int();
    test_ranges();
    test_all_bounds();
    test_ill_conditioned();
    test_maximize();
    test_malformed_missing_endata();
    test_malformed_unknown_section();
    test_malformed_undeclared_row();
    test_malformed_empty();
}
