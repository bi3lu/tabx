"""tabx — C++17 CSV integer parser with a zero-copy pandas bridge.

Install::

    pip install tabx

Quick start::

    import tabx

    # 3× faster than pd.read_csv — returns a real pd.DataFrame
    df = tabx.parse_csv_dataframe(csv_text)

    # Or get the raw NumPy array and column names directly:
    arr, cols = tabx.parse_csv_numpy(csv_text, skip_header=True)
"""

from __future__ import annotations

from typing import TYPE_CHECKING

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

if TYPE_CHECKING:
    import pandas as pd  # type: ignore[import-untyped]

__version__: str = "0.1.0"
__all__: list[str] = [
    "parse_csv_numbers",
    "sum_csv_numbers",
    "parse_csv_flat",
    "sum_csv_all",
    "parse_csv_numpy",
    "parse_csv_dataframe",
    "list_xlsx_sheets",
    "parse_xlsx_numpy",
    "parse_xlsx_dataframe",
]


def parse_csv_dataframe(csv_text: str, skip_header: bool = True) -> "pd.DataFrame":
    """Parse CSV text directly into a ``pd.DataFrame`` with inferred column types.

    Type inference is performed per column, similarly to ``pandas.read_csv``:

    * All integers, no nulls -> ``int64``
    * Integers/floats with nulls -> ``float64``
    * All booleans -> ``bool``
    * Strings or mixed values -> ``object``

    Args:
        csv_text: Full CSV text with rows separated by ``'\\n'``.
        skip_header: When ``True`` (default), the first row is used as
            column names.  When ``False``, columns are numbered 0, 1, 2, …

    Returns:
        A ``pd.DataFrame`` with per-column inferred dtypes.

    Raises:
        ImportError: If ``pandas`` is not installed.
    """
    try:
        import pandas as pd

    except ImportError as exc:
        raise ImportError(
            "pandas is required for parse_csv_dataframe. "
            "Install it with: pip install pandas"
        ) from exc

    columns, col_names = parse_csv_mixed(csv_text, skip_header=skip_header)

    return pd.DataFrame(dict(zip(col_names, columns)))


def parse_xlsx_dataframe(
    file_path: str,
    sheet_name: str = "",
    skip_header: bool = True,
) -> "pd.DataFrame":
    """Parse an integer XLSX worksheet directly into a ``pd.DataFrame``.

    Args:
        file_path: Path to an ``.xlsx`` file.
        sheet_name: Worksheet name. Empty string means the first sheet.
        skip_header: When ``True`` (default), first row becomes column names.

    Returns:
        ``pd.DataFrame`` with ``int32`` columns.

    Raises:
        RuntimeError: On malformed XLSX, unsupported cell types, or parse errors.
        ImportError: If ``pandas`` is not installed.
    """
    try:
        import pandas as pd

    except ImportError as exc:
        raise ImportError(
            "pandas is required for parse_xlsx_dataframe. "
            "Install it with: pip install pandas"
        ) from exc

    arr, cols = parse_xlsx_numpy(
        file_path=file_path,
        sheet_name=sheet_name,
        skip_header=skip_header,
    )

    return pd.DataFrame(arr, columns=cols)


def parse_xlsx_dataframe(
    file_path: str,
    sheet_name: str = "",
    skip_header: bool = True,
) -> "pd.DataFrame":
    """Parse an XLSX worksheet into a ``pd.DataFrame`` with inferred column types.

    Column types are inferred per-column, mirroring ``pandas.read_excel``:

    * All integers, no nulls → ``int64``
    * Integers/floats, with possible empty cells → ``float64`` (empty → NaN)
    * All booleans → ``bool``
    * Any strings or mixed types → ``object``

    Args:
        file_path: Path to an ``.xlsx`` file.
        sheet_name: Worksheet name. Empty string means the first sheet.
        skip_header: When ``True`` (default), first row becomes column names.

    Returns:
        ``pd.DataFrame`` with per-column inferred dtypes.

    Raises:
        RuntimeError: On malformed XLSX or ZIP/XML parse errors.
        ImportError: If ``pandas`` is not installed.
    """
    try:
        import pandas as pd

    except ImportError as exc:
        raise ImportError(
            "pandas is required for parse_xlsx_dataframe. "
            "Install it with: pip install pandas"
        ) from exc

    columns, col_names = parse_xlsx_mixed(
        file_path=file_path,
        sheet_name=sheet_name,
        skip_header=skip_header,
    )

    return pd.DataFrame(dict(zip(col_names, columns)))
