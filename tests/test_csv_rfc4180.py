# pyright: reportMissingImports=false
"""RFC 4180 compliance tests for tabx CSV parsing.

Covers:
- Quoted fields containing commas (quoted_commas.csv)
- Quoted fields containing embedded newlines (embedded_newlines.csv)
- Double-quote escaping "" → " (escaped_quotes.csv)
- "Malicious" edge cases inspired by csv-wrangled (PyPI)
- Custom delimiter and quote character
- CRLF line endings
"""

from __future__ import annotations

from pathlib import Path

import pandas as pd
import pandas.testing as pdt
import pytest

import tabx

FIXTURES = Path(__file__).parent / "fixtures"


# Fixture-file based tests:


class TestQuotedCommas:
    def test_dataframe_shape(self) -> None:
        text = (FIXTURES / "quoted_commas.csv").read_text()
        df = tabx.parse_csv_dataframe(text, skip_header=True)
        assert df.shape == (3, 3)
        assert list(df.columns) == ["name", "city", "score"]

    def test_commas_inside_field_preserved(self) -> None:
        text = (FIXTURES / "quoted_commas.csv").read_text()
        df = tabx.parse_csv_dataframe(text, skip_header=True)
        assert df["name"].tolist() == ["Smith, John", "Lee, Alice", "Doe Jane"]
        assert df["city"].tolist() == ["New York, NY", "San Francisco, CA", "Austin TX"]

    def test_score_column_numeric(self) -> None:
        text = (FIXTURES / "quoted_commas.csv").read_text()
        df = tabx.parse_csv_dataframe(text, skip_header=True)
        assert df["score"].tolist() == [95, 87, 73]


class TestEmbeddedNewlines:
    def test_dataframe_shape(self) -> None:
        text = (FIXTURES / "embedded_newlines.csv").read_text()
        df = tabx.parse_csv_dataframe(text, skip_header=True)
        assert df.shape == (3, 3)

    def test_newline_preserved_in_field(self) -> None:
        text = (FIXTURES / "embedded_newlines.csv").read_text()
        df = tabx.parse_csv_dataframe(text, skip_header=True)
        assert df["notes"].iloc[0] == "First line\nsecond line"
        assert df["notes"].iloc[1] == "Only one line"
        assert df["notes"].iloc[2] == "Three lines\nmiddle\nlast"

    def test_id_and_value_columns_numeric(self) -> None:
        text = (FIXTURES / "embedded_newlines.csv").read_text()
        df = tabx.parse_csv_dataframe(text, skip_header=True)
        assert df["id"].tolist() == [1, 2, 3]
        assert df["value"].tolist() == [100, 200, 300]


class TestEscapedQuotes:
    def test_dataframe_shape(self) -> None:
        text = (FIXTURES / "escaped_quotes.csv").read_text()
        df = tabx.parse_csv_dataframe(text, skip_header=True)
        assert df.shape == (3, 2)

    def test_doubled_quotes_unescaped(self) -> None:
        text = (FIXTURES / "escaped_quotes.csv").read_text()
        df = tabx.parse_csv_dataframe(text, skip_header=True)
        assert df["phrase"].tolist() == [
            'He said "Hello, World!"',
            'The answer is "42"',
            "No quotes here",
        ]


# "Malicious" edge cases (csv-wrangled style):


