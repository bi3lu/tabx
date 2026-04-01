#!/usr/bin/env python3
"""Benchmark: CSV → pd.DataFrame via pandas vs csvpp.

Both backends produce an equivalent pd.DataFrame from the same CSV text.
The benchmark measures total wall time including all internal steps:
  - pandas : pd.read_csv(io.StringIO(csv_text))
  - csvpp  : parse_csv_numpy() → pd.DataFrame(arr, columns=cols)

Example:
    python benchmarks/benchmark.py
    python benchmarks/benchmark.py --rows 500000 --cols 8 --iterations 20
"""

from __future__ import annotations

import argparse
import io
import random
import sys
import time
from typing import Callable

try:
    import csvpp

except ImportError:
    sys.exit("csvpp is not installed.\nRun: pip install -e .")

try:
    import pandas as pd

except ImportError:
    sys.exit("pandas is required for this benchmark: pip install pandas")


# Dataset generation:

_COLUMNS = ["id", "qty", "price", "discount", "tax", "weight", "score", "rank"]


def generate_csv(n_rows: int, n_cols: int, seed: int = 42) -> str:
    """Return an integer CSV string with a header row.

    Args:
        n_rows: Number of data rows (excluding header).
        n_cols: Number of integer columns (max 8).
        seed:   Random seed for reproducibility.

    Returns:
        CSV text with header and n_rows data rows.
    """
    if n_cols > len(_COLUMNS):
        raise ValueError(f"n_cols must be ≤ {len(_COLUMNS)}")

    cols = _COLUMNS[:n_cols]
    rng = random.Random(seed)
    lines = [",".join(cols)]

    for _ in range(n_rows):
        lines.append(",".join(str(rng.randint(1, 1_000_000)) for _ in cols))

    return "\n".join(lines)


# Implementations:


def _pandas_df(csv_text: str) -> pd.DataFrame:
    """Load CSV into DataFrame using pandas.read_csv."""
    return pd.read_csv(io.StringIO(csv_text))


def _csvpp_df(csv_text: str) -> pd.DataFrame:
    """Load CSV into DataFrame using csvpp NumPy bridge."""
    arr, cols = csvpp.parse_csv_numpy(csv_text, skip_header=True)
    return pd.DataFrame(arr, columns=cols)


# Timing helper:


def _measure(
    fn: Callable[[str], pd.DataFrame],
    csv_text: str,
    iterations: int,
) -> tuple[float, pd.DataFrame]:
    """Return (avg_seconds, last_result). One warmup call before timing."""
    result = fn(csv_text)
    start = time.perf_counter()

    for _ in range(iterations):
        fn(csv_text)

    return (time.perf_counter() - start) / iterations, result


# Verification:


def _verify(df_pandas: pd.DataFrame, df_fast: pd.DataFrame) -> None:
    """Raise AssertionError if the two DataFrames disagree on shape or sums."""
    assert (
        df_pandas.shape == df_fast.shape
    ), f"Shape mismatch: pandas={df_pandas.shape} csvpp={df_fast.shape}"

    for col in df_pandas.columns:
        s_p = int(df_pandas[col].sum())
        s_f = int(df_fast[col].sum())
        assert (
            s_p == s_f
        ), f"Column '{col}' sum mismatch: pandas={s_p} csvpp={s_f}"


# Entry point:


def run(n_rows: int = 200_000, n_cols: int = 5, iterations: int = 10) -> None:
    print(f"Dataset:    {n_rows:,} rows × {n_cols} integer columns")
    print(f"Iterations: {iterations} per method\n")

    csv_text = generate_csv(n_rows, n_cols)

    parsers: list[tuple[str, Callable[[str], pd.DataFrame]]] = [
        ("pandas.read_csv", _pandas_df),
        ("csvpp (C++)", _csvpp_df),
    ]

    timings: dict[str, float] = {}
    results: list[pd.DataFrame] = []

    for name, fn in parsers:
        print(f"  {name:<24} ...", end="", flush=True)
        avg_s, df = _measure(fn, csv_text, iterations)
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

    speedup = timings["pandas.read_csv"] / timings["csvpp (C++)"]
    print(f"\ncsvpp is {speedup:.1f}× faster than pandas.read_csv")
    print(f"Output shape: {results[1].shape}  dtype: {results[1].dtypes.iloc[0].name}")


def _parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="CSV → DataFrame benchmark: pandas vs csvpp.",
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
    return p.parse_args()


if __name__ == "__main__":
    args = _parse_args()
    run(n_rows=args.rows, n_cols=args.cols, iterations=args.iterations)
