# pyright: reportMissingImports=false

from __future__ import annotations

import numpy as np
import pandas as pd
import pandas.testing as pdt
import pytest

import tabx


def test_parse_csv_numbers_and_sum() -> None:
    text = "10, 20, 30"
    assert tabx.parse_csv_numbers(text) == [10, 20, 30]
    assert tabx.sum_csv_numbers(text) == 60


def test_parse_csv_flat_and_sum_all() -> None:
    csv_text = "value\n100\n200\n300"
    assert tabx.parse_csv_flat(csv_text, skip_header=True) == [100, 200, 300]
    assert tabx.sum_csv_all(csv_text, skip_header=True) == 600


def test_parse_csv_numpy_with_header() -> None:
    csv_text = "id,qty\n1,2\n3,4"
    arr, cols = tabx.parse_csv_numpy(csv_text, skip_header=True)

    assert arr.shape == (2, 2)
    assert arr.dtype == np.int32
    assert cols == ["id", "qty"]
    assert arr.tolist() == [[1, 2], [3, 4]]


def test_parse_csv_dataframe_mixed_types() -> None:
    csv_text = "id,active,score,label\n1,true,10.5,alpha\n2,false,,beta"

    df = tabx.parse_csv_dataframe(csv_text, skip_header=True)
    expected = pd.DataFrame(
        {
            "id": [1, 2],
            "active": [True, False],
            "score": [10.5, np.nan],
            "label": ["alpha", "beta"],
        }
    )

    pdt.assert_frame_equal(df, expected, check_dtype=False)


def test_parse_csv_invalid_token_raises() -> None:
    with pytest.raises(ValueError, match="Invalid integer token"):
        tabx.parse_csv_numbers("1,abc,3")


def test_parse_csv_flat_ignores_blank_lines_and_whitespace() -> None:
    csv_text = "value\n\n  \n1\n 2 \n\n3\n"
    assert tabx.parse_csv_flat(csv_text, skip_header=True) == [1, 2, 3]
    assert tabx.sum_csv_all(csv_text, skip_header=True) == 6


def test_parse_csv_dataframe_case_insensitive_booleans_and_missing_numeric() -> None:
    csv_text = "flag,qty\nTRUE,1\nfalse,\nTrUe,3"
    result = tabx.parse_csv_dataframe(csv_text, skip_header=True)

    expected = pd.DataFrame(
        {
            "flag": [True, False, True],
            "qty": [1.0, np.nan, 3.0],
        }
    )
    pdt.assert_frame_equal(result, expected, check_dtype=False)


def test_parse_csv_dataframe_ragged_rows_fill_missing_with_null() -> None:
    csv_text = "a,b,c\n1,2,3\n4,5\n6,7,8"
    result = tabx.parse_csv_dataframe(csv_text, skip_header=True)

    expected = pd.DataFrame(
        {
            "a": [1, 4, 6],
            "b": [2, 5, 7],
            "c": [3.0, np.nan, 8.0],
        }
    )
    pdt.assert_frame_equal(result, expected, check_dtype=False)


def test_parse_csv_dataframe_strict_mode_rejects_ragged_rows() -> None:
    csv_text = "a,b,c\n1,2,3\n4,5"

    with pytest.raises(ValueError, match="CSV row width mismatch"):
        tabx.parse_csv_dataframe(csv_text, skip_header=True, shape_mode="strict")


def test_parse_csv_dataframe_permissive_mode_warns_on_ragged_rows() -> None:
    csv_text = "a,b,c\n1,2,3\n4,5"

    with pytest.warns(RuntimeWarning, match="Permissive CSV shape mode detected"):
        result = tabx.parse_csv_dataframe(
            csv_text,
            skip_header=True,
            shape_mode="permissive",
            warn_on_ragged=True,
        )

    expected = pd.DataFrame(
        {
            "a": [1, 4],
            "b": [2, 5],
            "c": [3.0, np.nan],
        }
    )
    pdt.assert_frame_equal(result, expected, check_dtype=False)


def test_parse_csv_numpy_rejects_ragged_rows() -> None:
    csv_text = "id,qty\n1,2\n3"

    with pytest.raises(ValueError, match="CSV row width mismatch"):
        tabx.parse_csv_numpy(csv_text, skip_header=True)
