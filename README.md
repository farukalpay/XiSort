# ΞSort – Deterministic IEEE-754 Float Sorting

*External and in-memory sorting of billions of floating-point values with full IEEE compliance, deterministic tie-breaking, and optional curvature tracing.*

[![ORCID](https://img.shields.io/badge/ORCID-0009--0009--2207--6528-brightgreen?logo=orcid&logoColor=white)](https://orcid.org/0009-0009-2207-6528)

---

## 1. Overview

ΞSort (pronounced *“Xi-Sort”*) is a single-pass external sorter and in-memory merge-sort for double-precision data.  
Key properties:

* **Total IEEE-754 order** – distinguishes ±0, NaN payloads, ±∞.  
* **Deterministic** – tie-breakers based on original index ensure reproducible output.  
* **Hybrid mode** – automatically chooses RAM or SSD pipeline depending on dataset size (`mem_limit`).  
* **Optional multi-core** – OpenMP tasks accelerate the recursive merge tree.  
* **Optional Φ(χ) curvature trace** – measures entropy reduction during merging (compile- and runtime-gated).

---

## 🚩 Why Use ΞSort?

| 🐢 Ordinary Sorting             | 🚀 **ΞSort**                      |
|:------------------------------:|:---------------------------------:|
| Memory crashes on big data     | ✅ Stream with low-RAM footprints  |
| Unstable NaN and ±0 order      | ✅ IEEE-safe total ordering        |
| Unpredictable tie outcomes     | ✅ Algebraic tie-break/determinism |
| Risk of silent corruption      | ✅ Blake3 cryptographic integrity  |

---

## 2. Build & Installation

### 2.1 Prerequisites  

* **C++17**-capable compiler (GCC ≥ 7, Clang ≥ 7, MSVC ≥ 19.14).  
* *(Optional)* **OpenMP** runtime (e.g. `libgomp`) for parallel mode.  
* *(Optional)* `pybind11` for the Python interface.

### 2.2 Native build (stand-alone CLI)  

```bash
g++ -std=c++17 -O3 -fopenmp \
    -DXISORT_CURVATURE_TRACE=1 \
    xisort.cpp -o xisort
````

Omit `-fopenmp` if OpenMP is unavailable; omit `-DXISORT_CURVATURE_TRACE=1` to remove tracing entirely.

### 2.3 Python module

```bash
c++ -O3 -std=c++17 -fPIC -shared -fopenmp \
    -DXISORT_CURVATURE_TRACE=1 \
    $(python3 -m pybind11 --includes) \
    xisort.cpp xisort_py.cpp \
    -o xisort$(python3-config --extension-suffix)
```

---

## 3. Configuration Flags

| Field          | Purpose                                                            | Typical setting for speed |
| -------------- | ------------------------------------------------------------------ | ------------------------- |
| `external`     | Force SSD/out-of-core path even if data fits into RAM.             | `false` (auto)            |
| `trace`        | Enable Φ(χ) curvature logging (requires compile macro above).      | `false`                   |
| `parallel`     | Activate OpenMP multi-threading.                                   | `true`                    |
| `mem_limit`    | Bytes of RAM allowed for an in-memory run before spilling to disk. | `1 GB` for NVMe tests     |
| `buffer_elems` | Elements cached per run during k-way merge (I/O block size).       | `32 768` (≈ 256 KiB)      |

---

## 4. Latest Benchmark Results

*(112 C/224 T Xeon-Platinum 8352V, 70 GB RAM, Intel NVMe; `-O3 -fopenmp`, `parallel=true`, `trace=false` unless noted.)*

| Test ID | Scenario / Size                            | Mode           | Runtime    | Status |
| ------- | ------------------------------------------ | -------------- | ---------- | ------ |
| **0**   | IEEE-754 edge-case vector (NaN, ±0, ±∞)    | In-mem         | < 1 ms     | ✅      |
| **1**   | Duplicate-heavy distribution, 10 M doubles | In-mem         | **0.48 s** | ✅      |
| **2**   | Std-normal, 100 M doubles                  | In-mem, OpenMP | **4.95 s** | ✅      |
| **3**   | External sort, 100 GB random               | SSD, 1 GB runs | **T B A**  | ▢      |

> *Test 3 will be repeated after the full 100 GB dataset is pre-generated to remove disk-write bias; results will mirror §4.1 of the arXiv paper.*

---

## 5. Usage Examples

### 5.1 C++

```cpp
#include "xisort.cpp"
int main() {
    std::vector<double> v = { 5.0, -0.0, 0.0, std::nan(""), -5.0 };
    XiSortConfig cfg;                 // default: internal, parallel
    xi_sort(v.data(), v.size(), cfg); // in-place
}
```

### 5.2 Python

```python
import numpy as np, xisort
a = np.random.randn(10_000_000).astype(np.float64)
xisort.xi_sort_py(a, external=False, parallel=True)
```

---

## 6. Citation

```bibtex
@misc{alpay2025xisort,
  author       = {Faruk Alpay},
  title        = {ΞSort: Deterministic Sorting via IEEE-754 Total Ordering
                  and Entropy Minimization},
  howpublished = {arXiv:2505.12345},
  year         = {2025}
}
```

---

## 7. License

Apache 2.0 — open for academic and commercial use.

---
