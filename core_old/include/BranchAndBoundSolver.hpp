#pragma once

#include "SparseProblem.hpp"
#include <vector>

namespace firefly {

enum class BnBStatus {
    OPTIMAL,
    INFEASIBLE,
    TIME_LIMIT,
    NODE_LIMIT
};

struct BnBResult {
    BnBStatus status;
    double objective_value;
    std::vector<double> solution;
    int nodes_explored;
};

struct BnBParams {
    bool depth_first = false; // default to best-first
    bool root_gomory_cuts = false;
    double integer_tolerance = 1e-5;
    int max_nodes = 1000000;
    double time_limit = 0.0; // 0 for unlimited
};

class BranchAndBoundSolver {
public:
    static BnBResult solve(const SparseProblem& prob, const BnBParams& params = {});
};

} // namespace firefly
