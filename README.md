# csvpp

A C++17 CSV integer parser exposed to Python via [pybind11](https://github.com/pybind/pybind11).

The project demonstrates end-to-end native extension development: a zero-dependency C++ core, a CMake build system with automatic pybind11 download, and a benchmark comparing CSV-to-DataFrame loading speed against pandas.

---

## Features

- **C++17** core with no external runtime dependencies
- **pybind11** bindings compiled as a native `.so` / `.pyd` extension module
- **CMake** build system — pybind11 is fetched automatically via `FetchContent`
- **Allman style** throughout all C++ sources
- **Type stubs** (`.pyi`) for IDE auto-completion and static analysis
- **Benchmark suite** — head-to-head CSV → `pd.DataFrame` comparison against pandas

---

## Repository layout

```
fast-parser/
├── cpp/
│   ├── parser.h             # Public API with Doxygen-style comments
│   └── parser.cpp           # Core parsing logic
├── python/
│   └── wrapper.cpp          # pybind11 module definition (_core)
├── src/
│   └── fast_parser/      	 # Installable Python package
│       ├── __init__.py      # Public re-exports + __version__
│       ├── __init__.pyi     # Type stubs for the public Python API
│       ├── _core.pyi        # Type stubs for the native extension (overloads, NDArray[np.int32])
│       └── py.typed         # PEP 561 marker
├── examples/
│   └── example.py           # Quick usage demo
├── benchmarks/
│   └── benchmark.py         # pandas vs csvpp DataFrame benchmark
├── pyproject.toml           # Build config (scikit-build-core + mypy + ruff)
├── CMakeLists.txt
├── requirements-benchmark.txt
└── README.md
```

---

## Prerequisites

| Tool | Minimum version |
|---|---|
| CMake | 3.20 |
| C++ compiler | C++17 (GCC ≥ 9, Clang ≥ 10, MSVC 2019) |
| Python | 3.8 with development headers |

Benchmark dependency:

```bash
pip install -r requirements-benchmark.txt
```

---

## Install

### From PyPI

```bash
pip install csvpp
```

### Development (editable) install

```bash
pip install scikit-build-core pybind11   # one-time bootstrap
pip install -e .                         # builds C++ and installs as editable
```

The `pip install -e .` step invokes CMake automatically (via scikit-build-core),
links against pybind11, and places the `csvpp` package on the Python path.

> **Disable native CPU tuning for distribution wheels:**
> ```bash
> pip wheel . --config-settings cmake.define.FAST_PARSER_NATIVE=OFF
> ```

---

## API

### Single-row functions

```python
import csvpp

# Parse one CSV row into a list of integers
values = csvpp.parse_csv_numbers("10, 20, 30") # [10, 20, 30]

# Sum all integers in one CSV row
total  = csvpp.sum_csv_numbers("10, 20, 30") # 60
```

### Multi-row functions

```python
csv_text = "value\n100\n200\n300\n400"

# Parse an entire CSV document into a flat list (skip the header)
flat  = csvpp.parse_csv_flat(csv_text, skip_header=True)  # [100, 200, 300, 400]

# Sum all integers across all rows
total = csvpp.sum_csv_all(csv_text, skip_header=True)     # 1000
```

### CSV → DataFrame (the fast path)

```python
# One-liner: single C++ pass → zero-copy NumPy buffer → pd.DataFrame
df = csvpp.parse_csv_dataframe(csv_text)   # dtype int32

# For direct NumPy access (no DataFrame construction):
arr, cols = csvpp.parse_csv_numpy(csv_text, skip_header=True)

# pandas equivalent (3× slower on integer-only CSVs)
# import io, pandas as pd; df = pd.read_csv(io.StringIO(csv_text))
```

### C++ API

```cpp
#include "parser.h"

// Single-row
std::vector<int> values = fast_parser::parse_csv_numbers("1, 2, 3"); // {1, 2, 3}
int total  = fast_parser::sum_csv_numbers("1, 2, 3"); // 6

// Multi-row
std::vector<int> flat  = fast_parser::parse_csv_flat(csv_text, /*skip_header=*/true);
long long grand = fast_parser::sum_csv_all(csv_text, /*skip_header=*/true);
```

Run the demo script (requires `pip install -e .` first):

```bash
python examples/example.py
```

---

## Benchmark

Both methods produce an identical `pd.DataFrame`. The benchmark verifies shape and per-column sums before reporting timings.

```bash
python benchmarks/benchmark.py # default: 200 000 rows, 5 cols, 10 iterations
python benchmarks/benchmark.py --rows 500000 --cols 8 --iterations 20
```

### Results — 200 000 rows × 5 integer columns, 10 iterations (Apple M-series, Python 3.12)

```
Dataset:    200,000 rows × 5 integer columns
Iterations: 10 per method

  pandas.read_csv          ...     39.69 ms
  csvpp (C++)              ...     13.36 ms

Method                   Avg time    vs fastest
─────────────────────────────────────────────────
csvpp (C++)               13.36 ms         1.00×  ← fastest
pandas.read_csv           39.69 ms         2.97×

csvpp is 3.0× faster than pandas.read_csv
Output shape: (200000, 5)  dtype: int32
```

### Why csvpp wins

| Step | `pandas.read_csv` | `csvpp` |
|---|---|---|
| Input handling | `io.StringIO(text)` — heap allocation | `std::string_view` — zero copy |
| Tokenisation | C CSV tokenizer + per-column type inference | custom branchless int parser, single pass |
| Memory layout | per-column NumPy allocation | one `reserve()` into a flat int32 buffer |
| DataFrame construction | automatic (heavy) | `pd.DataFrame(arr, columns=cols)` — O(1) wrap |

The benchmark measures **end-to-end wall time**: everything from raw CSV string to a usable `pd.DataFrame`.

---

## Publishing to PyPI

```bash
# 1. Install build tools
pip install build twine

# 2. Build a source distribution and wheel
#    Pass FAST_PARSER_NATIVE=OFF so the wheel runs on any compatible CPU.
pip wheel . --config-settings cmake.define.FAST_PARSER_NATIVE=OFF -w dist/
python -m build --sdist

# 3. Upload
twine upload dist/*
```

For multi-platform wheels use [cibuildwheel](https://cibuildwheel.pypa.io):

```yaml
# .github/workflows/release.yml (excerpt)
- uses: pypa/cibuildwheel@v2
  env:
    CIBW_ENVIRONMENT: CMAKE_ARGS="-DFAST_PARSER_NATIVE=OFF"
```

---

## License

MIT — see [LICENSE](LICENSE).