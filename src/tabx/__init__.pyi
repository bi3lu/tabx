from __future__ import annotations

from typing import Any, Callable, Literal

from ._core import list_xlsx_sheets as list_xlsx_sheets
from ._core import parse_csv_flat as parse_csv_flat
from ._core import parse_csv_mixed as parse_csv_mixed
from ._core import parse_csv_numbers as parse_csv_numbers
from ._core import parse_csv_numpy as parse_csv_numpy
from ._core import parse_xlsx_mixed as parse_xlsx_mixed
from ._core import parse_xlsx_numpy as parse_xlsx_numpy
from ._core import sum_csv_all as sum_csv_all
from ._core import sum_csv_numbers as sum_csv_numbers

DataFrameLike = Any
CsvShapeMode = Literal["strict", "permissive"]
CsvEngineMode = Literal["auto", "infer", "int-fast", "schema"]
CsvUseCols = list[str] | list[int] | Callable[[object], bool]
CsvNaValuesScalar = str | list[str]
CsvNaValues = CsvNaValuesScalar | dict[str, CsvNaValuesScalar]

__version__: str
__all__: list[str]

def parse_csv_dataframe(
    csv_text: str,
    skip_header: bool = ...,
    shape_mode: CsvShapeMode = ...,
    warn_on_ragged: bool = ...,
    delimiter: str = ...,
    quote: str = ...,
    trim_whitespace: bool = ...,
    schema: list[str] | None = ...,
    engine: CsvEngineMode = ...,
    usecols: CsvUseCols | None = ...,
    nrows: int | None = ...,
    na_values: CsvNaValues | None = ...,
) -> DataFrameLike:
    """Parse CSV text directly into a ``pd.DataFrame`` with inferred column types.

    Fully RFC 4180 compliant: handles quoted fields containing commas or
    embedded newlines, and escaped double-quotes (``""`` sequences).  A single-pass
    state machine processes the whole document with no per-row allocations.

    Column types are inferred per-column similarly to ``pandas.read_csv``:

    * All integers, no nulls -> ``int64``
    * Integers/floats with nulls -> ``float64``
    * All booleans -> ``bool``
    * Strings or mixed values -> ``object``

    Unquoted empty fields become ``None``/``NaN``; quoted empty fields
    (``""``) become empty strings.

    Args:
        csv_text: Full CSV text.  Line endings may be LF or CRLF.
        skip_header: When ``True`` (default), the first row is used as
            column names.  When ``False``, columns are numbered 0, 1, 2, …
        shape_mode: ``"strict"`` rejects ragged rows, ``"permissive"`` pads
            missing cells with null-like values.
        warn_on_ragged: Emit ``RuntimeWarning`` when permissive mode encounters
            inconsistent row widths.
        delimiter: Single-character field separator.  Defaults to ``','``.
        quote: Single-character quoting character.  Defaults to ``'"'``.
        trim_whitespace: Strip leading/trailing ASCII whitespace from unquoted
            fields.  Quoted field content is always preserved verbatim.
            Defaults to ``True``.
        schema: Optional fixed column schema for ultra-fast strict parsing.
            Allowed names: ``int``, ``float``, ``bool``, ``string``.
        engine: ``"auto"`` (default) routes between infer and int-fast,
            ``"infer"`` forces mixed inference, ``"int-fast"`` forces
            integer NumPy path, ``"schema"`` requires ``schema`` and forces
            schema-aware parsing.

    Returns:
        A ``pd.DataFrame`` with per-column inferred dtypes.

    Raises:
        ImportError: If ``pandas`` is not installed.
        ValueError: If ``delimiter`` or ``quote`` is not a single character.
    """
    ...

def parse_csv_file_dataframe(
    file_path: str,
    skip_header: bool = ...,
    shape_mode: CsvShapeMode = ...,
    warn_on_ragged: bool = ...,
    delimiter: str = ...,
    quote: str = ...,
    trim_whitespace: bool = ...,
    schema: list[str] | None = ...,
    engine: CsvEngineMode = ...,
    encoding: str = ...,
    usecols: CsvUseCols | None = ...,
    nrows: int | None = ...,
    na_values: CsvNaValues | None = ...,
) -> DataFrameLike:
    """Parse CSV file directly into a ``pd.DataFrame``.

    This is a notebook-friendly wrapper around ``parse_csv_dataframe`` that
    reads text from ``file_path`` and applies the same parsing options.
    """
    ...

def parse_xlsx_dataframe(
    file_path: str,
    sheet_name: str = ...,
    skip_header: bool = ...,
) -> DataFrameLike:
    """Parse an XLSX worksheet into a ``pd.DataFrame`` with inferred column types.

    Column types are inferred per-column, mirroring ``pandas.read_excel``:

    * All integers, no nulls → ``int64``
    * Integers/floats with possible empty cells → ``float64`` (empty → NaN)
    * All booleans → ``bool``
    * Any strings or mixed types → ``object``

    Args:
        file_path: Path to an ``.xlsx`` file.
        sheet_name: Worksheet name. Empty string selects the first sheet.
        skip_header: When ``True`` (default), first row becomes column names.

    Returns:
        A ``pd.DataFrame`` with per-column inferred dtypes.
    """
    ...
