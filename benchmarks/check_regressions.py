#!/usr/bin/env python3
"""Fail benchmark CI when tabx regresses beyond configured ratio thresholds.

The input is one or more benchmark JSON files emitted by benchmarks/benchmark.py.
Each record must contain:
    - title
    - tabx_vs_pandas_ratio  (pandas_time / tabx_time)

A ratio >= 1.0 means tabx is faster than pandas.
A ratio < 1.0 means tabx is slower.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

# Minimum acceptable (pandas / tabx) ratio per benchmark title.
# Example: 0.45 means tabx may be at most ~2.22x slower than pandas.
MIN_RATIO_BY_TITLE: dict[str, float] = {
    # Baseline after additional classify + trim + DataFrame(copy=False) tuning:
    # mixed ~= 0.86, quoted ~= 1.16 (100k rows, 5 iterations).
    # Thresholds keep modest CI variance headroom.
    "CSV — mixed realistic  (int · float · bool · string · nulls)": 0.80,
    'CSV — RFC 4180 quoted-heavy  ("Last, First" · escaped "" · )': 1.05,
    "CSV — wide sparse  (~60 % null cells)": 0.45,
}


def load_records(paths: list[Path]) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []

    for path in paths:
        data = json.loads(path.read_text(encoding="utf-8"))

        if not isinstance(data, list):
            raise ValueError(f"Expected list in {path}, got {type(data).__name__}")

        for item in data:
            if not isinstance(item, dict):
                raise ValueError(f"Expected dict record in {path}, got {type(item).__name__}")

            records.append(item)

    return records


def main() -> int:
    parser = argparse.ArgumentParser(description="Check benchmark regression thresholds.")
    parser.add_argument(
        "json_files",
        nargs="+",
        help="Benchmark JSON files produced by benchmarks/benchmark.py",
    )
    args = parser.parse_args()

    records = load_records([Path(p) for p in args.json_files])
    failures: list[str] = []

    for rec in records:
        title = str(rec.get("title", ""))

        if title not in MIN_RATIO_BY_TITLE:
            continue

        ratio_raw = rec.get("tabx_vs_pandas_ratio")

        if not isinstance(ratio_raw, (int, float)):
            failures.append(f"{title}: missing numeric tabx_vs_pandas_ratio")
            continue

        ratio = float(ratio_raw)
        min_ratio = MIN_RATIO_BY_TITLE[title]

        if ratio < min_ratio:
            failures.append(
                f"{title}: ratio={ratio:.3f} < min={min_ratio:.3f} "
                f"(tabx too slow; allowed max slowdown ~{1.0 / min_ratio:.2f}x)"
            )

    if failures:
        print("Benchmark regression check FAILED:")

        for msg in failures:
            print(f"  - {msg}")

        return 1

    print("Benchmark regression check passed.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
