from __future__ import annotations

import importlib
from typing import TYPE_CHECKING, Any, Literal

if TYPE_CHECKING:
    import pandas as pd


CsvShapeMode = Literal["strict", "permissive"]
CsvEngineMode = Literal["auto", "infer", "int-fast", "schema"]


def _looks_integer_only_csv(csv_text: str, delimiter: str, skip_header: bool) -> bool:
    """Heuristic sniff for int-only CSV suitable for parse_csv_numpy fast path."""
    if '"' in csv_text:
        return False

    lines = [ln for ln in csv_text.splitlines() if ln.strip()]
    if not lines:
        return False

    data_lines = lines[1:] if skip_header else lines
    if not data_lines:
        return False

    for line in data_lines[:64]:
        parts = line.split(delimiter)
        if not parts:
            return False

        for token in parts:
            t = token.strip()
            if not t:
                return False

            if t[0] in "+-":
                if len(t) == 1:
                    return False
                t = t[1:]

            if not t.isdigit():
                return False

    return True


def _require_pandas() -> "type[pd.DataFrame]":
    try:
        import pandas as pd

    except ImportError as exc:
        raise ImportError(
            "pandas is required for DataFrame parsing helpers. "
            "Install it with: pip install pandas"
        ) from exc

    return pd.DataFrame


def _core_module() -> Any:
    return importlib.import_module("tabx._core")


def parse_csv_dataframe(
    csv_text: str,
    skip_header: bool = True,
    shape_mode: CsvShapeMode = "permissive",
    warn_on_ragged: bool = False,
    delimiter: str = ",",
    quote: str = '"',
    trim_whitespace: bool = True,
    schema: list[str] | None = None,
    engine: CsvEngineMode = "auto",
) -> "pd.DataFrame":
    """Parse CSV text directly into a ``pd.DataFrame`` with inferred column types.

    Fully RFC 4180 compliant: handles quoted fields containing commas or
    embedded newlines, and escaped double-quotes (``""`` sequences).  Unquoted empty
    fields become ``NaN``; quoted empty fields (``""``) become empty strings.
    """
    dataframe_type = _require_pandas()
    core = _core_module()

    if engine not in {"auto", "infer", "int-fast", "schema"}:
        raise ValueError("engine must be one of: auto, infer, int-fast, schema")

    if engine == "schema" and not schema:
        raise ValueError("engine='schema' requires schema")

    if engine == "int-fast" or (
        engine == "auto"
        and schema is None
        and delimiter == ","
        and quote == '"'
        and trim_whitespace
        and not warn_on_ragged
        and shape_mode == "strict"
        and _looks_integer_only_csv(csv_text, delimiter=delimiter, skip_header=skip_header)
    ):
        arr, col_names = core.parse_csv_numpy(csv_text, skip_header=skip_header)
        return dataframe_type(arr, columns=col_names, copy=False)

    columns, col_names = core.parse_csv_mixed(
        csv_text,
        skip_header=skip_header,
        shape_mode=shape_mode,
        warn_on_ragged=warn_on_ragged,
        delimiter=delimiter,
        quote=quote,
        trim_whitespace=trim_whitespace,
        schema=schema,
    )
    return dataframe_type(dict(zip(col_names, columns)), copy=False)


def parse_xlsx_dataframe(
    file_path: str,
    sheet_name: str = "",
    skip_header: bool = True,
) -> "pd.DataFrame":
    """Parse an XLSX worksheet into a ``pd.DataFrame`` with inferred column types."""
    dataframe_type = _require_pandas()
    core = _core_module()
    columns, col_names = core.parse_xlsx_mixed(
        file_path=file_path,
        sheet_name=sheet_name,
        skip_header=skip_header,
    )

    return dataframe_type(dict(zip(col_names, columns)))
