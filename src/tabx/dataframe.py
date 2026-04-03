from __future__ import annotations

import importlib
from typing import TYPE_CHECKING, Any, Literal

if TYPE_CHECKING:
    import pandas as pd


CsvShapeMode = Literal["strict", "permissive"]


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
) -> "pd.DataFrame":
    """Parse CSV text directly into a ``pd.DataFrame`` with inferred column types."""
    dataframe_type = _require_pandas()
    core = _core_module()
    columns, col_names = core.parse_csv_mixed(
        csv_text,
        skip_header=skip_header,
        shape_mode=shape_mode,
        warn_on_ragged=warn_on_ragged,
    )
    return dataframe_type(dict(zip(col_names, columns)))


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
