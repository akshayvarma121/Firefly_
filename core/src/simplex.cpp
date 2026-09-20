#include "firefly/simplex.h"
#include <Eigen/Sparse>
#include <Eigen/SparseLU>
#include <chrono>
#include <iostream>
#include <cmath>

namespace firefly {

using SpMat = Eigen::SparseMatrix<double, Eigen::ColMajor>;

enum class Status { BASIC, AT_LOWER, AT_UPPER, FREE, FIXED };

struct BoundedSimplex {
    size_t M, N;
    SpMat A;
    std::vector<double> L, U, c, x;
    std::vector<Status> status;
    std::vector<size_t> basis;
    SimplexOptions options;
    
    SolveStatus solve(size_t& iterations, std::vector<double>& duals) {
        if (M == 0) {
            // No constraints. Check for unboundedness and set variables to bounds.
            for (size_t j = 0; j < N; ++j) {
                if (status[j] == Status::FIXED) continue;
                if (c[j] < -options.tolerance) {
                    if (std::isinf(U[j])) return SolveStatus::UNBOUNDED;
                    x[j] = U[j];
                    status[j] = Status::AT_UPPER;
                } else if (c[j] > options.tolerance) {
                    // Note: minimizing c^T x. So if c[j] > 0, we want x[j] as small as possible
                    if (std::isinf(L[j])) return SolveStatus::UNBOUNDED;
                    x[j] = L[j];
                    status[j] = Status::AT_LOWER;
                } else {
                    if (!std::isinf(L[j])) { x[j] = L[j]; status[j] = Status::AT_LOWER; }
                    else if (!std::isinf(U[j])) { x[j] = U[j]; status[j] = Status::AT_UPPER; }
                    else { x[j] = 0; status[j] = Status::FREE; }
                }
            }
            iterations = 0;
            duals.assign(M, 0.0);
            return SolveStatus::OPTIMAL;
        }

        Eigen::SparseLU<SpMat, Eigen::COLAMDOrdering<int>> solver;
        
        for (size_t iter = 0; iter < options.max_iterations; ++iter) {
            // 1. Extract B
            std::vector<Eigen::Triplet<double>> B_triplets;
            B_triplets.reserve(M * 10);
            for (size_t i = 0; i < M; ++i) {
                size_t j = basis[i];
                for (SpMat::InnerIterator it(A, j); it; ++it) {
                    B_triplets.push_back({(int)it.row(), (int)i, it.value()});
                }
            }
            SpMat B(M, M);
            B.setFromTriplets(B_triplets.begin(), B_triplets.end());
            
            solver.analyzePattern(B);
            solver.factorize(B);
            if (solver.info() != Eigen::Success) {
                throw std::runtime_error("Simplex: SparseLU factorization failed (singular basis).");
            }
            
            // 2. Solve B^T y = c_B
            Eigen::VectorXd c_B(M);
            for(size_t i = 0; i < M; ++i) c_B[i] = c[basis[i]];
            
            Eigen::VectorXd y = solver.transpose().solve(c_B);
            
            // 3. Pricing (Bland's Rule)
            int entering_var = -1;
            
            for (size_t j = 0; j < N; ++j) {
                if (status[j] == Status::BASIC || status[j] == Status::FIXED) continue;
                
                double z_j = 0;
                for (SpMat::InnerIterator it(A, j); it; ++it) {
                    z_j += it.value() * y[it.row()];
                }
                double c_j = c[j] - z_j;
                
                if (status[j] == Status::AT_LOWER || status[j] == Status::FREE) {
                    if (c_j < -options.tolerance) {
                        entering_var = j;
                        break;
                    }
                }
                if (status[j] == Status::AT_UPPER || status[j] == Status::FREE) {
                    if (c_j > options.tolerance) {
                        entering_var = j;
                        break;
                    }
                }
            }
            
            if (entering_var == -1) {
                // Optimal! Extract duals
                duals.assign(y.data(), y.data() + M);
                iterations = iter;
                return SolveStatus::OPTIMAL;
            }
            
            // 4. Search Direction
            Eigen::VectorXd A_e(M);
            A_e.setZero();
            for (SpMat::InnerIterator it(A, entering_var); it; ++it) {
                A_e[it.row()] = it.value();
            }
            Eigen::VectorXd d = solver.solve(A_e);
            
            // 5. Ratio Test
            double z_e = 0;
            for (SpMat::InnerIterator it(A, entering_var); it; ++it) z_e += it.value() * y[it.row()];
            double red_cost = c[entering_var] - z_e;
            
            int step_sign = (red_cost < 0) ? 1 : -1; 
            
            double max_step = INFINITY;
            int leaving_row = -1;
            
            if (step_sign > 0) {
                if (!std::isinf(U[entering_var])) max_step = U[entering_var] - x[entering_var];
            } else {
                if (!std::isinf(L[entering_var])) max_step = x[entering_var] - L[entering_var];
            }
            if (max_step < 0.0) max_step = 0.0;
            
            for (size_t i = 0; i < M; ++i) {
                size_t j = basis[i];
                double d_i = d[i] * step_sign;
                
                if (d_i > options.tolerance) {
                    if (!std::isinf(L[j])) {
                        double step = (x[j] - L[j]) / d_i;
                        if (step < 0.0) step = 0.0;
                        
                        if (step < max_step - options.tolerance) {
                            max_step = step;
                            leaving_row = i;
                        } else if (std::abs(step - max_step) <= options.tolerance) {
                            if (leaving_row == -1 || j < basis[leaving_row]) {
                                max_step = step;
                                leaving_row = i;
                            }
                        }
                    }
                } else if (d_i < -options.tolerance) {
                    if (!std::isinf(U[j])) {
                        double step = (U[j] - x[j]) / (-d_i);
                        if (step < 0.0) step = 0.0;
                        
                        if (step < max_step - options.tolerance) {
                            max_step = step;
                            leaving_row = i;
                        } else if (std::abs(step - max_step) <= options.tolerance) {
                            if (leaving_row == -1 || j < basis[leaving_row]) {
                                max_step = step;
                                leaving_row = i;
                            }
                        }
                    }
                }
            }
            
            if (std::isinf(max_step)) {
                iterations = iter;
                return SolveStatus::UNBOUNDED;
            }
            
            // 6. Update Primal
            x[entering_var] += max_step * step_sign;
            for (size_t i = 0; i < M; ++i) {
                x[basis[i]] -= max_step * step_sign * d[i];
                // Clamp to prevent drift
                if (!std::isinf(L[basis[i]]) && x[basis[i]] < L[basis[i]] + options.tolerance && x[basis[i]] > L[basis[i]] - options.tolerance) x[basis[i]] = L[basis[i]];
                if (!std::isinf(U[basis[i]]) && x[basis[i]] < U[basis[i]] + options.tolerance && x[basis[i]] > U[basis[i]] - options.tolerance) x[basis[i]] = U[basis[i]];
            }
            if (!std::isinf(L[entering_var]) && x[entering_var] < L[entering_var] + options.tolerance && x[entering_var] > L[entering_var] - options.tolerance) x[entering_var] = L[entering_var];
            if (!std::isinf(U[entering_var]) && x[entering_var] < U[entering_var] + options.tolerance && x[entering_var] > U[entering_var] - options.tolerance) x[entering_var] = U[entering_var];
            
            // 7. Update Basis
            if (leaving_row == -1) {
                // Bound flip
                if (status[entering_var] == Status::AT_LOWER) status[entering_var] = Status::AT_UPPER;
                else if (status[entering_var] == Status::AT_UPPER) status[entering_var] = Status::AT_LOWER;
            } else {
                size_t leaving_var = basis[leaving_row];
                basis[leaving_row] = entering_var;
                status[entering_var] = Status::BASIC;
                
                if (L[leaving_var] == U[leaving_var]) {
                    status[leaving_var] = Status::FIXED;
                } else if (std::abs(x[leaving_var] - L[leaving_var]) < std::abs(x[leaving_var] - U[leaving_var])) {
                    status[leaving_var] = Status::AT_LOWER;
                    x[leaving_var] = L[leaving_var];
                } else {
                    status[leaving_var] = Status::AT_UPPER;
                    x[leaving_var] = U[leaving_var];
                }
            }
        }
        
        throw std::runtime_error("Simplex: Max iterations reached.");
    }
};

SolveResult SimplexSolver::solve(const PresolvedProblem& pre, const SimplexOptions& options) {
    SolveResult result;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    size_t M = pre.stats.reduced_rows;
    size_t N_orig = pre.stats.reduced_cols;
    size_t N_slack = M;
    size_t N_art = M;
    size_t N_total = N_orig + N_slack + N_art;
    
    BoundedSimplex sim;
    sim.M = M;
    sim.N = N_total;
    sim.options = options;
    
    sim.L.assign(N_total, -INFINITY);
    sim.U.assign(N_total, INFINITY);
    sim.c.assign(N_total, 0.0);
    sim.x.assign(N_total, 0.0);
    sim.status.assign(N_total, Status::FREE);
    sim.basis.assign(M, 0);
    
    std::vector<double> c1(N_total, 0.0);
    std::vector<double> c2(N_total, 0.0);
    
    std::vector<Eigen::Triplet<double>> A_triplets;
    
    // 1. Original Variables
    for (size_t j = 0; j < N_orig; ++j) {
        sim.L[j] = pre.problem.col_lower_bounds[j];
        sim.U[j] = pre.problem.col_upper_bounds[j];
        c2[j] = pre.problem.objective[j];
        
        if (sim.L[j] == sim.U[j]) {
            sim.x[j] = sim.L[j];
            sim.status[j] = Status::FIXED;
        } else if (!std::isinf(sim.L[j])) {
            sim.x[j] = sim.L[j];
            sim.status[j] = Status::AT_LOWER;
        } else if (!std::isinf(sim.U[j])) {
            sim.x[j] = sim.U[j];
            sim.status[j] = Status::AT_UPPER;
        } else {
            sim.x[j] = 0.0;
            sim.status[j] = Status::FREE;
        }
    }
    
    for (const auto& trip : pre.problem.matrix) {
        A_triplets.push_back({(int)trip.row, (int)trip.col, trip.value});
    }
    
    // 2. Slacks
    std::vector<double> s_eval(M, 0.0);
    for (const auto& trip : pre.problem.matrix) {
        s_eval[trip.row] += trip.value * sim.x[trip.col];
    }
    
    for (size_t i = 0; i < M; ++i) {
        size_t j = N_orig + i;
        sim.L[j] = pre.problem.row_lower_bounds[i];
        sim.U[j] = pre.problem.row_upper_bounds[i];
        A_triplets.push_back({(int)i, (int)j, -1.0});
        
        // Phase 1 Setup
        size_t a = N_orig + N_slack + i; // artificial
        sim.L[a] = 0.0;
        sim.U[a] = INFINITY;
        
        if (s_eval[i] < sim.L[j] - options.tolerance) {
            sim.x[j] = sim.L[j];
            sim.status[j] = Status::AT_LOWER;
            
            sim.x[a] = sim.L[j] - s_eval[i];
            sim.status[a] = Status::BASIC;
            sim.basis[i] = a;
            c1[a] = 1.0;
            A_triplets.push_back({(int)i, (int)a, 1.0});
        } else if (s_eval[i] > sim.U[j] + options.tolerance) {
            sim.x[j] = sim.U[j];
            sim.status[j] = Status::AT_UPPER;
            
            sim.x[a] = s_eval[i] - sim.U[j];
            sim.status[a] = Status::BASIC;
            sim.basis[i] = a;
            c1[a] = 1.0;
            A_triplets.push_back({(int)i, (int)a, -1.0});
        } else {
            sim.x[j] = s_eval[i];
            sim.status[j] = Status::BASIC;
            sim.basis[i] = j;
            
            sim.x[a] = 0.0;
            sim.status[a] = Status::AT_LOWER;
            sim.L[a] = 0.0; sim.U[a] = 0.0; // lock it
        }
    }
    
    sim.A.resize(M, N_total);
    sim.A.setFromTriplets(A_triplets.begin(), A_triplets.end());
    
    // --- PHASE I ---
    sim.c = c1;
    std::vector<double> duals;
    try {
        SolveStatus s1 = sim.solve(result.phase1_iterations, duals);
        if (s1 == SolveStatus::UNBOUNDED) {
            throw std::runtime_error("Phase I Unbounded - This should be mathematically impossible.");
        }
        
        double p1_obj = 0.0;
        for (size_t i = 0; i < N_total; ++i) p1_obj += c1[i] * sim.x[i];
        
        if (p1_obj > options.tolerance) {
            result.status = SolveStatus::INFEASIBLE;
            return result;
        }
    } catch (const std::exception& e) {
        auto end_time_err = std::chrono::high_resolution_clock::now();
        result.solve_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time_err - start_time).count();
        result.status = SolveStatus::ERROR;
        result.message = std::string("Phase I Error: ") + e.what();
        return result;
    }
    
