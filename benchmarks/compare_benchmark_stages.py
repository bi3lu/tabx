#!/usr/bin/env python3
"""Compare two benchmark JSON artifacts produced by benchmark.py.

Usage:
    python benchmarks/compare_benchmark_stages.py \
        --baseline old/csv-all.json \
        --candidate new/csv-all.json \
        --output comparison.txt
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import cast


def load_by_title(path: Path) -> dict[str, dict[str, object]]:
    records = json.loads(path.read_text(encoding="utf-8"))

    if not isinstance(records, list):
        raise ValueError(f"Expected list in {path}, got {type(records).__name__}")

    out: dict[str, dict[str, object]] = {}

    for rec in records:
        if not isinstance(rec, dict):
            continue

        title = rec.get("title")

        if isinstance(title, str):
            out[title] = rec

    return out


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare two benchmark JSON artifacts.")
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--output", default="")
    parser.add_argument(
        "--fail-on-regression",
        action="store_true",
        help="Exit with code 1 when any compared title gets worse ratio.",
    )
    args = parser.parse_args()

    baseline = load_by_title(Path(args.baseline))
    candidate = load_by_title(Path(args.candidate))

    titles = sorted(set(baseline.keys()) & set(candidate.keys()))
    lines: list[str] = []
    lines.append("title | ratio_old | ratio_new | delta | status")
    lines.append("----- | --------- | --------- | ----- | ------")

    regressions = 0

    for title in titles:
        old_raw = baseline[title].get("tabx_vs_pandas_ratio", 0.0)
        new_raw = candidate[title].get("tabx_vs_pandas_ratio", 0.0)
        old_ratio = float(cast(float, old_raw))
        new_ratio = float(cast(float, new_raw))
        delta = new_ratio - old_ratio

        if delta > 1e-9:
            status = "improved"

        elif delta < -1e-9:
            status = "regressed"
            regressions += 1

        else:
            status = "unchanged"

        lines.append(f"{title} | {old_ratio:.3f} | {new_ratio:.3f} | {delta:+.3f} | {status}")

    text = "\n".join(lines)
    print(text)

    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text + "\n", encoding="utf-8")

    if args.fail_on_regression and regressions > 0:
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
