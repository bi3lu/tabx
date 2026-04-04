#!/usr/bin/env python3
"""Benchmark: DataFrame loading via pandas vs tabx (CSV and XLSX).

Supported benchmark formats:
    - csv           : integer-only CSV (baseline)
    - csv-mixed     : realistic CSV with int, float, bool, string and null cells
    - csv-wide-sparse : wide CSV (~60 % cells empty) ─ tests null-handling speed
    - csv-quoted    : RFC 4180 heavy quoting (commas / escaped quotes in fields)
    - csv-external  : benchmark a real CSV file passed via --input
    - csv-all       : all three CSV variants above
    - xlsx          : integer XLSX
    - xlsx-mixed    : mixed-type XLSX
    - both          : csv + xlsx (integer)

Example:
        python benchmarks/benchmark.py
        python benchmarks/benchmark.py --format csv-mixed --rows 300000 --iterations 15
        python benchmarks/benchmark.py --format csv-all --rows 100000 --iterations 10
        python benchmarks/benchmark.py --format csv-external --input benchmarks/public_data/nyc_taxi_sample.csv
        python benchmarks/benchmark.py --format xlsx --rows 500000 --cols 8 --iterations 20
        python benchmarks/benchmark.py --format xlsx-mixed --rows 50000 --iterations 10
"""

from __future__ import annotations

import argparse
import io
import json
import random
import sys
import tempfile
import time
from pathlib import Path
from typing import Callable, cast

try:
    import tabx

except ImportError:
    sys.exit("tabx is not installed.\nRun: pip install -e .")

try:
    import pandas as pd

except ImportError:
    sys.exit("pandas is required for this benchmark: pip install pandas")

try:
    import openpyxl  # noqa: F401

except ImportError:
    sys.exit("openpyxl is required for XLSX benchmark: pip install openpyxl")


# Dataset generation:

_COLUMNS = ["id", "qty", "price", "discount", "tax", "weight", "score", "rank"]


def generate_table(
    n_rows: int,
    n_cols: int,
    seed: int = 42,
) -> tuple[list[str], list[list[int]]]:
    """Return deterministic integer table data.

    Args:
        n_rows: Number of data rows (excluding header).
        n_cols: Number of integer columns (max 8).
        seed:   Random seed for reproducibility.

    Returns:
        (column_names, row_values)
    """
    if n_cols > len(_COLUMNS):
        raise ValueError(f"n_cols must be ≤ {len(_COLUMNS)}")

    cols = _COLUMNS[:n_cols]
    rng = random.Random(seed)
    rows: list[list[int]] = []

    for _ in range(n_rows):
        rows.append([rng.randint(1, 1_000_000) for _ in cols])

    return cols, rows


def table_to_csv(cols: list[str], rows: list[list[int]]) -> str:
    """Render table to CSV text with a header row."""
    lines = [",".join(cols)]
    for row in rows:
        lines.append(",".join(str(v) for v in row))

    return "\n".join(lines)


def table_to_dataframe(cols: list[str], rows: list[list[int]]) -> pd.DataFrame:
    """Create an in-memory DataFrame used to emit XLSX input."""
    return pd.DataFrame(rows, columns=cols, dtype="int32")


def generate_mixed_xlsx_dataframe(
    n_rows: int,
    seed: int = 42,
) -> pd.DataFrame:
    """Return deterministic mixed-type tabular data for XLSX benchmarks.

    The schema is intentionally limited to types currently handled by tabx:
    integers, floats, strings, booleans, and object/mixed columns.
    """
    rng = random.Random(seed)

    ints: list[int] = []
    floats: list[float | None] = []
    strings: list[str] = []
    bools: list[bool] = []
    mixed: list[int | str | None] = []

    for i in range(n_rows):
        ints.append(rng.randint(1, 1_000_000))

        if i % 7 == 0:
            floats.append(None)

        else:
            floats.append(round(rng.uniform(0.0, 10_000.0), 3))

        strings.append(f"label_{rng.randint(1, 500)}")
        bools.append((i + rng.randint(0, 1)) % 2 == 0)

        selector = i % 3

        if selector == 0:

            mixed.append(rng.randint(1, 10_000))
        elif selector == 1:
            mixed.append(f"code_{rng.randint(1, 300)}")

        else:
            mixed.append(None)

    return pd.DataFrame(
        {
            "ints": ints,
            "floats": floats,
            "strings": strings,
            "bools": bools,
            "mixed": mixed,
        }
    )


