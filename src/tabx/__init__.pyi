from __future__ import annotations

from typing import Any

from ._core import list_xlsx_sheets as list_xlsx_sheets
from ._core import parse_csv_flat as parse_csv_flat
from ._core import parse_csv_numbers as parse_csv_numbers
from ._core import parse_csv_numpy as parse_csv_numpy
from ._core import parse_xlsx_numpy as parse_xlsx_numpy
from ._core import sum_csv_all as sum_csv_all
from ._core import sum_csv_numbers as sum_csv_numbers

DataFrameLike = Any

__version__: str
__all__: list[str]

def parse_csv_dataframe(
    csv_text: str,
    skip_header: bool = ...,
) -> DataFrameLike:
    """Parse an integer CSV string directly into a ``pd.DataFrame``.

    Approximately 3× faster than ``pd.read_csv(io.StringIO(csv_text))``
    on integer-only CSVs.

    Args:
        csv_text: Full CSV text with rows separated by ``'\\n'``.
        skip_header: When ``True`` (default), the first row is used as
            column names.  When ``False``, columns are numbered 0, 1, 2, …

    Returns:
        A ``pd.DataFrame`` with ``int32`` columns.

    Raises:
        ValueError: If any field is empty or cannot be parsed as an integer.
        ImportError: If ``pandas`` is not installed.
    """
    ...

def parse_xlsx_dataframe(
    file_path: str,
    sheet_name: str = ...,
    skip_header: bool = ...,
) -> DataFrameLike:
    """Parse an integer XLSX worksheet directly into a ``pd.DataFrame``.

    Args:
        file_path: Path to an ``.xlsx`` file.
        sheet_name: Worksheet name. Empty string selects the first sheet.
        skip_header: When ``True``, first row becomes column names.

    Returns:
        A ``pd.DataFrame`` with ``int32`` columns.
    """
    ...
