// probe.cpp — throwaway pybind11 smoke-test module
// Validates: scikit-build-core + MSVC + pybind11 + editable install pipeline.
// Delete this directory once the real bindings (core/bindings/) are confirmed working.

#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(firefly_probe, m) {
    m.doc() = "Firefly build-pipeline smoke-test (not a real solver module)";

    m.def(
        "add",
        [](double a, double b) { return a + b; },
        py::arg("a"),
        py::arg("b"),
        "Return a + b. Used only to confirm the pybind11/scikit-build-core pipeline works."
    );
}
