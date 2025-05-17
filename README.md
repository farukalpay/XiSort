# ΞSort: Float Sorting Reimagined 🎲✨

*Stream billions of floating-point numbers with precision, safety, and deterministic grace.*

[![License](https://img.shields.io/github/license/FarukAlpay/XiSort)](https://github.com/FarukAlpay/XiSort/blob/main/LICENSE)
[![ORCID](https://img.shields.io/badge/ORCID-0009--0009--2207--6528-brightgreen?logo=orcid&logoColor=white)](https://orcid.org/0009-0009-2207-6528)

---

## 🌌 What is ΞSort?

ΞSort (*“Xi-Sort”*) **redefines external sorting for floating-point data**—preserving all IEEE intricacies (*NaN, ±∞, ±0*) while guaranteeing reproducibility and cryptographic integrity. Born from the principles of **determinism, efficiency, and mathematical elegance**, ΞSort is ideal for sorting data far beyond RAM limits on standard hardware.

---

## 🚩 Why Use ΞSort?

| 🐢 Ordinary Sorting             | 🚀 **ΞSort**                      |
|:------------------------------:|:---------------------------------:|
| Memory crashes on big data     | ✅ Stream with low-RAM footprints  |
| Unstable NaN and ±0 order      | ✅ IEEE-safe total ordering        |
| Unpredictable tie outcomes     | ✅ Algebraic tie-break/determinism |
| Risk of silent corruption      | ✅ Blake3 cryptographic integrity  |

---

## 🎯 Quick Start: 1 Million Floats in Seconds

```bash
pip install xisort

# Sort and verify instantly
xisort --count 1_000_000 --progress --verify-sorted
```

---

## 🚧 Battle-Tested Performance

Tests below were run on an **Apple Silicon M4 Pro (Python ≥3.9):**

| 🧪 Scenario                | 🖥 Command                                 | 📌 Result                              |
|---------------------------|-------------------------------------------|----------------------------------------|
| Basic IEEE sanity         | `xisort --selftest`                       | signed-zero OK, χ² = 6.8 OK            |
| Reproducibility (seeded)  | `xisort --seed 123 --require-deterministic`| 100% identical results across runs     |
| Curved sorting metric     | `xisort --mode curved --epsilon 0.03`      | Stable clustering with ε-curve         |
| Randomized ties           | `xisort --tie-break random --seed 42`      | Globally sorted; ties randomized       |
| Integrity-free speed      | `xisort --no-integrity`                   | Blake3 disabled; ~20% faster           |
| Quota safety test         | `xisort --max-gb 0.01`                    | Safe termination: MemoryError triggered|
| Heavy lifting: 10M floats | `xisort --count 10_000_000 --progress`    | ~15.7s (M4 Pro SSD)                    |

*Additional edge cases, huge-scale datasets, and high-stress memory tests are continuously expanding!*

---

## 🔮 Elegant Python API

```python
from xisort import XiSort

# Infinite possibilities, finite memory
data = (x for x in range(100_000_000))
sorter = XiSort(seed=42, nan_shuffle=True)

for v in sorter.stream_sort(data):
    print(v)   # sorted stream, ready for your pipeline
```

---

## 📂 Project Structure

```
XiSort/
├── src/      # Core sorting magic ✨
│   ├── core.py
│   └── cli.py
├── tests/           # Robust pytest suite (🐣 Will be Added)
├── examples/        # Interactive tutorials (🐣 Will be Added)
└── paper/           # Academic rigor: arXiv LaTeX paper 📚 (🐣 Will be Added)
```

---

## 📖 Citation & Academic Use

ΞSort is documented and discussed in-depth in the forthcoming arXiv publication:

```bibtex
@misc{alpay2025xisort,
  author       = {Faruk Alpay},
  title        = {ΞSort: Deterministic External Sorting with Integrity Guarantees},
  howpublished = {arXiv:YYMM.NNNNN},
  year         = {2025},
  note         = {\url{https://github.com/FarukAlpay/XiSort}}
}
```

---

## 🔒 License

Apache 2.0 — freedom for both academia and commercial innovation.

---

## 🌠 Meet the Creator

- 🎓 **Faruk Alpay** — [ORCID](https://orcid.org/0009-0009-2207-6528)
- Part of the visionary **Alpay Algebra** ecosystem.

---

## 🚀 Next Up

ΞSort evolves! Expect more heavy-duty stress-tests, cross-platform consistency checks, and advanced sorting metrics—fueling your data-driven dreams.

**Happy sorting!** 🎉 
