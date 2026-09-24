#include "firefly/mps_parser.h"
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

void test_marker_int() {
    auto prob = MPSParser::parse("E:/firefly/core/tests/regression/marker_int.mps");
    FIREFLY_TEST_ASSERT(prob.is_integer[prob.col_to_index.at("X1")] == true);
    std::cout << "test_marker_int passed.\n";
}

void test_ranges() {
    auto prob = MPSParser::parse("E:/firefly/core/tests/regression/ranges.mps");
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
    auto prob = MPSParser::parse("E:/firefly/core/tests/regression/all_bounds.mps");
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
    FIREFLY_TEST_ASSERT(prob.is_integer[x7]);
    
    std::cout << "test_all_bounds passed.\n";
}

void test_ill_conditioned() {
    auto prob = MPSParser::parse("E:/firefly/core/tests/regression/ill_conditioned.mps");
    
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
    auto prob = MPSParser::parse("E:/firefly/core/tests/regression/maximize.mps");
    FIREFLY_TEST_ASSERT(prob.minimize == false);
    std::cout << "test_maximize passed.\n";
}

void test_malformed_missing_endata() {
    try {
        MPSParser::parse("E:/firefly/core/tests/regression/malformed.mps");
        FIREFLY_TEST_ASSERT(false && "Should have thrown MPSParseException for missing ENDATA");
    } catch (const MPSParseException& e) {
        std::string msg = e.what();
        FIREFLY_TEST_ASSERT(msg.find("Missing ENDATA") != std::string::npos);
        // Ensure line number is valid context
        FIREFLY_TEST_ASSERT(e.line_number() > 0);
    }
    std::cout << "test_malformed_missing_endata passed.\n";
}

void test_malformed_unknown_section() {
    try {
        MPSParser::parse("E:/firefly/core/tests/regression/malformed_unknown_section.mps");
        FIREFLY_TEST_ASSERT(false && "Should have thrown MPSParseException");
    } catch (const MPSParseException& e) {
        std::string msg = e.what();
        FIREFLY_TEST_ASSERT(msg.find("Unknown section header: FAKE_SECTION") != std::string::npos);
        FIREFLY_TEST_ASSERT(e.line_number() == 4);
    }
    std::cout << "test_malformed_unknown_section passed.\n";
}

void test_malformed_undeclared_row() {
    try {
        MPSParser::parse("E:/firefly/core/tests/regression/malformed_undeclared_row.mps");
        FIREFLY_TEST_ASSERT(false && "Should have thrown MPSParseException");
    } catch (const MPSParseException& e) {
        std::string msg = e.what();
        FIREFLY_TEST_ASSERT(msg.find("Reference to undeclared row: BAD_ROW") != std::string::npos);
        FIREFLY_TEST_ASSERT(e.line_number() == 5);
    }
    std::cout << "test_malformed_undeclared_row passed.\n";
}

void test_malformed_empty() {
    try {
        MPSParser::parse("E:/firefly/core/tests/regression/malformed_empty.mps");
        FIREFLY_TEST_ASSERT(false && "Should have thrown MPSParseException");
    } catch (const MPSParseException& e) {
        std::string msg = e.what();
        FIREFLY_TEST_ASSERT(msg.find("Empty file") != std::string::npos);
        FIREFLY_TEST_ASSERT(e.line_number() == 0);
    }
    std::cout << "test_malformed_empty passed.\n";
}

void test_whitespace() {
    auto prob = MPSParser::parse("E:/firefly/core/tests/regression/whitespace.mps");
    size_t x1 = prob.col_to_index.at("X1");
    size_t x2 = prob.col_to_index.at("X2");
    size_t c1 = prob.row_to_index.at("C1");
    assert_double_eq(prob.objective[x1], 10.0);
    assert_double_eq(prob.objective[x2], 2.0);
    std::cout << "test_whitespace passed.\n";
}

void run_mps_test_by_name(const std::string& name) {
    if (name == "marker_int") test_marker_int();
    else if (name == "ranges") test_ranges();
    else if (name == "all_bounds") test_all_bounds();
    else if (name == "ill_conditioned") test_ill_conditioned();
    else if (name == "whitespace") test_whitespace();
    else if (name == "maximize") test_maximize();
    else if (name == "malformed_missing_endata") test_malformed_missing_endata();
    else if (name == "malformed_unknown_section") test_malformed_unknown_section();
    else if (name == "malformed_undeclared_row") test_malformed_undeclared_row();
    else if (name == "malformed_empty") test_malformed_empty();
    else if (name == "all") {
        test_marker_int();
        test_ranges();
        test_all_bounds();
        test_ill_conditioned();
        test_whitespace();
        test_maximize();
        test_malformed_missing_endata();
        test_malformed_unknown_section();
        test_malformed_undeclared_row();
        test_malformed_empty();
    } else {
        throw std::runtime_error("Unknown test: " + name + ". Valid tests: marker_int, ranges, all_bounds, ill_conditioned, whitespace, maximize, malformed_missing_endata, malformed_unknown_section, malformed_undeclared_row, malformed_empty, all");
    }
}
