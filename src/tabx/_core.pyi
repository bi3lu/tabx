from __future__ import annotations

from typing import Literal, overload

import numpy as np
import numpy.typing as npt

def parse_csv_numbers(input: str) -> list[int]:
    """Parse a single-row CSV string into a list of integers.

    Args:
        input: Comma-separated integer values, e.g. ``"1, 2, 3"``.

    Returns:
        Parsed integer values in input order.

    Raises:
        ValueError: If a field is empty or cannot be parsed as an integer.
    """
    ...

def sum_csv_numbers(input: str) -> int:
    """Return the sum of all integers in a single-row CSV string.

    Args:
        input: Comma-separated integer values, e.g. ``"1, 2, 3"``.

    Returns:
        Sum of all parsed integers.

    Raises:
        ValueError: If a field is empty or cannot be parsed as an integer.
    """
    ...

def parse_csv_flat(csv_text: str, skip_header: bool = ...) -> list[int]:
    """Parse every integer cell in a multi-row CSV string into a flat list.

    Blank lines are silently skipped.

    Args:
        csv_text: Full CSV text with rows separated by ``'\\n'``.
        skip_header: When ``True``, the first row is treated as a header and
            excluded from the output.

    Returns:
        All integer values in document order (row-major).

    Raises:
        ValueError: If any field is empty or cannot be parsed as an integer.
    """
    ...

def sum_csv_all(csv_text: str, skip_header: bool = ...) -> int:
    """Sum every integer in a multi-row CSV string.

    Args:
        csv_text: Full CSV text with rows separated by ``'\\n'``.
        skip_header: When ``True``, skip the first (header) row.

    Returns:
        Sum of all parsed integers as a 64-bit integer.

    Raises:
        ValueError: If any field is empty or cannot be parsed as an integer.
    """
    ...

@overload
def parse_csv_numpy(
    csv_text: str,
    skip_header: Literal[True],
) -> tuple[npt.NDArray[np.int32], list[str]]:
    """Parse a multi-row integer CSV into a zero-copy 2-D NumPy ``int32`` array.

    Performs a fast newline-count scan to pre-allocate the buffer, then a
    single parsing pass that writes directly into the NumPy buffer.  The
    returned array shares memory with no intermediate copy (``py::capsule``
    ownership transfer).

    Args:
        csv_text: Full CSV text with rows separated by ``'\\n'``.
        skip_header: When ``True``, the first row is parsed as column names
            and returned as ``list[str]``; otherwise column indices
            (``list[int]``) are returned.

    Returns:
        A tuple ``(array, columns)`` where *array* has shape
        ``(rows, cols)`` and dtype ``int32``, and *columns* contains
        either column-name strings or zero-based integer indices.

    Raises:
        ValueError: If any field is empty or cannot be parsed as an integer.

    Example::

        arr, cols = tabx.parse_csv_numpy(csv_text, skip_header=True)
        df = pd.DataFrame(arr, columns=cols)
    """
    ...

@overload
def parse_csv_numpy(
    csv_text: str,
    skip_header: Literal[False] = ...,
) -> tuple[npt.NDArray[np.int32], list[int]]: ...
@overload
def parse_csv_numpy(
    csv_text: str,
    skip_header: bool,
) -> tuple[npt.NDArray[np.int32], list[str] | list[int]]: ...
def list_xlsx_sheets(file_path: str) -> list[dict[str, int | str]]:
    """List worksheets in an XLSX workbook.

    Returns:
        A list like ``[{"name": "Sheet1", "index": 0}, ...]``.
    """
    ...

@overload
def parse_xlsx_numpy(
    file_path: str,
    sheet_name: str = ...,
    skip_header: Literal[True] = ...,
) -> tuple[npt.NDArray[np.int32], list[str]]: ...
@overload
def parse_xlsx_numpy(
    file_path: str,
    sheet_name: str = ...,
    skip_header: Literal[False] = ...,
) -> tuple[npt.NDArray[np.int32], list[int]]: ...
@overload
def parse_xlsx_numpy(
    file_path: str,
    sheet_name: str = ...,
    skip_header: bool = ...,
) -> tuple[npt.NDArray[np.int32], list[str] | list[int]]: ...
