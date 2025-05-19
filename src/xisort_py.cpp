// AUTHOR: FARUK ALPAY
// ORCID: 0009-0009-2207-6528
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
namespace py = pybind11;
// Forward declarations for XiSort
struct XiSortConfig {
    bool external;
    bool trace;
    bool parallel;
    std::size_t mem_limit;
    std::size_t buffer_elems;
};
void xi_sort(double* data, uint64_t n, const XiSortConfig& cfg);
// Python wrapper function for xi_sort
py::array_t<double> xi_sort_py(py::array_t<double> arr,
                               bool external=false, bool trace=false,
                               bool parallel=false,
                               std::size_t mem_limit=SIZE_MAX,
                               std::size_t buffer_elems=(1ULL<<15)) {
    // Extract raw pointer to NumPy array data (C++ double*)
    auto buf = arr.request();
    if(buf.ndim != 1) {
        throw std::runtime_error("xi_sort_py: Only 1-dimensional arrays are supported");
    }
    if(buf.strides[0] != sizeof(double)) {
        throw std::runtime_error("xi_sort_py: Array must be contiguous in memory");
    }
    double* data = static_cast<double*>(buf.ptr);
    uint64_t n = static_cast<uint64_t>(buf.shape[0]);
    // XiSortConfig from function arguments
    XiSortConfig cfg;
    cfg.external = external;
    cfg.trace = trace;
    cfg.parallel = parallel;
    cfg.mem_limit = mem_limit;
    cfg.buffer_elems = buffer_elems;
    //xi_sort to perform in-place sorting
    xi_sort(data, n, cfg);
    // Return the sorted array (same object as input)
    return arr;
}
// pybind11 module definition
PYBIND11_MODULE(xisort, m) {
    m.doc() = "XiSort Python binding";
    m.def("xi_sort_py", &xi_sort_py,
          py::arg("arr"), py::arg("external")=false, py::arg("trace")=false,
          py::arg("parallel")=false, py::arg("mem_limit")=SIZE_MAX,
          py::arg("buffer_elems")=(1ULL<<15));
}
