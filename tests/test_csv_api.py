# pyright: reportMissingImports=false

from __future__ import annotations

from pathlib import Path

import numpy as np
import pandas as pd
import pandas.testing as pdt
import pytest

import tabx
from tabx import dataframe as dataframe_mod


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


def test_parse_csv_dataframe_schema_aware_fast_path() -> None:
    core = dataframe_mod._core_module()
    csv_text = "id,active,score,label\n1,true,10.5,alpha\n2,false,,beta"

    if not dataframe_mod._core_supports_schema(core):
        with pytest.raises(ValueError, match="engine='schema' is unavailable"):
            tabx.parse_csv_dataframe(
                csv_text,
                skip_header=True,
                schema=["int", "bool", "float", "string"],
                engine="schema",
                shape_mode="strict",
            )

        # In infer mode, unsupported schema is ignored and parsing still works.
        fallback_df = tabx.parse_csv_dataframe(
            csv_text,
            skip_header=True,
            schema=["int", "bool", "float", "string"],
            engine="infer",
        )
        assert fallback_df.shape == (2, 4)
        return

    df = tabx.parse_csv_dataframe(
        csv_text,
        skip_header=True,
        schema=["int", "bool", "float", "string"],
        engine="schema",
        shape_mode="strict",
    )

    expected = pd.DataFrame(
        {
            "id": [1, 2],
            "active": [True, False],
            "score": [10.5, np.nan],
            "label": ["alpha", "beta"],
        }
    )
    pdt.assert_frame_equal(df, expected, check_dtype=False)


def test_parse_csv_dataframe_engine_int_fast() -> None:
    csv_text = "id,qty\n1,2\n3,4\n"

    df = tabx.parse_csv_dataframe(
        csv_text,
        skip_header=True,
        engine="int-fast",
    )

    expected = pd.DataFrame({"id": [1, 3], "qty": [2, 4]})
    pdt.assert_frame_equal(df, expected, check_dtype=False)


def test_parse_csv_dataframe_invalid_engine_raises() -> None:
    with pytest.raises(ValueError, match="engine must be one of"):
        tabx.parse_csv_dataframe("a,b\n1,2\n", engine="invalid")  # type: ignore[arg-type]


def test_parse_csv_dataframe_schema_engine_requires_schema() -> None:
    with pytest.raises(ValueError, match="requires schema"):
        tabx.parse_csv_dataframe("a,b\n1,2\n", engine="schema", schema=None)


def test_parse_csv_dataframe_auto_engine_can_route_int_fast() -> None:
    csv_text = "id,qty\n1,2\n3,4\n"

    df = tabx.parse_csv_dataframe(
        csv_text,
        skip_header=True,
        engine="auto",
        shape_mode="strict",
    )

    expected = pd.DataFrame({"id": [1, 3], "qty": [2, 4]})
    pdt.assert_frame_equal(df, expected, check_dtype=False)


def test_integer_only_sniff_helper_branches() -> None:
    assert not dataframe_mod._looks_integer_only_csv('a,b\n"1",2\n', ",", True)
    assert not dataframe_mod._looks_integer_only_csv("\n \n", ",", True)
    assert not dataframe_mod._looks_integer_only_csv("a,b\n", ",", True)
    assert not dataframe_mod._looks_integer_only_csv("a,b\n1,\n", ",", True)
    assert not dataframe_mod._looks_integer_only_csv("a,b\n-,2\n", ",", True)
    assert not dataframe_mod._looks_integer_only_csv("a,b\n1,2.5\n", ",", True)
    assert dataframe_mod._looks_integer_only_csv("a,b\n+1,-2\n", ",", True)


def test_parse_csv_file_dataframe_notebook_style(tmp_path: Path) -> None:
    csv_path = tmp_path / "demo.csv"
    csv_path.write_text("id,qty,status\n1,2,ok\n2,3,pending\n", encoding="utf-8")

    df = tabx.parse_csv_file_dataframe(str(csv_path))
    expected = pd.DataFrame({"id": [1, 2], "qty": [2, 3], "status": ["ok", "pending"]})
    pdt.assert_frame_equal(df, expected, check_dtype=False)


def test_parse_csv_dataframe_usecols_and_nrows() -> None:
    csv_text = "id,qty,price,status\n1,2,10.5,ok\n2,3,20.0,pending\n3,4,30.5,ok\n"
    df = tabx.parse_csv_dataframe(csv_text, usecols=["id", "status"], nrows=2)

    expected = pd.DataFrame({"id": [1, 2], "status": ["ok", "pending"]})
    pdt.assert_frame_equal(df, expected, check_dtype=False)


def test_parse_csv_dataframe_na_values_simple() -> None:
    csv_text = "id,note\n1,ok\n2,NA\n3,NULL\n"
    df = tabx.parse_csv_dataframe(csv_text, na_values=["NA", "NULL"])

    assert pd.isna(df["note"].iloc[1])
    assert pd.isna(df["note"].iloc[2])


