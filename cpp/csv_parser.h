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
    std::vector<int> parse_csv_numbers(const std::string &input);

    /**
     * @brief Sum all integers in a single-row CSV string.
     *
     * @param input  Single-row CSV string, e.g. "1, 2, 3".
     * @return       Sum of all parsed integers.
     */
    int sum_csv_numbers(const std::string &input);

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
    std::vector<int> parse_csv_flat(const std::string &csv_text, bool skip_header = false);

    /**
     * @brief Sum every integer in a multi-row CSV text.
     *
     * @param csv_text    Full CSV text, possibly spanning multiple lines.
     * @param skip_header When true, skip the first line (default: false).
     * @return            Sum of all parsed integers as a 64-bit integer.
     */
    long long sum_csv_all(const std::string &csv_text, bool skip_header = false);

    /**
     * @brief Shape returned by parse_csv_into_buffer.
     */
    struct CsvShape
    {
        std::size_t rows;
        std::size_t cols;
    };

    /**
     * @brief How ragged CSV rows should be handled.
     */
    enum class CsvShapeMode : uint8_t
    {
        Strict = 0,
        Permissive = 1,
    };

    /**
     * @brief Summary of observed CSV row-width inconsistencies.
     */
    struct CsvShapeInfo
    {
        std::size_t expected_cols = 0;
        std::size_t min_observed_cols = 0;
        std::size_t max_observed_cols = 0;
        std::size_t ragged_rows = 0;
    };

    /**
     * @brief Classification of one CSV cell.
     */
    enum class CsvCellKind : uint8_t
    {
        Empty = 0,
        Integer = 1,
        Float = 2,
        Boolean = 3,
        String = 4,
    };

    /**
     * @brief Expected type for one column in schema-aware CSV parsing.
     */
    enum class CsvSchemaType : uint8_t
    {
        Integer = 1,
        Float = 2,
        Boolean = 3,
        String = 4,
    };

    /**
     * @brief One typed CSV cell value.
     */
    struct CsvRawCell
    {
        CsvCellKind kind = CsvCellKind::Empty;
        double dval = 0.0;
        std::string sval;
        bool bval = false;
    };

    /**
     * @brief One mixed CSV column in compact typed form.
     *
     * The parser starts in a typed fast-path and only materializes
     * @ref object_cells when a column truly requires object/mixed semantics.
     */
    struct CsvMixedColumn
    {
        bool has_empty = false;
        bool has_int = false;
        bool has_float = false;
        bool has_bool = false;
        bool has_string = false;

        bool object_mode = false;
        CsvCellKind stable_kind = CsvCellKind::Empty; ///< Typed fast-path kind.

        // Row-aligned buffers used by typed fast paths.
        std::vector<std::int64_t> int_values;
        std::vector<double> float_values;
        std::vector<std::uint8_t> bool_values;
        std::vector<std::string> string_values;
        std::vector<std::uint8_t> null_mask; ///< 1 => null at row, 0 => value.

        // Populated only if object_mode becomes true.
        std::vector<CsvRawCell> object_cells;
    };

    /**
     * @brief Options controlling RFC 4180 CSV parsing behaviour.
     *
     * Pass an instance to parse_csv_mixed to override defaults.
     */
    struct CsvParseOptions
    {
        char delimiter = ',';        ///< Field separator (default: comma).
        char quote = '"';            ///< Quoting character (default: double-quote).
        bool trim_whitespace = true; ///< Strip leading/trailing ASCII whitespace from
                                     ///  unquoted fields.  Quoted field content is
                                     ///  always preserved verbatim.
    };

    /**
     * @brief Full mixed-type result for CSV parsing.
     */
    struct CsvMixedResult
    {
        std::vector<std::string> headers;
        std::size_t rows = 0;
        std::size_t cols = 0;
        std::vector<CsvMixedColumn> columns;
        CsvShapeInfo shape_info;
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
        const std::string &csv_text,
        bool skip_header,
        std::vector<int32_t> &out,
        std::vector<std::string> *header_out = nullptr);

    /**
     * @brief Parse a CSV text into per-cell typed values (RFC 4180 compliant).
     *
     * Handles quoted fields (including embedded commas and newlines) and
     * escaped double-quotes ("") via a single-pass state machine.
     *
     * Type inference per token:
     * - empty token           -> Empty
     * - true / false          -> Boolean
     * - integer literal       -> Integer
     * - floating literal      -> Float
     * - otherwise             -> String
     *
     * @param csv_text    Full CSV text.  Line endings may be LF or CRLF.
     * @param skip_header When true, the first logical row is used as column names.
     * @param shape_mode  Strict: reject ragged rows.  Permissive: pad/extend.
     * @param opts        Delimiter, quote character and whitespace trimming.
     */
    CsvMixedResult parse_csv_mixed(
        const std::string &csv_text,
        bool skip_header = true,
        CsvShapeMode shape_mode = CsvShapeMode::Permissive,
        CsvParseOptions opts = {});

    /**
     * @brief Parse CSV using a fixed per-column schema (ultra-fast strict path).
     *
     * Type inference is skipped; each column is parsed according to @p schema.
     * Empty unquoted fields are treated as nulls. Quoted empty fields in string
     * columns are treated as empty strings.
     */
    CsvMixedResult parse_csv_mixed_schema(
        const std::string &csv_text,
        const std::vector<CsvSchemaType> &schema,
        bool skip_header = true,
        CsvShapeMode shape_mode = CsvShapeMode::Strict,
        CsvParseOptions opts = {});

} // namespace tabx
