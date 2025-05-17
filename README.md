# ΞSort

*A deterministic, integrity-checked external sorter for 64-bit floats.*  
Handles **billions of values** on commodity hardware while preserving IEEE
edge-cases ( ±0 , NaN, ±∞ ) and producing a **reproducible total order** across
all platforms.

[![PyPI](https://img.shields.io/pypi/v/xisort?logo=pypi)](https://pypi.org/project/xisort)
[![CI](https://github.com/FarukAlpay/XiSort/actions/workflows/ci.yml/badge.svg)](https://github.com/FarukAlpay/XiSort/actions)
![platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey)
![license](https://img.shields.io/github/license/FarukAlpay/XiSort)

---

## 🔑 Why ΞSort?

| Conventional sort            | ΞSort                                          |
|------------------------------|------------------------------------------------|
| RAM-bound; crashes on > memory | Streams in fixed-size chunks; scratch-quota guard |
| Undefined order for ±0 / NaN | Stable, IEEE-compliant total order             |
| No tie-break reproducibility | Algebraic tie keys or deterministic PRNG       |
| Silent data corruption risk  | Blake3 tag per chunk (optional)                |

---

## 🚀 Installation

```bash
pip install xisort          # Python ≥ 3.9

No C-extensions; pure NumPy + mmap + Blake3.

⸻

⚡ Quick CLI demo

xisort --count 1_000_000 --progress

# or with full reproducibility:
xisort --seed 42 --require-deterministic --verify-sorted

Common options:

Flag	Meaning
--count N	Synthetic benchmark input (default = 1 000 000).
--progress	Print 5 % milestones (≥ 5 s interval).
--verify-sorted	Abort on first out-of-order value (single pass).
--nan-shuffle	Randomise NaNs/±Inf order inside the tail segment.
--max-gb X	Hard scratch quota (MemoryError if exceeded).
--mode curved	ε-curved metric for clustered data.

Run xisort --help for the full flag list.

⸻

🔧 Python API

from xisort import XiSort

stream = (x for x in range(10_000_000))          # any iterable of float64
sorter = XiSort(seed=123, nan_shuffle=True)

for v in sorter.stream_sort(stream):
    ...   # consume sorted values lazily

The iterator starts emitting values long before the full dataset is read,
enabling true pipeline streaming.

⸻

📊 Rough performance (M4 Pro, single-thread)

Data size	NumPy np.sort (in-RAM)	xisort --no-integrity (≥ 1 GiB scratch)
1 M floats	0.07 s	0.25 s
10 M floats	0.7 s	1.6 s (single-chunk)15.7 s (external merge)
1 B floats	— (OOM)	≈ 32 min with 45 GiB scratch, 6 GiB RAM

	Detailed benchmarks and I/O breakdown in the accompanying paper (paper/main.tex).

⸻

🏗 Repo structure

XiSort/
├─ src/xisort/         ← library code
│   ├─ core.py         (XiSort implementation)
│   └─ cli.py          (argparse wrapper)
├─ tests/              ← pytest suite
├─ examples/           ← Jupyter notebooks & shell demos
└─ paper/              ← arXiv LaTeX source

Install an editable dev environment:

git clone https://github.com/FarukAlpay/XiSort.git
cd XiSort
python -m venv .venv && source .venv/bin/activate
pip install -e ".[dev]"        # pytest, ruff, mypy, mkdocs-material
pytest -q



⸻

📰 Citing ΞSort

If you use ΞSort in research, please cite the arXiv preprint (replace
placeholder when paper ID is assigned):

@misc{alpay2025xisort,
  author       = {Faruk Alpay},
  title        = {ΞSort: Deterministic External Sorting with Integrity Guarantees},
  howpublished = {arXiv:YYMM.NNNNN},
  year         = {2025},
  note         = {\url{https://github.com/FarukAlpay/XiSort}}
}



⸻

🛡 License

Apache 2.0 — free for commercial and academic use, with patent grant.
See LICENSE for full text.

ΞSort is part of the broader Alpay Algebra ecosystem.
For discussion, open an issue or ping @farukalpay.

