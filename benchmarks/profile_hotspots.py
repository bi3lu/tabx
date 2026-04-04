#!/usr/bin/env python3
"""Render a cProfile .prof file into a human-readable hotspot report."""

from __future__ import annotations

import argparse
import io
import pstats
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert cProfile output to text report.")
    parser.add_argument("prof_file", help="Path to .prof file")
    parser.add_argument("--output", required=True, help="Path to output .txt report")
    parser.add_argument("--top", type=int, default=60, help="How many functions to include")
    args = parser.parse_args()

    stream = io.StringIO()
    stats = pstats.Stats(args.prof_file, stream=stream)
    stats.strip_dirs().sort_stats("cumtime").print_stats(args.top)

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(stream.getvalue(), encoding="utf-8")
    print(f"Wrote hotspot report: {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
