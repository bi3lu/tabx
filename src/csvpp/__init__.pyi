from __future__ import annotations

from typing import TYPE_CHECKING

from ._core import parse_csv_flat as parse_csv_flat
from ._core import parse_csv_numbers as parse_csv_numbers
from ._core import parse_csv_numpy as parse_csv_numpy
from ._core import sum_csv_all as sum_csv_all
from ._core import sum_csv_numbers as sum_csv_numbers

if TYPE_CHECKING:
    import pandas as pd

__version__: str
__all__: list[str]

def parse_csv_dataframe(
    csv_text: str,
    skip_header: bool = ...,
) -> "pd.DataFrame":
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
