#include "PDLPSolver.hpp"
#include "StandardForm.hpp"
#include <Eigen/Sparse>
#include <iostream>
#include <cmath>
#include <algorithm>

namespace firefly::internal {

double estimate_norm_power_iteration(const Eigen::SparseMatrix<double, Eigen::ColMajor>& A, int iters = 20) {
    int n = A.cols();
    Eigen::VectorXd x = Eigen::VectorXd::Random(n);
    x.normalize();
    for (int i = 0; i < iters; ++i) {
        Eigen::VectorXd y = A * x;
        x = A.transpose() * y;
        if (x.norm() > 1e-12) {
            x.normalize();
        }
    }
    return (A * x).norm();
}

PDLPResult pdlp_cpu_solve(const SparseProblem& prob, const PDLPSolverParams& params, PDLPCallback cb) {
    auto [std_prob, mapping] = Standardizer::apply(prob);

    int m = std_prob.num_constrs;
    int n = std_prob.num_vars;

    if (m == 0 || n == 0) return {PDLPStatus::INFEASIBLE, 0.0, {}, 0};

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

    double norm_A = estimate_norm_power_iteration(A);
    if (norm_A < 1e-12) norm_A = 1.0;

    double eta = 1.0 / norm_A;
    double omega = 1.0; // Primal weight
    double tau = eta * omega;
    double sigma = eta / omega;

    Eigen::VectorXd x = Eigen::VectorXd::Zero(n);
    Eigen::VectorXd y = Eigen::VectorXd::Zero(m);
    
    for (int iter = 0; iter < params.max_iterations; ++iter) {
        Eigen::VectorXd x_prev = x;
        
        // Primal step: x_{k+1} = proj_{x >= 0} (x_k - tau * (c - A^T y_k))
        Eigen::VectorXd A_T_y = A.transpose() * y;
        Eigen::VectorXd grad_x = c - A_T_y;
        x = x - tau * grad_x;
        x = x.cwiseMax(0.0);
        
        // Dual step: y_{k+1} = y_k + sigma * (A (2x_{k+1} - x_k) - b)
        Eigen::VectorXd x_ext = 2.0 * x - x_prev;
        Eigen::VectorXd A_x_ext = A * x_ext;
        y = y + sigma * (b - A_x_ext);

        if (iter % params.report_frequency == 0 || iter == params.max_iterations - 1) {
            double primal_obj = std_prob.obj_offset + c.dot(x);
            double dual_obj = std_prob.obj_offset + b.dot(y);
            
            Eigen::VectorXd primal_res = A * x - b;
            Eigen::VectorXd dual_res = c - A.transpose() * y;
            double p_err = primal_res.norm() / (1.0 + b.norm());
            
            // Only negative dual residuals violate KKT for x>=0
            Eigen::VectorXd dual_viol = dual_res.cwiseMin(0.0);
            double d_err = dual_viol.norm() / (1.0 + c.norm());

            double gap = std::abs(primal_obj - dual_obj) / (1.0 + std::abs(primal_obj) + std::abs(dual_obj));

            if (cb) cb(iter, primal_obj, dual_obj);

            if (p_err < params.tolerance && d_err < params.tolerance && gap < params.tolerance) {
                std::vector<double> std_sol(x.data(), x.data() + x.size());
                std::vector<double> orig_sol = mapping.reconstruct(std_sol);
                return {PDLPStatus::OPTIMAL, primal_obj, orig_sol, iter};
            }
        }
    }

    std::vector<double> std_sol(x.data(), x.data() + x.size());
    std::vector<double> orig_sol = mapping.reconstruct(std_sol);
    return {PDLPStatus::MAX_ITERATIONS, std_prob.obj_offset + c.dot(x), orig_sol, params.max_iterations};
}

} // namespace firefly::internal
