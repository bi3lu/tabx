#!/usr/bin/env python3
"""Benchmark: DataFrame loading via pandas vs tabx (CSV and XLSX).

Supported benchmark formats:
    - csv  : pandas.read_csv(io.StringIO(csv_text)) vs tabx.parse_csv_dataframe(...)
    - xlsx : pandas.read_excel(path) vs tabx.parse_xlsx_dataframe(path)

Example:
        python benchmarks/benchmark.py
        python benchmarks/benchmark.py --format xlsx --rows 500000 --cols 8 --iterations 20
"""

from __future__ import annotations

import argparse
import io
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


# Implementations:


def _pandas_csv_df(csv_text: str) -> pd.DataFrame:
    """Load CSV into DataFrame using pandas.read_csv."""
    return pd.read_csv(io.StringIO(csv_text))


def _tabx_csv_df(csv_text: str) -> pd.DataFrame:
    """Load CSV into DataFrame using tabx parser."""
    return cast(pd.DataFrame, tabx.parse_csv_dataframe(csv_text, skip_header=True))


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


def _run_single_benchmark(
    title: str,
    payload: str,
    loaders: list[tuple[str, Callable[[str], pd.DataFrame]]],
    iterations: int,
) -> None:
    """Run and print one benchmark table (CSV or XLSX)."""
    print(f"\n=== {title} ===")

    timings: dict[str, float] = {}
    results: list[pd.DataFrame] = []

    for name, fn in loaders:
        print(f"  {name:<24} ...", end="", flush=True)
        avg_s, df = _measure(fn, payload, iterations)
        timings[name] = avg_s
        results.append(df)
        print(f"  {avg_s * 1_000:>8.2f} ms")

    _verify(results[0], results[1])

    fastest = min(timings.values())
    name_w = max(len(k) for k in timings) + 2

    print(f"\n{'Method':<{name_w}}  {'Avg time':>10}  {'vs fastest':>12}")
    print("─" * (name_w + 28))

    for name, elapsed in sorted(timings.items(), key=lambda x: x[1]):
        ratio = elapsed / fastest
        mark = "  ← fastest" if elapsed == fastest else ""
        print(f"{name:<{name_w}}  {elapsed * 1_000:>8.2f} ms  {ratio:>11.2f}×{mark}")

    speedup = timings["pandas"] / timings["tabx (C++)"]
    print(f"\ntabx is {speedup:.1f}× faster than pandas ({title})")
    print(f"Output shape: {results[1].shape}  dtype: {results[1].dtypes.iloc[0].name}")


# Entry point:


def run(
    n_rows: int = 200_000,
    n_cols: int = 5,
    iterations: int = 10,
    fmt: str = "both",
) -> None:
    print(f"Dataset:    {n_rows:,} rows × {n_cols} integer columns")
    print(f"Iterations: {iterations} per method\n")

    cols, rows = generate_table(n_rows, n_cols)
    csv_text = table_to_csv(cols, rows)

    if fmt in {"csv", "both"}:
        _run_single_benchmark(
            title="CSV",
            payload=csv_text,
            loaders=[
                ("pandas", _pandas_csv_df),
                ("tabx (C++)", _tabx_csv_df),
            ],
            iterations=iterations,
        )

    if fmt in {"xlsx", "both"}:
        with tempfile.TemporaryDirectory(prefix="tabx_bench_") as tmpdir:
            xlsx_path = Path(tmpdir) / "dataset.xlsx"
            table_to_dataframe(cols, rows).to_excel(xlsx_path, index=False)

            _run_single_benchmark(
                title="XLSX",
                payload=str(xlsx_path),
                loaders=[
                    ("pandas", _pandas_xlsx_df),
                    ("tabx (C++)", _tabx_xlsx_df),
                ],
                iterations=iterations,
            )


def _parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="CSV/XLSX → DataFrame benchmark: pandas vs tabx.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument(
        "--rows", type=int, default=200_000, metavar="N", help="Number of data rows."
    )
    p.add_argument(
        "--cols",
        type=int,
        default=5,
        metavar="N",
        help=f"Number of integer columns (1–{len(_COLUMNS)}).",
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
        choices=["csv", "xlsx", "both"],
        default="both",
        help="Input format to benchmark.",
    )
    return p.parse_args()


if __name__ == "__main__":
    args = _parse_args()
    run(n_rows=args.rows, n_cols=args.cols, iterations=args.iterations, fmt=args.fmt)
