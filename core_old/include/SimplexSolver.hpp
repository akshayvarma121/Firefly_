#pragma once

#include "SparseProblem.hpp"
#include <vector>

namespace firefly {

enum class SimplexStatus {
    OPTIMAL,
    INFEASIBLE,
    UNBOUNDED,
    MAX_ITERATIONS
};

struct SimplexResult {
    SimplexStatus status;
    double objective_value;
    std::vector<double> solution;
};

class SimplexSolver {
public:
    static SimplexResult solve(const SparseProblem& prob);
};

} // namespace firefly
