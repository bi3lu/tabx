# tabx

A C++17 CSV integer parser exposed to Python via [pybind11](https://github.com/pybind/pybind11).

The project demonstrates end-to-end native extension development: a zero-dependency C++ core, a CMake build system with automatic pybind11 download, and benchmarks comparing CSV/XLSX DataFrame loading speed against pandas.

---

## Features

- **C++17** core with no external runtime dependencies
- **pybind11** bindings compiled as a native `.so` / `.pyd` extension module
- **CMake** build system — pybind11 is fetched automatically via `FetchContent`
- **Allman style** throughout all C++ sources
- **Type stubs** (`.pyi`) for IDE auto-completion and static analysis
- **Benchmark suite** — head-to-head CSV/XLSX → `pd.DataFrame` comparisons against pandas

---

## Repository layout

```
tabx/
├── cpp/
│   ├── parser.h             # Public API with Doxygen-style comments
│   └── parser.cpp           # Core parsing logic
├── python/
│   └── wrapper.cpp          # pybind11 module definition (_core)
├── src/
│   └── tabx/      	 # Installable Python package
│       ├── __init__.py      # Public re-exports + __version__
│       ├── __init__.pyi     # Type stubs for the public Python API
│       ├── _core.pyi        # Type stubs for the native extension (overloads, NDArray[np.int32])
│       └── py.typed         # PEP 561 marker
├── examples/
│   └── example.py           # Quick usage demo
├── benchmarks/
│   └── benchmark.py         # pandas vs tabx benchmark (CSV + XLSX)
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

Benchmark dependencies:

```bash
pip install -r requirements-benchmark.txt
```

---

## Install

### From PyPI

```bash
pip install tabx
```

### Development (editable) install

```bash
pip install scikit-build-core pybind11   # one-time bootstrap
pip install -e .                         # builds C++ and installs as editable
```

The `pip install -e .` step invokes CMake automatically (via scikit-build-core),
links against pybind11, and places the `tabx` package on the Python path.

> **Disable native CPU tuning for distribution wheels:**
> ```bash
> pip wheel . --config-settings cmake.define.TABX_NATIVE=OFF
> ```

---

## API

### Single-row functions

```python
import tabx

# Parse one CSV row into a list of integers
values = tabx.parse_csv_numbers("10, 20, 30") # [10, 20, 30]

# Sum all integers in one CSV row
total  = tabx.sum_csv_numbers("10, 20, 30") # 60
```

### Multi-row functions

```python
csv_text = "value\n100\n200\n300\n400"

# Parse an entire CSV document into a flat list (skip the header)
flat  = tabx.parse_csv_flat(csv_text, skip_header=True)  # [100, 200, 300, 400]

# Sum all integers across all rows
total = tabx.sum_csv_all(csv_text, skip_header=True)     # 1000
```

### CSV → DataFrame (the fast path)

```python
# One-liner: single C++ pass → zero-copy NumPy buffer → pd.DataFrame
df = tabx.parse_csv_dataframe(csv_text)   # dtype int32

# For direct NumPy access (no DataFrame construction):
arr, cols = tabx.parse_csv_numpy(csv_text, skip_header=True)

# pandas equivalent (3× slower on integer-only CSVs)
# import io, pandas as pd; df = pd.read_csv(io.StringIO(csv_text))
```

### C++ API

```cpp
#include "parser.h"

// Single-row
std::vector<int> values = tabx::parse_csv_numbers("1, 2, 3"); // {1, 2, 3}
int total  = tabx::sum_csv_numbers("1, 2, 3"); // 6

// Multi-row
std::vector<int> flat  = tabx::parse_csv_flat(csv_text, /*skip_header=*/true);
long long grand = tabx::sum_csv_all(csv_text, /*skip_header=*/true);
```

Run the demo script (requires `pip install -e .` first):

```bash
python examples/example.py
```

---

## Benchmark

Both backends produce an identical `pd.DataFrame`. The benchmark verifies shape and per-column sums before reporting timings.

```bash
python benchmarks/benchmark.py                                  # default: both formats
python benchmarks/benchmark.py --format csv                    # CSV only
python benchmarks/benchmark.py --format xlsx                   # XLSX only
python benchmarks/benchmark.py --format both --rows 500000 --cols 8 --iterations 20
```

`--format` options:

- `csv`  — `pandas.read_csv` vs `tabx.parse_csv_dataframe`
- `xlsx` — `pandas.read_excel` vs `tabx.parse_xlsx_dataframe`
- `both` — runs CSV and XLSX sequentially

### Example Results — XLSX, 2 000 rows × 5 integer columns, 2 iterations

```
Dataset:    2,000 rows × 5 integer columns
Iterations: 2 per method

=== XLSX ===
  pandas                   ...     22.73 ms
  tabx (C++)               ...      3.41 ms

Method          Avg time    vs fastest
────────────────────────────────────────
tabx (C++)        3.41 ms         1.00×  ← fastest
pandas           22.73 ms         6.66×

tabx is 6.7× faster than pandas (XLSX)
Output shape: (2000, 5)  dtype: int32
```

### Why tabx wins

For CSV:

| Step | `pandas.read_csv` | `tabx` |
|---|---|---|
| Input handling | `io.StringIO(text)` — heap allocation | `std::string_view` — zero copy |
| Tokenisation | C CSV tokenizer + per-column type inference | custom branchless int parser, single pass |
| Memory layout | per-column NumPy allocation | one `reserve()` into a flat int32 buffer |
| DataFrame construction | automatic (heavy) | `pd.DataFrame(arr, columns=cols)` — O(1) wrap |

For XLSX:

- `pandas.read_excel` performs a generic spreadsheet decode path.
- `tabx.parse_xlsx_dataframe` uses a targeted integer-only C++ parser and writes directly to an `int32` buffer before wrapping into `pd.DataFrame`.

The benchmark measures **end-to-end wall time**: everything from raw CSV text or XLSX file to a usable `pd.DataFrame`.

---

## Publishing to PyPI

```bash
# 1. Install build tools
pip install build twine

# 2. Build a source distribution and wheel
#    Pass TABX_NATIVE=OFF so the wheel runs on any compatible CPU.
pip wheel . --config-settings cmake.define.TABX_NATIVE=OFF -w dist/
python -m build --sdist

# 3. Upload
twine upload dist/*
```

For multi-platform wheels use [cibuildwheel](https://cibuildwheel.pypa.io):

```yaml
# .github/workflows/release.yml (excerpt)
- uses: pypa/cibuildwheel@v2
  env:
    CIBW_ENVIRONMENT: CMAKE_ARGS="-DTABX_NATIVE=OFF"
```

---

## License

MIT — see [LICENSE](LICENSE).