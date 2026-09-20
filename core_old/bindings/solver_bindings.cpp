#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include "SparseProblem.hpp"
#include "MPSParser.hpp"
#include "SimplexSolver.hpp"
#include "PDLPSolver.hpp"
#include "BranchAndBoundSolver.hpp"
#include <chrono>

namespace py = pybind11;

namespace firefly {

struct SolveResult {
    std::string status;
    double objective;
    std::vector<double> solution;
    double wall_time_ms;
    int iterations;
};

SolveResult solve(const SparseProblem& problem, const std::string& method, bool gpu, py::object iteration_callback) {
    auto start_time = std::chrono::steady_clock::now();

    std::string active_method = method;
    if (active_method == "auto") {
        bool has_int = false;
        for (bool is_int : problem.is_integer) {
            if (is_int) { has_int = true; break; }
        }
        active_method = has_int ? "milp" : "pdlp";
    }

    SolveResult result;
    result.iterations = 0;

    PDLPCallback cpp_callback = nullptr;
    if (!iteration_callback.is_none()) {
        auto cb = iteration_callback.cast<std::function<void(int, double, double, double)>>();
        cpp_callback = [cb, start_time](int iter, double primal_obj, double dual_obj) {
            // Re-acquire GIL to call Python function
            py::gil_scoped_acquire acquire;
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double, std::milli>(now - start_time).count();
            cb(iter, primal_obj, dual_obj, elapsed);
        };
    }

    if (active_method == "simplex") {
        py::gil_scoped_release release;
        auto res = SimplexSolver::solve(problem);
        switch (res.status) {
            case SimplexStatus::OPTIMAL: result.status = "OPTIMAL"; break;
            case SimplexStatus::INFEASIBLE: result.status = "INFEASIBLE"; break;
            case SimplexStatus::UNBOUNDED: result.status = "UNBOUNDED"; break;
            case SimplexStatus::MAX_ITERATIONS: result.status = "MAX_ITERATIONS"; break;
        }
        result.objective = res.objective_value;
        result.solution = res.solution;
    } else if (active_method == "milp") {
        py::gil_scoped_release release;
        BnBParams params;
        auto res = BranchAndBoundSolver::solve(problem, params);
        switch (res.status) {
            case BnBStatus::OPTIMAL: result.status = "OPTIMAL"; break;
            case BnBStatus::INFEASIBLE: result.status = "INFEASIBLE"; break;
            case BnBStatus::TIME_LIMIT: result.status = "TIME_LIMIT"; break;
            case BnBStatus::NODE_LIMIT: result.status = "NODE_LIMIT"; break;
        }
        result.objective = res.objective_value;
        result.solution = res.solution;
        result.iterations = res.nodes_explored;
    } else { // pdlp is default for continuous
        py::gil_scoped_release release;
        PDLPSolverParams params;
        params.force_cpu = !gpu;
        auto res = PDLPSolver::solve(problem, params, cpp_callback);
        switch (res.status) {
            case PDLPStatus::OPTIMAL: result.status = "OPTIMAL"; break;
            case PDLPStatus::INFEASIBLE: result.status = "INFEASIBLE"; break;
            case PDLPStatus::UNBOUNDED: result.status = "UNBOUNDED"; break;
            case PDLPStatus::MAX_ITERATIONS: result.status = "MAX_ITERATIONS"; break;
        }
        result.objective = res.objective_value;
        result.solution = res.solution;
        result.iterations = res.iterations;
    }

    auto end_time = std::chrono::steady_clock::now();
    result.wall_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    return result;
}

PYBIND11_MODULE(firefly_solver, m) {
    m.doc() = "Firefly GPU-accelerated optimization solver core";

    py::class_<SparseProblem>(m, "SparseProblem")
        .def(py::init<>())
        .def_readwrite("num_vars", &SparseProblem::num_vars)
        .def_readwrite("num_constrs", &SparseProblem::num_constrs)
        .def_readwrite("name", &SparseProblem::name)
        .def_readwrite("obj_coeffs", &SparseProblem::obj_coeffs)
        .def_readwrite("obj_offset", &SparseProblem::obj_offset)
        .def_readwrite("row_ptr", &SparseProblem::row_ptr)
        .def_readwrite("col_idx", &SparseProblem::col_idx)
        .def_readwrite("values", &SparseProblem::values)
        .def_readwrite("row_senses", &SparseProblem::row_senses)
        .def_readwrite("rhs", &SparseProblem::rhs)
        .def_readwrite("var_lower_bounds", &SparseProblem::var_lower_bounds)
        .def_readwrite("var_upper_bounds", &SparseProblem::var_upper_bounds)
        .def_readwrite("is_integer", &SparseProblem::is_integer);

    py::class_<SolveResult>(m, "SolveResult")
        .def_readonly("status", &SolveResult::status)
        .def_readonly("objective", &SolveResult::objective)
        .def_readonly("solution", &SolveResult::solution)
        .def_readonly("wall_time_ms", &SolveResult::wall_time_ms)
        .def_readonly("iterations", &SolveResult::iterations);

    m.def("parse_mps", &MPSParser::parse_file, "Parse an MPS file into a SparseProblem");
    m.def("parse_mps_string", &MPSParser::parse_string, "Parse an MPS string into a SparseProblem");

    m.def("solve", &solve, "Solve the given SparseProblem",
          py::arg("problem"),
          py::arg("method") = "auto",
          py::arg("gpu") = true,
          py::arg("iteration_callback") = py::none());
}

} // namespace firefly