# New CSV-specific generators:


_CITIES = [
    "New York, NY",
    "Los Angeles, CA",
    "Chicago, IL",
    "Houston, TX",
    "Phoenix, AZ",
    "San Francisco, CA",
    "San Antonio, TX",
    "San Diego, CA",
    "Dallas, TX",
    "Austin, TX",
]

_CATEGORIES = [
    "Electronics",
    "Clothing",
    "Food & Beverage",
    "Home & Garden",
    "Sports",
    "Books",
    "Toys & Games",
    "Beauty",
    "Automotive",
    "Health",
]


def generate_mixed_csv(n_rows: int, seed: int = 42) -> str:
    """Realistic CSV with six columns of varying types.

    Schema: id (int), amount (int), price (float|empty), active (bool),
    category (string), score (float).

    ~14 % of price cells and ~8 % of score cells are null (empty field), so
    pandas and tabx both produce float64 columns with NaN for those.
    """
    rng = random.Random(seed)
    lines = ["id,amount,price,active,category,score"]
    for i in range(n_rows):
        id_ = i + 1
        amount = rng.randint(1, 1_000_000)
        price = "" if i % 7 == 0 else f"{rng.uniform(0.01, 9_999.99):.4f}"
        active = "true" if rng.random() > 0.5 else "false"
        category = rng.choice(_CATEGORIES)
        score = "" if i % 13 == 0 else f"{rng.gauss(50.0, 15.0):.3f}"
        lines.append(f"{id_},{amount},{price},{active},{category},{score}")
    return "\n".join(lines)


def generate_wide_sparse_csv(n_rows: int, n_cols: int = 24, seed: int = 42) -> str:
    """Wide CSV where ~60 % of data cells are empty.

    All non-empty cells are integers. This stresses null-tracking,
    per-column type inference, and NaN fill in the output array.

    Args:
        n_rows:  Number of data rows.
        n_cols:  Number of columns (default 24).
        seed:    Random seed.
    """
    rng = random.Random(seed)
    col_names = [f"f{i:02d}" for i in range(n_cols)]
    lines = [",".join(col_names)]
    for _ in range(n_rows):
        row = ["" if rng.random() < 0.60 else str(rng.randint(1, 100_000)) for _ in col_names]
        lines.append(",".join(row))
    return "\n".join(lines)


def generate_quoted_heavy_csv(n_rows: int, seed: int = 42) -> str:
    """RFC 4180 stress-test CSV.

    Every name field is quoted because it contains a comma ("Last, First").
    Every city field is quoted (contains ", STATE").  ~10 % of remark fields
    contain escaped double-quotes (\"\" sequences).  ~8 % contain embedded
    commas.  Score is a plain integer.  id is a plain integer.
    """
    rng = random.Random(seed)
    lines = ["id,name,city,score,remarks"]
    for i in range(n_rows):
        id_ = i + 1
        last = f"Last{rng.randint(1, 500)}"
        first = f"First{rng.randint(1, 500)}"
        name = f'"{last}, {first}"'  # always quoted due to comma
        city = f'"{rng.choice(_CITIES)}"'  # always quoted due to comma
        score = rng.randint(1, 100)
        idx = i % 10
        if idx == 0:
            # Escaped double-quotes inside a quoted field
            remarks = f'"status: ""confirmed"""'
        elif idx in (3, 7):
            # Comma inside a quoted field
            remarks = f'"shipped, delivered"'
        else:
            remarks = f"plain_{rng.randint(1, 200)}"
        lines.append(f"{id_},{name},{city},{score},{remarks}")
    return "\n".join(lines)


# Implementations:


def _pandas_csv_df(csv_text: str) -> pd.DataFrame:
    """Load CSV into DataFrame using pandas.read_csv."""
    return pd.read_csv(io.StringIO(csv_text))


def _tabx_csv_df(csv_text: str) -> pd.DataFrame:
    """Load CSV into DataFrame using tabx parser."""
    return cast(pd.DataFrame, tabx.parse_csv_dataframe(csv_text, skip_header=True))


