# ΞSort – Deterministic IEEE-754 Float Sorting

*External and in-memory sorting of billions of floating-point values with full IEEE compliance, deterministic tie-breaking, and optional Φ(χ) curvature tracing.*

[![ORCID](https://img.shields.io/badge/ORCID-0009--0009--2207--6528-brightgreen?logo=orcid&logoColor=white)](https://orcid.org/0009-0009-2207-6528)

---

## 1 · Overview
ΞSort (pronounced *“Xi-Sort”*) is a single-pass external sorter and in-memory merge-sort for double-precision data.

* **Total IEEE-754 order** – distinguishes ±0, NaN payloads, ±∞.  
* **Deterministic** – tie-breakers use original index for reproducible output.  
* **Hybrid mode** – auto-chooses RAM or SSD pipeline via `mem_limit`.  
* **Multi-core** – OpenMP accelerates merge tree (>7× on 8 vCPUs).  
* **Optional Φ(χ) trace** – entropy diagnostics (compile/run gated).

---

## 2 · Build & Installation

### 2.1 Prerequisites
* C++17 compiler (GCC ≥ 7, Clang ≥ 7, MSVC ≥ 19.14)  
* *(opt.)* OpenMP runtime (`libgomp`)  
* *(opt.)* `pybind11` for Python bridge

### 2.2 Quick build (CLI)

```bash
g++ -std=c++17 -O3 -fopenmp src/xisort_cli.cpp -o xisort
````

### 2.3 Makefile / CMake

```bash
# Make
make            # builds bin/xisort and bin/xisort_tests
make run-tests  # runs validation suite

# CMake (out-of-source)
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j
./bin/xisort_tests --small
```

---

## 3 · Configuration knobs

| Field          | Purpose                           | Fast default |
| -------------- | --------------------------------- | ------------ |
| `external`     | Force disk path even if RAM fits  | `false`      |
| `trace`        | Enable Φ(χ) curvature logging     | `false`      |
| `parallel`     | Activate OpenMP                   | `true`       |
| `mem_limit`    | Bytes of RAM per in-mem run       | `1 GB`       |
| `buffer_elems` | Cache per file during k-way merge | `32 768`     |

---

## 4 · Benchmarks

*All times: GCC 11 `-O3 -fopenmp`, `parallel=true`, `trace=false`.*

| ID | Scenario / Size                | mem-limit | HW / FS      | Runtime      | MB s⁻¹       | Status |
| -- | ------------------------------ | --------- | ------------ | ------------ | ------------ | ------ |
| 0  | IEEE edge cases (±0, NaN, ±∞)  | –         | RAM          | ‹1 ms        | –            | ✅      |
| 1  | Duplicate-heavy · 10 M doubles | RAM       | RAM          | **0.89 s**   | 90 MB/s      | ✅      |
| 2  | Normal(0,1) · 100 M doubles    | RAM       | RAM          | **3.86 s**   | 207 MB/s     | ✅      |
| 3a | **External · 5 GB** (RunPod)   | 1 GB      | MooseFS NVMe | **50.6 s**   | **101 MB/s** | ✅      |
| 3b | External · 100 GB (paper)      | 16 GB     | 970 EVO NVMe | **≈ 1020 s** | **98 MB/s**  | ✅      |

The throughputs match within 2%: XiSort’s cost after Phase 1 becomes pure I/O, so wall-clock scales linearly with bytes.

---

### 4.1 Reproduce the external tests

| Step                       | 5 GB / RunPod example                                                                                          | 100 GB / paper setup                                                                                                |
| -------------------------- | -------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| **1. Build**               | `make` (or `g++ …`)                                                                                            | same                                                                                                                |
| **2. Generate input once** | `python tools/gen_random_bin.py 5GB`                                                                           | `python tools/gen_random_bin.py 100GB`                                                                              |
| **3. Threads**             | `export OMP_NUM_THREADS=$(nproc)`                                                                              | `export OMP_NUM_THREADS=32`                                                                                         |
| **4. Run**                 | `bash\n./xisort \\\n  --external --parallel \\\n  --mem-limit 1073741824 \\\n  input_5GB.bin output_5GB.bin\n` | `bash\n./xisort \\\n  --external --parallel \\\n  --mem-limit 17179869184 \\\n  input_100GB.bin output_100GB.bin\n` |
| **5. Expected time**       | **≈ 50 s**                                                                                                     | **≈ 1020 s (17 min)**                                                                                                        |
| **6. Verify**              | `sha256sum` or GNU `sort -g` spot-check                                                                        | same                                                                                                                |

---

## 5 · Usage

### 5.1 C++

```cpp
#include "src/xisort.cpp"
int main() {
    double v[5] = {5.0, -0.0, 0.0, std::nan(""), -5.0};
    XiSortConfig cfg;        // in-mem, parallel
    xi_sort(v, 5, cfg);
}
```

### 5.2 Python

```python
import numpy as np, xisort
a = np.random.randn(10_000_000).astype(np.float64)
xisort.xi_sort_py(a, external=False, parallel=True)
```

---

## 6 · Cite

```bibtex
@misc{alpay2025xisort,
  author = {Faruk Alpay},
  title  = {ΞSort: Deterministic Sorting via IEEE-754 Total Ordering
            and Entropy Minimization},
  howpublished = {arXiv:2505.12345},
  year   = {2025}
}
```

---

## 7 · License

Apache 2.0 — free for academic & commercial use.

