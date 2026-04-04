from __future__ import annotations

import importlib
from collections.abc import Callable
from pathlib import Path
from typing import TYPE_CHECKING, Any, Literal, Union, cast

from typing_extensions import TypeAlias

if TYPE_CHECKING:
    import pandas as pd


CsvShapeMode = Literal["strict", "permissive"]
CsvEngineMode = Literal["auto", "infer", "int-fast", "schema"]
CsvUseCols: TypeAlias = Union[list[str], list[int], Callable[[object], bool]]
CsvNaValuesScalar: TypeAlias = Union[str, list[str]]
CsvNaValues: TypeAlias = Union[CsvNaValuesScalar, dict[str, CsvNaValuesScalar]]


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


def _core_supports_schema(core: Any) -> bool:
    try:
        core.parse_csv_mixed(
            "a\n1\n",
            skip_header=True,
            shape_mode="strict",
            warn_on_ragged=False,
            delimiter=",",
            quote='"',
            trim_whitespace=True,
            schema=["int"],
        )
        return True
    except TypeError:
        return False


def _apply_usecols(
    df: "pd.DataFrame",
    usecols: CsvUseCols,
) -> "pd.DataFrame":
    if callable(usecols):
        selected: list[object] = []
        for name in df.columns:
            try:
                keep = bool(usecols(name))
            except TypeError:
                keep = bool(usecols(str(name)))

            if keep:
                selected.append(name)

        return cast("pd.DataFrame", df.loc[:, selected])

    if not usecols:
        return df

    if all(isinstance(c, str) for c in usecols):
        requested = list(usecols)
        missing = [col for col in requested if col not in df.columns]
        if missing:
            available = [str(col) for col in df.columns]
            raise ValueError(
                "usecols columns not found: "
                f"{missing}. Available columns: {available}"
            )

        return cast("pd.DataFrame", df.loc[:, requested])

    if all(isinstance(c, int) for c in usecols):
        positions = cast(list[int], usecols)
        min_pos = min(positions)
        max_pos = max(positions)
        if min_pos < 0:
            raise ValueError(f"usecols index out of range: {min_pos}")
        if max_pos >= len(df.columns):
            raise ValueError(f"usecols index out of range: {max_pos}")
        cols = [df.columns[i] for i in positions]
        return cast("pd.DataFrame", df.loc[:, cols])

    raise ValueError("usecols must contain only str or only int")


def _apply_na_values(
    df: "pd.DataFrame",
    na_values: CsvNaValues,
) -> "pd.DataFrame":
    out = df.copy()

    if isinstance(na_values, dict):
        for col, values in na_values.items():
            if col not in out.columns:
                continue

            tokens = {values} if isinstance(values, str) else set(values)
            if not tokens:
                continue

            series = out[col]
            if str(series.dtype) in {"object", "string", "str"}:
                out[col] = series.where(~series.isin(tokens))

        return cast("pd.DataFrame", out)

    tokens = {na_values} if isinstance(na_values, str) else set(na_values)
    if not tokens:
        return df

    for col in out.columns:
        series = out[col]
        if str(series.dtype) in {"object", "string", "str"}:
            out[col] = series.where(~series.isin(tokens))

    return cast("pd.DataFrame", out)


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
    usecols: CsvUseCols | None = None,
    nrows: int | None = None,
    na_values: CsvNaValues | None = None,
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

    if nrows is not None and nrows < 0:
        raise ValueError("nrows must be >= 0")

    schema_supported = _core_supports_schema(core)
    if schema is not None and not schema_supported:
        if engine == "schema":
            raise ValueError(
                "engine='schema' is unavailable in the current native build. "
                "Reinstall tabx from current sources."
            )
        schema = None

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
        df = dataframe_type(arr, columns=col_names, copy=False)
    else:
        mixed_kwargs: dict[str, Any] = {
            "skip_header": skip_header,
            "shape_mode": shape_mode,
            "warn_on_ragged": warn_on_ragged,
            "delimiter": delimiter,
            "quote": quote,
            "trim_whitespace": trim_whitespace,
        }
        if schema is not None:
            mixed_kwargs["schema"] = schema

        columns, col_names = core.parse_csv_mixed(csv_text, **mixed_kwargs)
        df = dataframe_type(dict(zip(col_names, columns)), copy=False)

    if usecols is not None:
        df = _apply_usecols(df, usecols)

    if na_values is not None:
        df = _apply_na_values(df, na_values)

    if nrows is not None:
        df = df.head(nrows)

    return df


def parse_csv_file_dataframe(
    file_path: str,
    skip_header: bool = True,
    shape_mode: CsvShapeMode = "permissive",
    warn_on_ragged: bool = False,
    delimiter: str = ",",
    quote: str = '"',
    trim_whitespace: bool = True,
    schema: list[str] | None = None,
    engine: CsvEngineMode = "auto",
    encoding: str = "utf-8",
    usecols: CsvUseCols | None = None,
    nrows: int | None = None,
    na_values: CsvNaValues | None = None,
) -> "pd.DataFrame":
    """Parse a CSV file directly into a ``pd.DataFrame`` for notebook workflows."""
    csv_text = Path(file_path).read_text(encoding=encoding)
    return parse_csv_dataframe(
        csv_text,
        skip_header=skip_header,
        shape_mode=shape_mode,
        warn_on_ragged=warn_on_ragged,
        delimiter=delimiter,
        quote=quote,
        trim_whitespace=trim_whitespace,
        schema=schema,
        engine=engine,
        usecols=usecols,
        nrows=nrows,
        na_values=na_values,
    )


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