def _read_text_file(path: str) -> str:
    """Read a UTF-8 text file once so the benchmark measures parsing, not I/O."""
    return Path(path).read_text(encoding="utf-8")


def _pandas_xlsx_df(xlsx_path: str) -> pd.DataFrame:
    """Load XLSX into DataFrame using pandas.read_excel."""
    return pd.read_excel(xlsx_path)


def _tabx_xlsx_df(xlsx_path: str) -> pd.DataFrame:
    """Load XLSX into DataFrame using tabx parser."""
    return cast(pd.DataFrame, tabx.parse_xlsx_dataframe(xlsx_path, skip_header=True))


# Timing helper:


def _measure(
    fn: Callable[[str], pd.DataFrame],
    payload: str,
    iterations: int,
) -> tuple[float, pd.DataFrame]:
    """Return (avg_seconds, last_result). One warmup call before timing."""
    result = fn(payload)
    start = time.perf_counter()

    for _ in range(iterations):
        fn(payload)

    return (time.perf_counter() - start) / iterations, result


# Verification:


def _verify(df_pandas: pd.DataFrame, df_tabx: pd.DataFrame) -> None:
    """Raise AssertionError if the two DataFrames disagree on shape or sums."""
    assert (
        df_pandas.shape == df_tabx.shape
    ), f"Shape mismatch: pandas={df_pandas.shape} tabx={df_tabx.shape}"

    for col in df_pandas.columns:
        s_p = int(df_pandas[col].sum())
        s_t = int(df_tabx[col].sum())
        assert s_p == s_t, f"Column '{col}' sum mismatch: pandas={s_p} tabx={s_t}"


def _verify_numeric_cols(df_pandas: pd.DataFrame, df_tabx: pd.DataFrame) -> None:
    """Verify shape and per-column numeric sums for mixed-type DataFrames.

    Only numeric columns (int/float) are compared by sum; string and bool
    columns are verified only by non-null count so that dtype differences
    between pandas and tabx don't cause false failures.
    """
    assert (
        df_pandas.shape == df_tabx.shape
    ), f"Shape mismatch: pandas={df_pandas.shape} tabx={df_tabx.shape}"

    for col in df_pandas.columns:
        p_col = df_pandas[col]
        t_col = df_tabx[col]
        if pd.api.types.is_numeric_dtype(p_col) and pd.api.types.is_numeric_dtype(t_col):
            sp = float(p_col.sum())
            st = float(t_col.sum())
            diff = abs(sp - st)
            tol = max(1e-3 * abs(sp), 1e-6)
            assert (
                diff <= tol
            ), f"Column '{col}' numeric sum mismatch: pandas={sp:.6f} tabx={st:.6f}"
        else:
            # For string / bool columns just verify non-null counts match.
            assert p_col.count() == t_col.count(), (
                f"Column '{col}' non-null count mismatch: "
                f"pandas={p_col.count()} tabx={t_col.count()}"
            )


def _verify_mixed(df_pandas: pd.DataFrame, df_tabx: pd.DataFrame) -> None:
    """Raise AssertionError if mixed-type XLSX results differ."""
    assert list(df_pandas.columns) == list(df_tabx.columns), (
        f"Column mismatch: pandas={list(df_pandas.columns)} " f"tabx={list(df_tabx.columns)}"
    )

    pd.testing.assert_frame_equal(
        df_pandas,
        df_tabx,
        check_dtype=False,
        check_exact=True,
    )