class TestMaliciousEdgeCases:
    def test_empty_quoted_field_becomes_empty_string(self) -> None:
        # "" in a quoted field is an empty string, NOT a null/None.
        # Unquoted empty (trailing comma) stays null — they are distinct by RFC 4180.
        text = 'a,b\n"",hello\n"world",""\n'
        df = tabx.parse_csv_dataframe(text, skip_header=True)
        assert df["a"].tolist() == ["", "world"]
        assert df["b"].tolist() == ["hello", ""]

    def test_whitespace_only_quoted_field_preserved_verbatim(self) -> None:
        # Quoted field content must never be trimmed even with trim_whitespace=True.
        text = 'id,label\n1,"   "\n'
        df = tabx.parse_csv_dataframe(text, skip_header=True)
        assert df["label"].iloc[0] == "   "

    def test_trailing_comma_produces_empty_last_field(self) -> None:
        text = "x,y,z\n1,2,\n3,4,5\n"
        df = tabx.parse_csv_dataframe(text, skip_header=True)
        assert df.shape == (2, 3)
        assert pd.isna(df["z"].iloc[0])
        assert df["z"].iloc[1] == 5

    def test_crlf_line_endings(self) -> None:
        text = "name,val\r\nalpha,1\r\nbeta,2\r\n"
        df = tabx.parse_csv_dataframe(text, skip_header=True)
        assert df["name"].tolist() == ["alpha", "beta"]
        assert df["val"].tolist() == [1, 2]

    def test_mixed_lf_and_crlf_endings(self) -> None:
        text = "a,b\r\n1,2\n3,4\r\n"
        df = tabx.parse_csv_dataframe(text, skip_header=True)
        assert df["a"].tolist() == [1, 3]
        assert df["b"].tolist() == [2, 4]

    def test_single_column_quoted(self) -> None:
        text = 'phrase\n"hello, world"\n"foo""bar"\n'
        df = tabx.parse_csv_dataframe(text, skip_header=True)
        assert df["phrase"].tolist() == ["hello, world", 'foo"bar']

    def test_header_only_no_data_rows(self) -> None:
        text = "a,b,c\n"
        df = tabx.parse_csv_dataframe(text, skip_header=True)
        assert df.shape == (0, 3)
        assert list(df.columns) == ["a", "b", "c"]

    def test_leading_whitespace_before_opening_quote_trimmed(self) -> None:
        # With trim_whitespace=True (default): spaces before " are stripped,
        # field is treated as quoted.
        text = 'a,b\n  "hello, world"  ,2\n'
        df = tabx.parse_csv_dataframe(text, skip_header=True, trim_whitespace=True)
        assert df["a"].iloc[0] == "hello, world"

    def test_no_trim_preserves_unquoted_spaces(self) -> None:
        text = "a,b\n  hello  ,2\n"
        df = tabx.parse_csv_dataframe(text, skip_header=True, trim_whitespace=False)
        assert df["a"].iloc[0] == "  hello  "

    def test_multiline_field_inside_quoted_commas_row(self) -> None:
        # Combined: quoted field with both comma and newline.
        text = 'id,desc\n1,"Line one, still one\nLine two, still one"\n2,"plain"\n'
        df = tabx.parse_csv_dataframe(text, skip_header=True)
        assert df["desc"].iloc[0] == "Line one, still one\nLine two, still one"
        assert df["desc"].iloc[1] == "plain"

    def test_quote_immediately_at_eof_no_newline(self) -> None:
        # File without trailing newline after closing quote.
        text = 'a,b\n1,"end"'
        df = tabx.parse_csv_dataframe(text, skip_header=True)
        assert df["b"].iloc[0] == "end"


# Custom separator and quote character:


class TestCustomOptions:
    def test_semicolon_delimiter(self) -> None:
        text = "name;score\nAlice;10\nBob;20\n"
        df = tabx.parse_csv_dataframe(text, skip_header=True, delimiter=";")
        assert list(df.columns) == ["name", "score"]
        assert df["score"].tolist() == [10, 20]

    def test_tab_delimiter(self) -> None:
        text = "a\tb\n1\t2\n3\t4\n"
        df = tabx.parse_csv_dataframe(text, skip_header=True, delimiter="\t")
        assert df["a"].tolist() == [1, 3]

    def test_custom_quote_char(self) -> None:
        text = "name,city\n'Smith, John','New York'\n"
        df = tabx.parse_csv_dataframe(text, skip_header=True, quote="'")
        assert df["name"].iloc[0] == "Smith, John"
        assert df["city"].iloc[0] == "New York"

    def test_custom_delimiter_with_quoted_field_containing_delimiter(self) -> None:
        # Pipe-delimited; quoted field contains a pipe.
        text = 'a|b\n"x|y"|2\n'
        df = tabx.parse_csv_dataframe(text, skip_header=True, delimiter="|")
        assert df["a"].iloc[0] == "x|y"
        assert df["b"].iloc[0] == 2

    def test_invalid_delimiter_length_raises(self) -> None:
        with pytest.raises(ValueError, match="delimiter must be a single character"):
            tabx.parse_csv_dataframe("a,b\n1,2", delimiter=",,")

    def test_invalid_quote_length_raises(self) -> None:
        with pytest.raises(ValueError, match="quote must be a single character"):
            tabx.parse_csv_dataframe("a,b\n1,2", quote='""')
