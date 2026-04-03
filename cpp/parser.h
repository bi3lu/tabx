#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tabx
{

/**
 * @brief Parse a comma-separated string of integers into a vector.
 *
 * Whitespace surrounding each token is ignored.  Throws
 * std::invalid_argument on an empty token or a non-integer token.
 *
 * @param input  Single-row CSV string, e.g. "1, 2, 3".
 * @return       Parsed integer values in order.
 */
std::vector<int> parse_csv_numbers(const std::string& input);

/**
 * @brief Sum all integers in a single-row CSV string.
 *
 * @param input  Single-row CSV string, e.g. "1, 2, 3".
 * @return       Sum of all parsed integers.
 */
int sum_csv_numbers(const std::string& input);

/**
 * @brief Parse every integer cell in a multi-row CSV text into a flat vector.
 *
 * Rows are separated by '\n'.  When @p skip_header is true the first row
 * is skipped so that a column-name header line is not treated as data.
 * Blank lines and lines containing only whitespace are silently ignored.
 *
 * @param csv_text    Full CSV text, possibly spanning multiple lines.
 * @param skip_header When true, skip the first line (default: false).
 * @return            Flat vector of all integer values in document order.
 */
std::vector<int> parse_csv_flat(const std::string& csv_text, bool skip_header = false);

/**
 * @brief Sum every integer in a multi-row CSV text.
 *
 * @param csv_text    Full CSV text, possibly spanning multiple lines.
 * @param skip_header When true, skip the first line (default: false).
 * @return            Sum of all parsed integers as a 64-bit integer.
 */
long long sum_csv_all(const std::string& csv_text, bool skip_header = false);

/**
 * @brief Shape returned by parse_csv_into_buffer.
 */
struct CsvShape { std::size_t rows; std::size_t cols; };

/**
 * @brief Classification of one CSV cell.
 */
enum class CsvCellKind : uint8_t
{
    Empty   = 0,
    Integer = 1,
    Float   = 2,
    Boolean = 3,
    String  = 4,
};

/**
 * @brief One typed CSV cell value.
 */
struct CsvRawCell
{
    CsvCellKind  kind = CsvCellKind::Empty;
    double       dval = 0.0;
    std::string  sval;
    bool         bval = false;
};

/**
 * @brief Full mixed-type result for CSV parsing.
 */
struct CsvMixedResult
{
    std::vector<std::string> headers;
    std::size_t              rows = 0;
    std::size_t              cols = 0;
    std::vector<CsvRawCell>  cells;
};

/**
 * @brief Parse all integer cells into a caller-supplied flat row-major buffer.
 *
 * Performs one fast newline-count scan to pre-allocate the buffer, then one
 * parsing scan.  No per-row intermediate vectors are created.  The caller
 * may transfer the buffer directly to a NumPy array (zero copy).
 *
 * @param csv_text    Full CSV text, rows separated by '\n'.
 * @param skip_header Skip the first line when true.
 * @param out         Destination buffer.  Cleared and filled in row-major order.
 * @param header_out  If non-null and skip_header is true, filled with the
 *                    header tokens so the caller can name DataFrame columns.
 * @return            Shape {rows, cols} of the filled buffer.
 */
CsvShape parse_csv_into_buffer(
    const std::string&        csv_text,
    bool                      skip_header,
    std::vector<int32_t>&     out,
    std::vector<std::string>* header_out = nullptr
);

/**
 * @brief Parse a CSV text into per-cell typed values.
 *
 * Type inference is performed per token:
 * - empty token           -> Empty
 * - true / false          -> Boolean
 * - integer literal       -> Integer
 * - floating literal      -> Float
 * - otherwise             -> String
 *
 * Rows are split by '\n' and fields by ',' (same simple CSV model as the
 * integer parser path; quoted CSV escapes are not interpreted).
 */
CsvMixedResult parse_csv_mixed(const std::string& csv_text, bool skip_header = true);

}  // namespace tabx