def _run_single_benchmark(
    title: str,
    dataset_label: str,
    payload: str,
    loaders: list[tuple[str, Callable[[str], pd.DataFrame]]],
    iterations: int,
    verify_fn: Callable[[pd.DataFrame, pd.DataFrame], None] = _verify,
) -> dict[str, object]:
    """Run and print one benchmark table (CSV or XLSX)."""
    print(dataset_label)
    print(f"\n=== {title} ===")

    timings: dict[str, float] = {}
    results: list[pd.DataFrame] = []

    for name, fn in loaders:
        print(f"  {name:<24} ...", end="", flush=True)
        avg_s, df = _measure(fn, payload, iterations)
        timings[name] = avg_s
        results.append(df)
        print(f"  {avg_s * 1_000:>8.2f} ms")

    verify_fn(results[0], results[1])

    fastest = min(timings.values())
    name_w = max(len(k) for k in timings) + 2

    print(f"\n{'Method':<{name_w}}  {'Avg time':>10}  {'vs fastest':>12}")
    print("─" * (name_w + 28))

    for name, elapsed in sorted(timings.items(), key=lambda x: x[1]):
        ratio = elapsed / fastest
        mark = "  ← fastest" if elapsed == fastest else ""
        print(f"{name:<{name_w}}  {elapsed * 1_000:>8.2f} ms  {ratio:>11.2f}×{mark}")

    speedup = timings["pandas"] / timings["tabx (C++)"]
    relation = "faster" if speedup >= 1.0 else "slower"
    relative = speedup if speedup >= 1.0 else (1.0 / speedup)

    print(f"\ntabx is {relative:.1f}× {relation} than pandas ({title})")
    print(f"Output shape: {results[1].shape}")
    print("Output dtypes:", ", ".join(str(dtype) for dtype in results[1].dtypes.tolist()))

    return {
        "title": title,
        "dataset": dataset_label,
        "iterations": iterations,
        "timings_ms": {name: elapsed * 1_000.0 for name, elapsed in timings.items()},
        "tabx_vs_pandas_ratio": speedup,
        "output_shape": list(results[1].shape),
        "output_dtypes": [str(dtype) for dtype in results[1].dtypes.tolist()],
    }


# Entry point:


def run(
    n_rows: int = 200_000,
    n_cols: int = 5,
    iterations: int = 10,
    fmt: str = "both",
    input_path: str = "",
    json_out: str = "",
) -> list[dict[str, object]]:
    print(f"Iterations: {iterations} per method\n")
    records: list[dict[str, object]] = []

    # Integer CSV / XLSX (existing):
    if fmt in {"csv", "both", "csv-all"}:
        cols, rows = generate_table(n_rows, n_cols)
        csv_text = table_to_csv(cols, rows)
        records.append(
            _run_single_benchmark(
                title="CSV — integer",
                dataset_label=f"Dataset:    {n_rows:,} rows × {n_cols} integer columns",
                payload=csv_text,
                loaders=[
                    ("pandas", _pandas_csv_df),
                    ("tabx (C++)", _tabx_csv_df),
                ],
                iterations=iterations,
            )
        )

    # Mixed-type CSV:
    if fmt in {"csv-mixed", "csv-all"}:
        csv_text = generate_mixed_csv(n_rows)
        records.append(
            _run_single_benchmark(
                title="CSV — mixed realistic  (int · float · bool · string · nulls)",
                dataset_label=(
                    f"Dataset:    {n_rows:,} rows × 6 columns  "
                    "(id int, amount int, price float?, active bool, category str, score float?)"
                ),
                payload=csv_text,
                loaders=[
                    ("pandas", _pandas_csv_df),
                    ("tabx (C++)", _tabx_csv_df),
                ],
                iterations=iterations,
                verify_fn=_verify_numeric_cols,
            )
        )

    # Wide-sparse CSV:
    if fmt in {"csv-wide-sparse", "csv-all"}:
        csv_text = generate_wide_sparse_csv(n_rows)
        n_sparse_cols = 24
        records.append(
            _run_single_benchmark(
                title="CSV — wide sparse  (~60 % null cells)",
                dataset_label=(
                    f"Dataset:    {n_rows:,} rows × {n_sparse_cols} int columns  "
                    "(60 % cells empty → NaN)"
                ),
                payload=csv_text,
                loaders=[
                    ("pandas", _pandas_csv_df),
                    ("tabx (C++)", _tabx_csv_df),
                ],
                iterations=iterations,
                verify_fn=_verify_numeric_cols,
            )
        )

    # RFC 4180 quoted-heavy CSV:
    if fmt in {"csv-quoted", "csv-all"}:
        csv_text = generate_quoted_heavy_csv(n_rows)
        records.append(
            _run_single_benchmark(
                title='CSV — RFC 4180 quoted-heavy  ("Last, First" · escaped "" · )',
                dataset_label=(
                    f"Dataset:    {n_rows:,} rows × 5 columns  "
                    "(all name/city fields quoted; 10 % rows with escaped quotes)"
                ),
                payload=csv_text,
                loaders=[
                    ("pandas", _pandas_csv_df),
                    ("tabx (C++)", _tabx_csv_df),
                ],
                iterations=iterations,
                verify_fn=_verify_numeric_cols,
            )
        )

    if fmt == "csv-external":
        if not input_path:
            raise ValueError("--input is required when --format=csv-external")

        csv_text = _read_text_file(input_path)
        records.append(
            _run_single_benchmark(
                title="CSV — external real-world file",
                dataset_label=f"Dataset:    {Path(input_path).name}",
                payload=csv_text,
                loaders=[
                    ("pandas", _pandas_csv_df),
                    ("tabx (C++)", _tabx_csv_df),
                ],
                iterations=iterations,
                verify_fn=_verify_numeric_cols,
            )
        )

    # Integer XLSX:
    if fmt in {"xlsx", "both"}:
        cols, rows = generate_table(n_rows, n_cols)

        with tempfile.TemporaryDirectory(prefix="tabx_bench_") as tmpdir:
            xlsx_path = Path(tmpdir) / "dataset.xlsx"
            table_to_dataframe(cols, rows).to_excel(xlsx_path, index=False)

            records.append(
                _run_single_benchmark(
                    title="XLSX — integer",
                    dataset_label=f"Dataset:    {n_rows:,} rows × {n_cols} integer columns",
                    payload=str(xlsx_path),
                    loaders=[
                        ("pandas", _pandas_xlsx_df),
                        ("tabx (C++)", _tabx_xlsx_df),
                    ],
                    iterations=iterations,
                )
            )

    # Mixed-type XLSX:
    if fmt == "xlsx-mixed":
        with tempfile.TemporaryDirectory(prefix="tabx_bench_mixed_") as tmpdir:
            xlsx_path = Path(tmpdir) / "dataset_mixed.xlsx"
            generate_mixed_xlsx_dataframe(n_rows).to_excel(xlsx_path, index=False)

            records.append(
                _run_single_benchmark(
                    title="XLSX — mixed",
                    dataset_label=(
                        f"Dataset:    {n_rows:,} rows × 5 mixed-type columns "
                        "(int, float?, string, bool, mixed)"
                    ),
                    payload=str(xlsx_path),
                    loaders=[
                        ("pandas", _pandas_xlsx_df),
                        ("tabx (C++)", _tabx_xlsx_df),
                    ],
                    iterations=iterations,
                    verify_fn=_verify_mixed,
                )
            )

    if json_out:
        out_path = Path(json_out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(records, indent=2), encoding="utf-8")
        print(f"\nWrote JSON results to: {out_path}")

    return records


