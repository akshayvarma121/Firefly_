#include "firefly/branch_and_bound.h"
#include "firefly/presolve.h"
#include "firefly/pdlp.h"
#include <chrono>
#include <queue>
#include <vector>
#include <cmath>
#include <iostream>
#include <algorithm>

namespace firefly {

struct BBNode {
    std::vector<double> col_lower_bounds;
    std::vector<double> col_upper_bounds;
    double lower_bound = -INFINITY;
    size_t depth = 0;

    bool operator<(const BBNode& other) const {
        return lower_bound > other.lower_bound; // Min-heap based on lower bound
    }
};

BBSolverResult BranchAndBoundSolver::solve(const Problem& prob, const BBSolverOptions& options) {
    // std::cout << "BBSolver::solve start" << std::endl;
    BBSolverResult result;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    double best_incumbent = INFINITY;
    std::vector<double> best_solution;
    
    std::priority_queue<BBNode> pq;
    std::vector<BBNode> stack; // Vector used as stack to allow iteration for best_bound
    
    BBNode root;
    root.col_lower_bounds = prob.col_lower_bounds;
    root.col_upper_bounds = prob.col_upper_bounds;
    root.depth = 0;
    
    if (options.ordering == NodeOrdering::BEST_FIRST) {
        pq.push(root);
    } else {
        stack.push_back(root);
    }
    
    size_t node_count = 0;
    double total_presolve_ms = 0;
    double total_lp_ms = 0;
    result.best_bound = -INFINITY;

    while (true) {
        bool empty = (options.ordering == NodeOrdering::BEST_FIRST) ? pq.empty() : stack.empty();
        if (empty) break;
        
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        if (elapsed_ms > options.time_limit_ms) {
            result.status = (best_incumbent < INFINITY) ? SolveStatus::FEASIBLE : SolveStatus::TIME_LIMIT;
            break;
        }
        
        if (node_count >= options.node_limit) {
            result.status = (best_incumbent < INFINITY) ? SolveStatus::FEASIBLE : SolveStatus::NODE_LIMIT;
            break;
        }
        
        BBNode current;
        if (options.ordering == NodeOrdering::BEST_FIRST) {
            current = pq.top();
            pq.pop();
            result.best_bound = current.lower_bound;
        } else {
            current = stack.back();
            stack.pop_back();
            double min_bound = current.lower_bound;
            for (const auto& n : stack) {
                if (n.lower_bound < min_bound) min_bound = n.lower_bound;
            }
            result.best_bound = min_bound;
        }
        
        node_count++;
        
        if (current.lower_bound >= best_incumbent - options.obj_tol) continue;
        
        Problem node_prob = prob;
        node_prob.col_lower_bounds = current.col_lower_bounds;
        node_prob.col_upper_bounds = current.col_upper_bounds;
        
        auto presolve_start = std::chrono::high_resolution_clock::now();
        PresolvedProblem pre;
        bool infeasible = false;
        try {
            pre = Presolver::presolve(node_prob);
        } catch (const InfeasibleProblemException&) {
            infeasible = true;
        }
        auto presolve_end = std::chrono::high_resolution_clock::now();
        total_presolve_ms += std::chrono::duration_cast<std::chrono::milliseconds>(presolve_end - presolve_start).count();
        
        if (infeasible) continue;
        
        SolveResult lp_res;
        if (pre.stats.reduced_cols == 0) {
            lp_res.status = SolveStatus::OPTIMAL;
            lp_res.objective_value = pre.postsolve.objective_offset;
            lp_res.primal_solution = Presolver::postsolve_primal(pre, {});
        } else if (options.lp_solver == LPSolverChoice::PDLP) {
            PDLPOptions pdlp_opt;
            pdlp_opt.tolerance = options.obj_tol; 
            lp_res = PDLPSolver::solve(pre, pdlp_opt);
        } else {
            SimplexOptions simp_opt;
            simp_opt.tolerance = options.obj_tol;
            lp_res = SimplexSolver::solve(pre, simp_opt);
        }
        // std::cout << "LP solve done" << std::endl;
        
        auto lp_end = std::chrono::high_resolution_clock::now();
        total_lp_ms += std::chrono::duration_cast<std::chrono::milliseconds>(lp_end - presolve_end).count();
        
        if (lp_res.status == SolveStatus::INFEASIBLE) {
            continue; 
        }
        
        if (lp_res.status == SolveStatus::UNBOUNDED) {
            if (current.depth == 0) {
                result.status = SolveStatus::UNBOUNDED;
                break;
            }
            continue;
        }
        
        if (lp_res.status != SolveStatus::OPTIMAL) {
            continue;
        }
        
        if (lp_res.objective_value >= best_incumbent - options.obj_tol) {
            continue; 
        }
        
        double max_fractionality = -1.0;
        int branch_var = -1;
        double branch_val = 0.0;
        
        for (size_t j = 0; j < prob.col_names.size(); ++j) {
            if (prob.is_integer[j]) {
                double val = lp_res.primal_solution[j];
                double fractional_part = std::abs(val - std::round(val));
                if (fractional_part > options.integrality_tol) {
                    double dist_to_half = 0.5 - std::abs(fractional_part - 0.5); 
                    if (dist_to_half > max_fractionality) {
                        max_fractionality = dist_to_half;
                        branch_var = (int)j;
                        branch_val = val;
                    }
                }
            }
        }
        
        if (branch_var == -1) {
            if (lp_res.objective_value < best_incumbent) {
                best_incumbent = lp_res.objective_value;
                best_solution = lp_res.primal_solution;
            }
        } else {
            BBNode left = current;
            left.col_upper_bounds[branch_var] = std::floor(branch_val);
            left.lower_bound = lp_res.objective_value;
            left.depth = current.depth + 1;
            
            BBNode right = current;
            right.col_lower_bounds[branch_var] = std::ceil(branch_val);
            right.lower_bound = lp_res.objective_value;
            right.depth = current.depth + 1;
            
            if (options.ordering == NodeOrdering::BEST_FIRST) {
                pq.push(left);
                pq.push(right);
            } else {
                stack.push_back(right);
                stack.push_back(left);
            }
        }
        
        if (options.progress_callback && node_count % options.callback_frequency == 0) {
            auto iter_now = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration_cast<std::chrono::milliseconds>(iter_now - start_time).count();
            options.progress_callback(node_count, best_incumbent, result.best_bound, ms);
        }
    }
    
    if (options.ordering == NodeOrdering::BEST_FIRST) {
        result.best_bound = pq.empty() ? best_incumbent : pq.top().lower_bound;
    } else {
        double min_bound = best_incumbent;
        for (const auto& n : stack) {
            if (n.lower_bound < min_bound) min_bound = n.lower_bound;
        }
        result.best_bound = min_bound;
    }

    if (result.status != SolveStatus::TIME_LIMIT && 
        result.status != SolveStatus::NODE_LIMIT && 
        result.status != SolveStatus::UNBOUNDED) {
        if (best_incumbent < INFINITY) {
            result.status = SolveStatus::OPTIMAL;
        } else {
            result.status = SolveStatus::INFEASIBLE;
        }
    }
    
    result.node_count = node_count;
    if (node_count > 0) {
        result.avg_presolve_ms_per_node = total_presolve_ms / node_count;
        result.avg_lp_solve_ms_per_node = total_lp_ms / node_count;
    }
    
    if (best_incumbent < INFINITY) {
        result.objective_value = best_incumbent;
        result.primal_solution = best_solution;
        
        for (size_t i = 0; i < prob.row_names.size(); ++i) {
            double val = 0.0;
            for (const auto& t : prob.matrix) {
                if (t.row == i) val += t.value * result.primal_solution[t.col];
            }
            if (val < prob.row_lower_bounds[i] - options.obj_tol || val > prob.row_upper_bounds[i] + options.obj_tol) {
                throw std::runtime_error("B&B Self-Check failed on constraint " + prob.row_names[i]);
            }
        }
        for (size_t j = 0; j < prob.col_names.size(); ++j) {
            if (prob.is_integer[j]) {
                double val = result.primal_solution[j];
                if (std::abs(val - std::round(val)) > options.integrality_tol) {
                    throw std::runtime_error("B&B Self-Check failed on integrality of " + prob.col_names[j]);
                }
            }
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    result.solve_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    return result;
}

} // namespace firefly