    // --- PHASE II ---
    sim.c = c2;
    // Lock remaining artificial variables
    for (size_t i = 0; i < M; ++i) {
        size_t a = N_orig + N_slack + i;
        sim.L[a] = 0.0;
        sim.U[a] = 0.0;
        if (sim.status[a] != Status::BASIC) {
            sim.status[a] = Status::FIXED;
            sim.x[a] = 0.0;
        }
    }
    
    try {
        SolveStatus s2 = sim.solve(result.phase2_iterations, duals);
        result.status = s2;
        
        if (s2 == SolveStatus::OPTIMAL) {
            double obj = pre.postsolve.objective_offset;
            for (size_t j = 0; j < N_orig; ++j) {
                obj += c2[j] * sim.x[j];
            }
            result.objective_value = obj;
            
            std::vector<double> red_primal(sim.x.begin(), sim.x.begin() + N_orig);
            result.primal_solution = Presolver::postsolve_primal(pre, red_primal);
            
            // Reconstruct duals
            result.dual_solution.assign(pre.stats.original_rows, 0.0);
            for (size_t i = 0; i < M; ++i) {
                size_t orig_row = pre.postsolve.reduced_row_to_orig_row[i];
                result.dual_solution[orig_row] = duals[i] / pre.postsolve.row_scale_factors[orig_row];
            }
            
            // Self-Check
            for (size_t i = 0; i < M; ++i) {
                double val = 0.0;
                for (const auto& trip : pre.problem.matrix) {
                    if (trip.row == i) val += trip.value * sim.x[trip.col];
                }
                if (val < pre.problem.row_lower_bounds[i] - options.tolerance || 
                    val > pre.problem.row_upper_bounds[i] + options.tolerance) {
                    throw std::runtime_error("Self-Check failed on reduced row " + std::to_string(i) + " : " + std::to_string(val) + " not in [" + std::to_string(pre.problem.row_lower_bounds[i]) + ", " + std::to_string(pre.problem.row_upper_bounds[i]) + "]");
                }
            }
        }
    } catch (const std::exception& e) {
        auto end_time_err = std::chrono::high_resolution_clock::now();
        result.solve_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time_err - start_time).count();
        result.status = SolveStatus::ERROR;
        result.message = std::string("Phase II Error: ") + e.what();
        return result;
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    result.solve_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    return result;
}

} // namespace firefly