def _parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="CSV/XLSX → DataFrame benchmark: pandas vs tabx.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--rows", type=int, default=200_000, metavar="N", help="Number of data rows.")
    p.add_argument(
        "--cols",
        type=int,
        default=5,
        metavar="N",
        help=(
            f"Number of integer columns (1–{len(_COLUMNS)}). "
            "Used by csv/xlsx; ignored by xlsx-mixed."
        ),
    )
    p.add_argument(
        "--iterations",
        type=int,
        default=10,
        metavar="N",
        help="Timed repetitions per method.",
    )
    p.add_argument(
        "--format",
        dest="fmt",
        choices=[
            "csv",
            "csv-mixed",
            "csv-wide-sparse",
            "csv-quoted",
            "csv-external",
            "csv-all",
            "xlsx",
            "xlsx-mixed",
            "both",
        ],
        default="both",
        help=(
            "Input format to benchmark.  "
            "'csv-all' runs all three CSV variants; "
            "'both' runs integer CSV + integer XLSX."
        ),
    )
    p.add_argument(
        "--input",
        default="",
        help="External CSV path used only with --format=csv-external.",
    )
    p.add_argument(
        "--json-out",
        default="",
        help="Optional output path for machine-readable benchmark results (JSON).",
    )
    return p.parse_args()


if __name__ == "__main__":
    args = _parse_args()
    run(
        n_rows=args.rows,
        n_cols=args.cols,
        iterations=args.iterations,
        fmt=args.fmt,
        input_path=args.input,
        json_out=args.json_out,
    )
