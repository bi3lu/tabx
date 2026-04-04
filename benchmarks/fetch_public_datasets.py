#!/usr/bin/env python3
"""Download public CSV datasets used by the benchmark workflow.

The goal is reproducibility, not massive scale. We intentionally fetch
moderate-size public CSVs that are stable and quick to download in CI.

Examples:
    python benchmarks/fetch_public_datasets.py
    python benchmarks/fetch_public_datasets.py --output-dir benchmarks/public_data
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path
from urllib.error import URLError
from urllib.request import urlopen

DATASETS: dict[str, str] = {
    # Mixed-type real-world aviation dataset.
    "flights-200k.csv": "https://raw.githubusercontent.com/vega/vega-datasets/master/data/flights-200k.csv",
    # Small realistic categorical + numeric dataset.
    "movies.csv": "https://raw.githubusercontent.com/vega/vega-datasets/master/data/movies.csv",
}


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fetch_dataset(url: str, output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with urlopen(url, timeout=60) as response:
        data = response.read()

    output_path.write_bytes(data)
    print(f"saved {output_path} ({len(data):,} bytes, sha256={_sha256(data)[:12]})")


def main() -> int:
    parser = argparse.ArgumentParser(description="Download public CSV datasets for benchmarking.")
    parser.add_argument(
        "--output-dir",
        default="benchmarks/public_data",
        help="Directory where downloaded files will be stored.",
    )
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    failures = 0

    for filename, url in DATASETS.items():
        try:
            fetch_dataset(url, output_dir / filename)

        except URLError as exc:
            failures += 1
            print(f"failed {filename}: {exc}", file=sys.stderr)

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
