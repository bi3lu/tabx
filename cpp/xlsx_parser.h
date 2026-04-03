#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "csv_parser.h"  // CsvShape

namespace tabx
{

/**
 * @brief Metadata for one worksheet inside an XLSX workbook.
 */
struct XlsxSheet
{
    std::string  name;   ///< Sheet name as it appears in the workbook tab.
    std::size_t  index;  ///< 0-based position in the workbook (sheet order).
};

/**
 * @brief Classification of an individual worksheet cell value.
 */
enum class XlsxCellKind : uint8_t
{
    Empty   = 0,  ///< Blank, missing, or self-closing cell; maps to NaN/None.
    Integer = 1,  ///< Whole number (exact for |value| ≤ 2^53, stored as double).
    Float   = 2,  ///< Floating-point number stored as double.
    Boolean = 3,  ///< Boolean cell (XLSX t="b"); bval holds the truth value.
    String  = 4,  ///< Text cell (shared string, inline string, or formula string).
    Error   = 5,  ///< Formula error (e.g. #DIV/0!); treated as empty downstream.
};

/**
 * @brief A single typed cell value from a parsed XLSX worksheet.
 */
struct XlsxRawCell
{
    XlsxCellKind kind = XlsxCellKind::Empty;
    double       dval = 0.0;    ///< Numeric value for Integer / Float cells.
    std::string  sval;           ///< Text value for String cells.
    bool         bval = false;   ///< Truth value for Boolean cells.
};

/**
 * @brief Full mixed-type result from parsing one XLSX worksheet.
 *
 * Cells are stored row-major: the cell at (row r, col c) is
 * @c cells[r * cols + c].  Missing/sparse cells are XlsxCellKind::Empty.
 */
struct XlsxMixedResult
{
    std::vector<std::string>  headers; ///< Column names; empty when skip_header=false.
    std::size_t               rows = 0;
    std::size_t               cols = 0;
    std::vector<XlsxRawCell>  cells;   ///< Row-major buffer; size == rows * cols.
};

/**
 * @brief Parse a worksheet into a per-cell typed result.
 *
 * Numeric cells become Integer or Float, boolean cells (t="b") become Boolean,
 * text cells become String, formula errors become Error, and blank/sparse cells
 * become Empty.  All cell types are handled — unlike parse_xlsx_into_buffer
 * which accepts only integers.
 *
 * @param file_path   Path to the .xlsx file.
 * @param sheet_name  Worksheet name; empty string selects the first sheet.
 * @param skip_header When true, the first non-empty row is used as column names.
 */
XlsxMixedResult parse_xlsx_mixed(
    const std::string& file_path,
    const std::string& sheet_name,
    bool               skip_header);

/// Convenience overload for a pre-loaded byte buffer.
XlsxMixedResult parse_xlsx_mixed(
    const std::vector<uint8_t>& xlsx_bytes,
    const std::string&          sheet_name,
    bool                        skip_header);

/**
 * @brief List all worksheets in the given XLSX file.
 *
 * Reads only @c xl/workbook.xml — sheet data is not loaded.
 *
 * @param file_path  Absolute or relative path to the .xlsx file.
 * @return           One entry per sheet, in workbook order.
 * @throws std::runtime_error  If the file cannot be opened or is malformed.
 */
std::vector<XlsxSheet> list_xlsx_sheets(const std::string& file_path);

/**
 * @brief Parse all integer cells of a worksheet into a caller-supplied flat
 *        row-major buffer.
 *
 * Mirrors parse_csv_into_buffer: pre-allocates with a row-count scan, then
 * performs a single streaming parse pass.  No per-row intermediate vectors
 * are created.  The caller may transfer the resulting buffer directly to a
 * NumPy array (zero copy via @c py::capsule).
 *
 * Only numeric cells (<c> with no @c t attribute or @c t="n") are read.
 * Shared-string cells (@c t="s") are allowed only in the header row when
 * @p skip_header is true; encountering them in data rows throws.
 *
 * @param file_path    Absolute or relative path to the .xlsx file.
 * @param sheet_name   Worksheet to parse.  Pass an empty string to use the
 *                     first sheet in workbook order.
 * @param skip_header  When true, the first populated row is treated as a
 *                     header and stored in @p header_out instead of @p out.
 * @param out          Destination buffer.  Cleared and filled in row-major
 *                     order (row0_col0, row0_col1, …, row1_col0, …).
 * @param header_out   If non-null and @p skip_header is true, filled with the
 *                     column names from the header row.
 * @return             Shape {rows, cols} of the data stored in @p out.
 * @throws std::runtime_error  On I/O failure, malformed ZIP/XML, a missing
 *                             sheet name, or non-integer data cells.
 */
CsvShape parse_xlsx_into_buffer(
    const std::string&        file_path,
    const std::string&        sheet_name,
    bool                      skip_header,
    std::vector<int32_t>&     out,
    std::vector<std::string>* header_out = nullptr
);

/**
 * @brief Convenience overload that parses from a pre-loaded byte buffer.
 *
 * Useful when the file has already been read into memory (e.g. downloaded
 * over a network or supplied from Python as @c bytes).
 *
 * @param xlsx_bytes   Raw XLSX (ZIP) bytes.
 * @param sheet_name   Worksheet to parse.  Empty string → first sheet.
 * @param skip_header  See the file-path overload.
 * @param out          Destination buffer.
 * @param header_out   See the file-path overload.
 * @return             Shape {rows, cols} of the data stored in @p out.
 */
CsvShape parse_xlsx_into_buffer(
    const std::vector<uint8_t>& xlsx_bytes,
    const std::string&          sheet_name,
    bool                        skip_header,
    std::vector<int32_t>&       out,
    std::vector<std::string>*   header_out = nullptr
);

}  // namespace tabx
