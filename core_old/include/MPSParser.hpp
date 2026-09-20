#pragma once

#include "SparseProblem.hpp"
#include <string>
#include <istream>

namespace firefly {

class MPSParser {
public:
    // Parses an MPS file from a given file path
    static SparseProblem parse_file(const std::string& filepath);

    // Parses an MPS formatted string (useful for testing)
    static SparseProblem parse_string(const std::string& mps_content);

private:
    static SparseProblem parse_stream(std::istream& stream);
};

} // namespace firefly
