"""tabx — C++17 CSV/XLSX parser with NumPy and pandas bridges.

Install::

    pip install tabx

Quick start::

    import tabx

    # Parse CSV directly into a real pandas DataFrame
    df = tabx.parse_csv_dataframe(csv_text)

    # Notebook-friendly file loader:
    df2 = tabx.parse_csv_file_dataframe("data/input.csv")

    # Or get the raw NumPy array and column names directly:
    arr, cols = tabx.parse_csv_numpy(csv_text, skip_header=True)
"""

from __future__ import annotations

from ._core import (
    list_xlsx_sheets,
    parse_csv_flat,
    parse_csv_mixed,
    parse_csv_numbers,
    parse_csv_numpy,
    parse_xlsx_mixed,
    parse_xlsx_numpy,
    sum_csv_all,
    sum_csv_numbers,
)
from .dataframe import parse_csv_dataframe, parse_csv_file_dataframe, parse_xlsx_dataframe

__version__: str = "0.1.0"
__all__: list[str] = [
    "parse_csv_numbers",
    "sum_csv_numbers",
    "parse_csv_flat",
    "sum_csv_all",
    "parse_csv_numpy",
    "parse_csv_mixed",
    "parse_csv_dataframe",
    "parse_csv_file_dataframe",
    "list_xlsx_sheets",
    "parse_xlsx_numpy",
    "parse_xlsx_mixed",
    "parse_xlsx_dataframe",
]
