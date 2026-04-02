# tabx

A C++17 data parser exposed to Python via [pybind11](https://github.com/pybind/pybind11):

- a fast integer-only CSV path
- an XLSX path that builds pandas-like `DataFrame`s with mixed column types

The project demonstrates end-to-end native extension development: a zero-dependency C++ core, a CMake build system with automatic pybind11 download, and benchmarks comparing CSV/XLSX DataFrame loading speed against pandas.

---

## Features

- **C++17** core with no external runtime dependencies
- **pybind11** bindings compiled as a native `.so` / `.pyd` extension module
- **CMake** build system — pybind11 is fetched automatically via `FetchContent`
- **Fast CSV integer path** — zero-copy NumPy `int32` buffer for numeric CSV workloads
- **Typed XLSX DataFrame path** — per-column inference for `int`, `float`, `string`, `bool`, and mixed columns
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

### XLSX → DataFrame

```python
import tabx

# Mixed-type worksheets are converted into a regular pandas DataFrame.
df = tabx.parse_xlsx_dataframe("report.xlsx")

# Example inferred dtypes:
# - all integers, no nulls -> int64
# - integers/floats with blanks -> float64
# - all booleans -> bool
# - strings or mixed columns -> object

# Pick a specific worksheet by name:
sales = tabx.parse_xlsx_dataframe("report.xlsx", sheet_name="Sales")

# Inspect workbook sheets first:
sheets = tabx.list_xlsx_sheets("report.xlsx")
```

For lower-level access there are two XLSX entry points:

- `tabx.parse_xlsx_dataframe(...)` — mixed-type, pandas-like DataFrame construction
- `tabx.parse_xlsx_numpy(...)` — integer-only NumPy fast path for numeric worksheets

### Typical Python workflow

In normal application code, `tabx` is the loading step and `pandas` does the rest:

```python
import pandas as pd
import tabx

# Load XLSX with tabx into a regular pandas DataFrame
df = tabx.parse_xlsx_dataframe("sales_report.xlsx", sheet_name="Sales")

# From this point on, use standard pandas code
df = df.rename(columns=str.lower)
df = df[df["status"] == "paid"].copy()
df["revenue"] = df["qty"] * df["unit_price"]

summary = (
  df.groupby("region", dropna=False)
  .agg(
    orders=("order_id", "count"),
    revenue=("revenue", "sum"),
    avg_discount=("discount", "mean"),
  )
  .sort_values("revenue", ascending=False)
)

print(summary)
```

This is the intended usage model:

- `tabx` handles fast XLSX ingestion
- the result is an ordinary `pd.DataFrame`
- all downstream filtering, joins, grouping, and export stay in pandas

If the workbook contains multiple sheets, inspect them first and then load the one you need:

```python
import tabx

for sheet in tabx.list_xlsx_sheets("sales_report.xlsx"):
  print(sheet)

df = tabx.parse_xlsx_dataframe("sales_report.xlsx", sheet_name="Sales")
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
Output shape: (2000, 5)
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

- `pandas.read_excel` performs a generic spreadsheet decode path with full type inference.
- `tabx.parse_xlsx_dataframe` parses the workbook in C++, classifies cells once, and materialises column arrays directly for pandas-style dtypes.
- On integer-only sheets, `tabx.parse_xlsx_numpy` still exposes the narrower `int32` fast path.

The benchmark measures **end-to-end wall time**: everything from raw CSV text or XLSX file to a usable `pd.DataFrame`. The included XLSX benchmark uses integer-only sheets so it can stress the numeric hot path directly.

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