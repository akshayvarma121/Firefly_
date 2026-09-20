#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <limits>

namespace firefly {

// A triplet for sparse matrix representation
struct Triplet {
    size_t row;
    size_t col;
    double value;
};

const double INF = std::numeric_limits<double>::infinity();

// Represents the parsed optimization problem
struct Problem {
    std::string name;
    bool minimize = true;
    std::string objective_name;

    // Names
    std::vector<std::string> row_names;
    std::vector<std::string> col_names;

    // Fast lookups (name -> index)
    std::unordered_map<std::string, size_t> row_to_index;
    std::unordered_map<std::string, size_t> col_to_index;

    // Row definitions
    // Sense: 'N' (none/obj), 'L' (<=), 'G' (>=), 'E' (==)
    std::vector<char> row_senses;
    
    // Bounds on rows (RHS & RANGES converted to explicit bounds)
    std::vector<double> row_lower_bounds;
    std::vector<double> row_upper_bounds;

    // Column definitions
    std::vector<double> col_lower_bounds;
    std::vector<double> col_upper_bounds;
    std::vector<bool> is_integer;

    // Objective function coefficients
    std::vector<double> objective;

    // Sparse matrix triplets
    std::vector<Triplet> matrix;
};

class MPSParseException : public std::runtime_error {
public:
    MPSParseException(size_t line_number, const std::string& message)
        : std::runtime_error("Line " + std::to_string(line_number) + ": " + message),
          line_number_(line_number) {}

    size_t line_number() const { return line_number_; }

private:
    size_t line_number_;
};

class MPSParser {
public:
    // Parse an MPS file and return a Problem struct.
    // Throws MPSParseException on any malformed input.
    static Problem parse(const std::string& filepath);
};

} // namespace firefly
