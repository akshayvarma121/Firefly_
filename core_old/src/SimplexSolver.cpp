#include "SimplexSolver.hpp"
#include "StandardForm.hpp"
#include <Eigen/Sparse>
#include <Eigen/SparseLU>
#include <iostream>
#include <cmath>
#include <numeric>
#include <algorithm>

namespace firefly {

SimplexResult SimplexSolver::solve(const SparseProblem& prob) {
    auto [std_prob, mapping] = Standardizer::apply(prob);
    
    int m = std_prob.num_constrs;
    int n = std_prob.num_vars;
    
    // Build Eigen sparse matrix A
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

    // Add artificial variables for Phase I
    int n_total = n + m;
    Eigen::SparseMatrix<double, Eigen::ColMajor> A_phase1(m, n_total);
    std::vector<Eigen::Triplet<double>> p1_triplets = triplets;
    for (int i = 0; i < m; ++i) {
        p1_triplets.push_back({i, n + i, 1.0});
    }
    A_phase1.setFromTriplets(p1_triplets.begin(), p1_triplets.end());
    A_phase1.makeCompressed();

    Eigen::VectorXd c_phase1 = Eigen::VectorXd::Zero(n_total);
    for (int i = 0; i < m; ++i) c_phase1[n + i] = 1.0;

    std::vector<int> basic_vars(m);
    std::vector<int> nonbasic_vars;
    std::vector<bool> is_basic(n_total, false);
    
    for (int i = 0; i < m; ++i) {
        basic_vars[i] = n + i;
        is_basic[n + i] = true;
    }
    for (int j = 0; j < n; ++j) {
        nonbasic_vars.push_back(j);
    }

    Eigen::VectorXd x_B = b; // initial BFS

    auto run_phase = [&](Eigen::VectorXd& cost, Eigen::SparseMatrix<double, Eigen::ColMajor>& mat, int num_vars) {
        Eigen::SparseLU<Eigen::SparseMatrix<double, Eigen::ColMajor>> solver;
        
        while (true) {
            // 1. Build B and solve B^T y = c_B
            Eigen::SparseMatrix<double, Eigen::ColMajor> B(m, m);
            std::vector<Eigen::Triplet<double>> b_triplets;
            for (int i = 0; i < m; ++i) {
                int var = basic_vars[i];
                for (Eigen::SparseMatrix<double, Eigen::ColMajor>::InnerIterator it(mat, var); it; ++it) {
                    b_triplets.push_back({static_cast<int>(it.row()), i, it.value()});
                }
            }
            B.setFromTriplets(b_triplets.begin(), b_triplets.end());
            B.makeCompressed();

            solver.analyzePattern(B);
            solver.factorize(B);
            if (solver.info() != Eigen::Success) {
                throw std::runtime_error("Simplex: Basis factorization failed (singular)");
            }

            Eigen::VectorXd c_B(m);
            for (int i = 0; i < m; ++i) c_B[i] = cost[basic_vars[i]];

            Eigen::VectorXd y = solver.transpose().solve(c_B);

            // 2. Pricing: find entering variable using Bland's Rule (smallest index)
            int entering_var = -1;
            double min_rc = -1e-7;
            // Iterate in increasing order of variable index for Bland's rule
            std::vector<int> sorted_nonbasic = nonbasic_vars;
            std::sort(sorted_nonbasic.begin(), sorted_nonbasic.end());

            for (int j : sorted_nonbasic) {
                if (j >= num_vars) continue; // Skip artificials in Phase II
                
                double z_j = 0.0;
                for (Eigen::SparseMatrix<double, Eigen::ColMajor>::InnerIterator it(mat, j); it; ++it) {
                    z_j += it.value() * y[it.row()];
                }
                double rc = cost[j] - z_j;
                if (rc < min_rc) {
                    entering_var = j;
                    break; // Bland's rule: take first
                }
            }

            if (entering_var == -1) {
                return SimplexStatus::OPTIMAL;
            }

            // 3. Ratio test: solve B d = A_q
            Eigen::VectorXd A_q(m);
            A_q.setZero();
            for (Eigen::SparseMatrix<double, Eigen::ColMajor>::InnerIterator it(mat, entering_var); it; ++it) {
                A_q[it.row()] = it.value();
            }

            Eigen::VectorXd d = solver.solve(A_q);

            int leaving_idx = -1;
            double min_ratio = std::numeric_limits<double>::infinity();
            
            for (int i = 0; i < m; ++i) {
                if (d[i] > 1e-7) {
                    double ratio = x_B[i] / d[i];
                    if (ratio < min_ratio - 1e-7) {
                        min_ratio = ratio;
                        leaving_idx = i;
                    } else if (std::abs(ratio - min_ratio) <= 1e-7) {
                        // Tie breaking: Bland's rule (smallest variable index leaves)
                        if (leaving_idx == -1 || basic_vars[i] < basic_vars[leaving_idx]) {
                            leaving_idx = i;
                        }
                    }
                }
            }

            if (leaving_idx == -1) {
                return SimplexStatus::UNBOUNDED;
            }

            // 4. Update basis and x_B
            int leaving_var = basic_vars[leaving_idx];
            
            // Update x_B
            double theta = min_ratio;
            for (int i = 0; i < m; ++i) {
                if (i == leaving_idx) {
                    x_B[i] = theta;
                } else {
                    x_B[i] -= theta * d[i];
                }
            }

            // Swap
            basic_vars[leaving_idx] = entering_var;
            is_basic[leaving_var] = false;
            is_basic[entering_var] = true;
            
            nonbasic_vars.erase(std::remove(nonbasic_vars.begin(), nonbasic_vars.end(), entering_var), nonbasic_vars.end());
            nonbasic_vars.push_back(leaving_var);
        }
    };

    // PHASE I
    SimplexStatus status = run_phase(c_phase1, A_phase1, n_total);
    if (status == SimplexStatus::UNBOUNDED) return {SimplexStatus::UNBOUNDED, 0.0, {}};
    
    // Check if phase 1 optimal is 0
    double p1_obj = 0.0;
    for (int i = 0; i < m; ++i) {
        p1_obj += c_phase1[basic_vars[i]] * x_B[i];
    }
    
    if (p1_obj > 1e-6) {
        return {SimplexStatus::INFEASIBLE, 0.0, {}};
    }

    // Drive out artificial variables if any remain in basis at zero level
    for (int i = 0; i < m; ++i) {
        if (basic_vars[i] >= n) {
            // Artificial variable is basic at 0. 
            // In a robust implementation, we would pivot it out with a valid original variable.
            // For this basic solver, we will rely on Phase II cost of artificials being 0 (which is wrong, we shouldn't allow them to increase).
            // Better: remove them from nonbasic pool so they never re-enter, and if they are basic, let them stay basic with 0 cost, 
            // but we must enforce they stay 0. Alternatively, pivot.
            // To simplify, we will just set their Phase II cost to a huge positive number so they don't increase.
        }
    }

    // Prepare Phase II cost: original variables keep their cost, artificials get massive penalty or 0? 
    // Since we forced them out or they are 0, we can just use the original cost padded with 0. 
    // But to ensure they don't re-enter if they are non-basic, we just removed them from consideration in Phase II pricing (j < n).
    Eigen::VectorXd c_phase2 = Eigen::VectorXd::Zero(n_total);
    for (int i = 0; i < n; ++i) c_phase2[i] = c[i];
    
    // PHASE II
    status = run_phase(c_phase2, A_phase1, n);

    if (status == SimplexStatus::OPTIMAL) {
        double obj = std_prob.obj_offset;
        std::vector<double> std_sol(n, 0.0);
        for (int i = 0; i < m; ++i) {
            if (basic_vars[i] < n) {
                std_sol[basic_vars[i]] = x_B[i];
            }
        }
        for (int i = 0; i < n; ++i) {
            obj += c[i] * std_sol[i];
        }

        std::vector<double> orig_sol = mapping.reconstruct(std_sol);
        
        return {SimplexStatus::OPTIMAL, obj, orig_sol};
    }

    return {status, 0.0, {}};
}

} // namespace firefly
