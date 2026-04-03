# pyright: reportMissingImports=false

from __future__ import annotations

from pathlib import Path

import numpy as np
import pandas as pd
import pandas.testing as pdt
import pytest

import tabx


def _write_xlsx(path: Path, df: pd.DataFrame, sheet_name: str = "Sheet1") -> None:
    with pd.ExcelWriter(path, engine="openpyxl") as writer:
        df.to_excel(writer, index=False, sheet_name=sheet_name)


def test_list_xlsx_sheets_and_parse_numpy(tmp_path: Path) -> None:
    path = tmp_path / "ints.xlsx"
    source = pd.DataFrame({"id": [1, 2], "qty": [10, 20]})
    _write_xlsx(path, source, sheet_name="Data")

    sheets = tabx.list_xlsx_sheets(str(path))
    assert any(item["name"] == "Data" for item in sheets)

    arr, cols = tabx.parse_xlsx_numpy(str(path), sheet_name="Data", skip_header=True)
    assert arr.shape == (2, 2)
    assert arr.dtype == np.int32
    assert cols == ["id", "qty"]
    assert arr.tolist() == [[1, 10], [2, 20]]


def test_parse_xlsx_dataframe_mixed(tmp_path: Path) -> None:
    path = tmp_path / "mixed.xlsx"
    source = pd.DataFrame(
        {
            "ints": [1, 2],
            "floats": [1.5, np.nan],
            "strings": ["x", "y"],
            "bools": [True, False],
        }
    )
    _write_xlsx(path, source)

    result = tabx.parse_xlsx_dataframe(str(path), skip_header=True)
    pdt.assert_frame_equal(result, source, check_dtype=False)


def test_parse_xlsx_missing_sheet_raises(tmp_path: Path) -> None:
    path = tmp_path / "single.xlsx"
    _write_xlsx(path, pd.DataFrame({"value": [1]}), sheet_name="Only")

    with pytest.raises(RuntimeError, match="Sheet not found"):
        tabx.parse_xlsx_numpy(str(path), sheet_name="DoesNotExist", skip_header=True)


def test_parse_xlsx_numpy_rejects_empty_numeric_cell(tmp_path: Path) -> None:
    path = tmp_path / "empty_numeric.xlsx"
    _write_xlsx(path, pd.DataFrame({"id": [1, 2], "qty": [10, None]}), sheet_name="Data")

    with pytest.raises(RuntimeError, match="Empty XLSX cell is not allowed"):
        tabx.parse_xlsx_numpy(str(path), sheet_name="Data", skip_header=True)


def test_parse_xlsx_dataframe_handles_nullable_mixed_columns(tmp_path: Path) -> None:
    path = tmp_path / "mixed_nullable.xlsx"
    _write_xlsx(
        path,
        pd.DataFrame(
            {
                "id": [1, 2],
                "note": ["x", None],
                "active": [True, None],
            }
        ),
        sheet_name="Data",
    )

    result = tabx.parse_xlsx_dataframe(str(path), sheet_name="Data", skip_header=True)

    assert result["id"].tolist() == [1, 2]
    assert result["note"].iloc[0] == "x"
    assert pd.isna(result["note"].iloc[1])
    assert result["active"].iloc[0] is True
    assert pd.isna(result["active"].iloc[1])
