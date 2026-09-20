#include "firefly/mps_parser.h"
#include "firefly/presolve.h"
#include "firefly/simplex.h"
#include <iostream>
using namespace firefly;
int main() {
    auto p = MPSParser::parse("core/tests/regression/all_bounds.mps");
    auto pre = Presolver::presolve(p);
    SimplexOptions so;
    auto res = SimplexSolver::solve(pre, so);
    std::cout << "Status: " << static_cast<int>(res.status) << std::endl;
    return 0;
}
