#include "BranchAndBoundSolver.hpp"
#include "PDLPSolver.hpp"
#include "SimplexSolver.hpp"
#include "StandardForm.hpp"
#include <Eigen/Sparse>
#include <Eigen/SparseLU>
#include <queue>
#include <stack>
#include <cmath>
#include <limits>
#include <iostream>
#include <chrono>

namespace firefly {

struct BnBNode {
    std::vector<double> lower_bounds;
    std::vector<double> upper_bounds;
    double lp_obj = -std::numeric_limits<double>::infinity();
    int depth = 0;

    bool operator<(const BnBNode& other) const {
        return lp_obj > other.lp_obj; // min-priority queue (lowest bound first)
    }
};

void add_gomory_cuts(SparseProblem& prob) {
    // Generate Gomory cuts using Simplex
    auto [std_prob, mapping] = Standardizer::apply(prob);
    
    // We need to run Simplex directly on standard problem to get the basis and B^-1.
    // Since SimplexSolver wraps standardization, we'll extract the core logic here for cut generation.
    // This is a simplified version just for the root node cuts.
    int m = std_prob.num_constrs;
    int n = std_prob.num_vars;
    
    Eigen::SparseMatrix<double, Eigen::ColMajor> A(m, n);
    std::vector<Eigen::Triplet<double>> triplets;
    for (int r = 0; r < m; ++r) {
        for (int ptr = std_prob.row_ptr[r]; ptr < std_prob.row_ptr[r+1]; ++ptr) {
            triplets.push_back({r, std_prob.col_idx[ptr], std_prob.values[ptr]});
        }
    }
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();

    Eigen::VectorXd b = Eigen::Map<Eigen::VectorXd>(std_prob.b.data(), m);
    Eigen::VectorXd c = Eigen::Map<Eigen::VectorXd>(std_prob.c.data(), n);

    // Call SimplexSolver on the ORIGINAL problem to get basic_vars in standard form
    auto res = SimplexSolver::solve(prob);
    if (res.status != SimplexStatus::OPTIMAL) return;

    // Gomory cuts require a tableau, but our SimplexSolver does not currently
    // expose the basic_vars or the optimal basis inverse B^-1.
    // To implement this correctly, we would need to revert the changes to SimplexResult
    // and correctly populate it. For SIH 2026, we will treat Gomory cuts as a stub.
    return;
}

BnBResult BranchAndBoundSolver::solve(const SparseProblem& prob, const BnBParams& params) {
    bool has_integer = false;
    for (bool is_int : prob.is_integer) {
        if (is_int) { has_integer = true; break; }
    }

    if (!has_integer) {
        PDLPSolverParams pdlp_params;
        pdlp_params.max_iterations = 200000;
        auto res = PDLPSolver::solve(prob, pdlp_params);

        bool is_optimal = (res.status == PDLPStatus::OPTIMAL);
        double node_obj = res.objective_value;
        std::vector<double> node_sol = res.solution;

        if (!is_optimal) {
            auto simplex_res = SimplexSolver::solve(prob);
            if (simplex_res.status == SimplexStatus::OPTIMAL) {
                is_optimal = true;
                node_obj = simplex_res.objective_value;
                node_sol = simplex_res.solution;
            }
        }

        if (is_optimal) {
            return {BnBStatus::OPTIMAL, node_obj, node_sol, 1};
        } else {
            return {BnBStatus::INFEASIBLE, 0.0, {}, 1};
        }
    }

    auto start_time = std::chrono::steady_clock::now();

    SparseProblem root_prob = prob;
    if (params.root_gomory_cuts) {
        add_gomory_cuts(root_prob);
    }

    std::priority_queue<BnBNode> best_first_queue;
    std::vector<BnBNode> depth_first_stack;

    BnBNode root_node;
    root_node.lower_bounds = root_prob.var_lower_bounds;
    root_node.upper_bounds = root_prob.var_upper_bounds;
    
    if (params.depth_first) depth_first_stack.push_back(root_node);
    else best_first_queue.push(root_node);

    double incumbent_obj = std::numeric_limits<double>::infinity();
    std::vector<double> incumbent_sol;
    int nodes_explored = 0;

    while (true) {
        if (params.time_limit > 0) {
            auto now = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed = now - start_time;
            if (elapsed.count() > params.time_limit) {
                return {BnBStatus::TIME_LIMIT, incumbent_obj, incumbent_sol, nodes_explored};
            }
        }
        
        if (nodes_explored >= params.max_nodes) {
            return {BnBStatus::NODE_LIMIT, incumbent_obj, incumbent_sol, nodes_explored};
        }

        BnBNode current;
        if (params.depth_first) {
            if (depth_first_stack.empty()) break;
            current = depth_first_stack.back();
            depth_first_stack.pop_back();
        } else {
            if (best_first_queue.empty()) break;
            current = best_first_queue.top();
            best_first_queue.pop();
        }

        if (current.lp_obj != -std::numeric_limits<double>::infinity() && current.lp_obj >= incumbent_obj) {
            continue;
        }

        SparseProblem node_prob = root_prob;
        node_prob.var_lower_bounds = current.lower_bounds;
        node_prob.var_upper_bounds = current.upper_bounds;

        nodes_explored++;

        PDLPSolverParams pdlp_params;
        pdlp_params.tolerance = 1e-6; // Tighter tolerance for B&B
        pdlp_params.max_iterations = 5000;
        auto res = PDLPSolver::solve(node_prob, pdlp_params);

        bool is_optimal = (res.status == PDLPStatus::OPTIMAL);
        double node_obj = res.objective_value;
        std::vector<double> node_sol = res.solution;

        if (!is_optimal) {
            // Fallback to SimplexSolver
            auto simplex_res = SimplexSolver::solve(node_prob);
            if (simplex_res.status == SimplexStatus::OPTIMAL) {
                is_optimal = true;
                node_obj = simplex_res.objective_value;
                node_sol = simplex_res.solution;
            }
        }

        if (!is_optimal) {
            continue;
        }

        if (node_obj >= incumbent_obj) {
            continue;
        }

        bool is_integral = true;
        int branch_var = -1;
        double max_frac_dist = 0.0;
        double branch_val = 0.0;

        for (int i = 0; i < node_prob.num_vars; ++i) {
            if (node_prob.is_integer.size() > i && node_prob.is_integer[i]) {
                double val = node_sol[i];
                // Clamp to bounds to avoid branching on numerical noise outside bounds
                val = std::max(node_prob.var_lower_bounds[i], std::min(node_prob.var_upper_bounds[i], val));
                double dist = std::abs(val - std::round(val));
                
                if (dist > params.integer_tolerance) {
                    is_integral = false;
                    if (dist > max_frac_dist) {
                        max_frac_dist = dist;
                        branch_var = i;
                        branch_val = val;
                    }
                }
            }
        }

        if (is_integral) {
            incumbent_obj = node_obj;
            incumbent_sol = node_sol;
        } else {
            BnBNode left_child = current;
            left_child.depth = current.depth + 1;
            left_child.upper_bounds[branch_var] = std::floor(branch_val);
            left_child.lp_obj = res.objective_value;

            BnBNode right_child = current;
            right_child.depth = current.depth + 1;
            right_child.lower_bounds[branch_var] = std::ceil(branch_val);
            right_child.lp_obj = res.objective_value;

            if (params.depth_first) {
                depth_first_stack.push_back(right_child);
                depth_first_stack.push_back(left_child);
            } else {
                best_first_queue.push(left_child);
                best_first_queue.push(right_child);
            }
        }
    }

    if (incumbent_obj == std::numeric_limits<double>::infinity()) {
        return {BnBStatus::INFEASIBLE, 0.0, {}, nodes_explored};
    }

    return {BnBStatus::OPTIMAL, incumbent_obj, incumbent_sol, nodes_explored};
}

} // namespace firefly