def test_parse_csv_dataframe_usecols_callable() -> None:
    csv_text = "id,qty,price,status\n1,2,10.5,ok\n2,3,20.0,pending\n"
    df = tabx.parse_csv_dataframe(csv_text, usecols=lambda c: c in {"id", "price"})

    expected = pd.DataFrame({"id": [1, 2], "price": [10.5, 20.0]})
    pdt.assert_frame_equal(df, expected, check_dtype=False)


def test_parse_csv_dataframe_na_values_per_column() -> None:
    csv_text = "id,note,status\n1,NA,ok\n2,ok,NULL\n3,NULL,pending\n"
    df = tabx.parse_csv_dataframe(
        csv_text,
        na_values={"note": ["NA"], "status": ["NULL"]},
    )

    assert pd.isna(df["note"].iloc[0])
    assert not pd.isna(df["note"].iloc[2])
    assert pd.isna(df["status"].iloc[1])
    assert not pd.isna(df["status"].iloc[2])


def test_parse_csv_dataframe_usecols_mixed_types_rejected() -> None:
    csv_text = "id,qty\n1,2\n"
    with pytest.raises(ValueError, match="usecols must contain only str or only int"):
        tabx.parse_csv_dataframe(csv_text, usecols=["id", 1])  # type: ignore[list-item]


def test_parse_csv_dataframe_usecols_empty_list_returns_same_columns() -> None:
    csv_text = "id,qty\n1,2\n"
    df = tabx.parse_csv_dataframe(csv_text, usecols=[])
    expected = pd.DataFrame({"id": [1], "qty": [2]})
    pdt.assert_frame_equal(df, expected, check_dtype=False)


def test_parse_csv_dataframe_usecols_missing_column_has_clear_error() -> None:
    csv_text = "id,qty\n1,2\n"
    with pytest.raises(ValueError, match="usecols columns not found"):
        tabx.parse_csv_dataframe(csv_text, usecols=["id", "price"])


def test_parse_csv_dataframe_usecols_int_positions() -> None:
    csv_text = "id,qty,price\n1,2,10.5\n"
    df = tabx.parse_csv_dataframe(csv_text, usecols=[0, 2])
    expected = pd.DataFrame({"id": [1], "price": [10.5]})
    pdt.assert_frame_equal(df, expected, check_dtype=False)


def test_parse_csv_dataframe_usecols_negative_index_rejected() -> None:
    csv_text = "id,qty\n1,2\n"
    with pytest.raises(ValueError, match="usecols index out of range: -1"):
        tabx.parse_csv_dataframe(csv_text, usecols=[-1])


def test_apply_usecols_callable_typeerror_fallback_to_str() -> None:
    frame = pd.DataFrame({0: [1], 1: [2]})

    def only_str_prefix(col: object) -> bool:
        if not isinstance(col, str):
            raise TypeError("expected string")
        return col == "0"

    selected = dataframe_mod._apply_usecols(frame, only_str_prefix)
    expected = pd.DataFrame({0: [1]})
    pdt.assert_frame_equal(selected, expected, check_dtype=False)


def test_apply_na_values_handles_empty_and_unknown_keys() -> None:
    frame = pd.DataFrame({"note": ["ok", "NA"]})
    # Empty token set for existing column and unknown column key should be no-op.
    out = dataframe_mod._apply_na_values(frame, {"note": [], "unknown": ["NA"]})
    pdt.assert_frame_equal(out, frame, check_dtype=False)


def test_apply_na_values_empty_global_tokens_returns_original_object() -> None:
    frame = pd.DataFrame({"note": ["ok"]})
    out = dataframe_mod._apply_na_values(frame, [])
    assert out is frame


def test_core_supports_schema_false_on_typeerror() -> None:
    class FakeCore:
        def parse_csv_mixed(self, *_args: object, **_kwargs: object) -> None:
            raise TypeError("schema unsupported")

    assert dataframe_mod._core_supports_schema(FakeCore()) is False


def test_parse_csv_dataframe_schema_forced_unavailable_branch(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    csv_text = "id\n1\n"
    monkeypatch.setattr(dataframe_mod, "_core_supports_schema", lambda _core: False)

    with pytest.raises(ValueError, match="engine='schema' is unavailable"):
        tabx.parse_csv_dataframe(
            csv_text,
            skip_header=True,
            schema=["int"],
            engine="schema",
            shape_mode="strict",
        )


def test_parse_csv_file_dataframe_notebook_style_e2e_options(tmp_path: Path) -> None:
    csv_path = tmp_path / "sales.csv"
    csv_path.write_text(
        "order_id,region,qty,status,comment\n"
        "1,EMEA,2,paid,ok\n"
        "2,APAC,3,pending,NA\n"
        "3,EMEA,4,paid,NULL\n",
        encoding="utf-8",
    )

    df = tabx.parse_csv_file_dataframe(
        str(csv_path),
        usecols=lambda c: c in {"order_id", "region", "comment"},
        na_values={"comment": ["NA", "NULL"]},
        nrows=2,
    )

    expected = pd.DataFrame(
        {
            "order_id": [1, 2],
            "region": ["EMEA", "APAC"],
            "comment": ["ok", np.nan],
        }
    )
    pdt.assert_frame_equal(df, expected, check_dtype=False)
