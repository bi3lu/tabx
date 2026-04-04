#include "csv_parser.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace tabx
{

    namespace
    {

        // Trim leading/trailing ASCII whitespace without any heap allocation.
        inline std::string_view trim_view(std::string_view sv) noexcept
        {
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
                sv.remove_prefix(1);

            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
                sv.remove_suffix(1);

            return sv;
        }

        // Parse a decimal integer from a string_view without constructing std::string.
        // Sets ok=false and returns 0 on any parse error.
        inline int parse_int(std::string_view sv, bool &ok) noexcept
        {
            const char *p = sv.data();
            const char *end = p + sv.size();

            bool negative = false;

            if (p < end && *p == '-')
            {
                negative = true;
                ++p;
            }
            else if (p < end && *p == '+')
            {
                ++p;
            }

            if (p == end)
            {
                ok = false;
                return 0;
            }

            int value = 0;
            while (p < end)
            {
                const auto c = static_cast<unsigned char>(*p);

                if (c < '0' || c > '9')
                {
                    ok = false;
                    return 0;
                }

                value = value * 10 + static_cast<int>(c - '0');
                ++p;
            }

            ok = true;
            return negative ? -value : value;
        }

        // Parse all comma-separated tokens in `line` and append integers to `out`.
        // No intermediate vector is allocated.
        inline void parse_row_into(std::string_view line, std::vector<int> &out)
        {
            std::size_t start = 0;

            while (start <= line.size())
            {
                const std::size_t comma = line.find(',', start);
                const std::size_t len = (comma == std::string_view::npos)
                                            ? line.size() - start
                                            : comma - start;
                const std::string_view token = trim_view(line.substr(start, len));

                if (token.empty())
                    throw std::invalid_argument("Empty CSV field is not allowed");

                bool ok = true;
                const int value = parse_int(token, ok);

                if (!ok)
                    throw std::invalid_argument("Invalid integer token: " + std::string(token));

                out.push_back(value);

                if (comma == std::string_view::npos)
                    break;

                start = comma + 1;
            }
        }

        // Sum all comma-separated integers in `line` without building a vector.
        inline long long sum_row(std::string_view line)
        {
            long long total = 0;
            std::size_t start = 0;

            while (start <= line.size())
            {
                const std::size_t comma = line.find(',', start);
                const std::size_t len = (comma == std::string_view::npos)
                                            ? line.size() - start
                                            : comma - start;
                const std::string_view token = trim_view(line.substr(start, len));

                if (token.empty())
                    throw std::invalid_argument("Empty CSV field is not allowed");

                bool ok = true;
                const int value = parse_int(token, ok);

                if (!ok)
                    throw std::invalid_argument("Invalid integer token: " + std::string(token));

                total += value;

                if (comma == std::string_view::npos)
                    break;

                start = comma + 1;
            }

            return total;
        }

        // RFC 4180 state-machine row tokenizer.
        //
        // Parses exactly one logical CSV row starting at `pos` inside `text` and
        // advances `pos` to the first character of the NEXT logical row (past the
        // \n or \r\n that terminates this row), or to text.size() at EOF.
        //
        // Returned strings are owned (not views) so that "" → " unescaping can be
        // performed without any extra copies for the common (no-escape) case the
        // compiler will optimise the move away.
        //
        // Blank-line detection is handled by the caller (see parse_csv_mixed).
        // The parser streams every parsed field to `consume_field(value, was_quoted, index)`
        // and returns the number of fields in the logical row.
        template <typename FieldConsumer>
        inline std::size_t parse_row_at_consume(
            std::string_view text,
            std::size_t &pos,
            const tabx::CsvParseOptions &opts,
            FieldConsumer &&consume_field)
        {
            enum class State : uint8_t
            {
                UNQUOTED,
                QUOTED,
                QUOTE_ESCAPE
            };

            std::size_t field_index = 0;
            std::string field;
            field.reserve(32);
            State state = State::UNQUOTED;
            bool field_quoted = false;

            // Trim in-place: removes leading/trailing ASCII spaces from `field`.
            auto trim_inplace = [](std::string &s) noexcept
            {
                std::size_t l = 0;
                while (l < s.size() && std::isspace(static_cast<unsigned char>(s[l])))
                    ++l;
                std::size_t r = s.size();
                while (r > l && std::isspace(static_cast<unsigned char>(s[r - 1])))
                    --r;
                if (l > 0 || r < s.size())
                    s = s.substr(l, r - l);
            };

            auto push_field = [&]()
            {
                if (!field_quoted && opts.trim_whitespace && !field.empty())
                {
                    const unsigned char first = static_cast<unsigned char>(field.front());
                    const unsigned char last = static_cast<unsigned char>(field.back());
                    if (std::isspace(first) || std::isspace(last))
                        trim_inplace(field);
                }
                consume_field(std::move(field), field_quoted, field_index);
                ++field_index;
                field.clear();
                field_quoted = false;
            };

            while (pos < text.size())
            {
                const char c = text[pos];

                switch (state)
                {
                case State::UNQUOTED:
                    if (c == opts.delimiter)
                    {
                        push_field();
                        ++pos;
                    }
                    else if (c == '\n')
                    {
                        push_field();
                        ++pos;
                        return field_index;
                    }
                    else if (c == '\r' && pos + 1 < text.size() && text[pos + 1] == '\n')
                    {
                        push_field();
                        pos += 2;
                        return field_index;
                    }
                    else if (c == opts.quote &&
                             (field.empty() ||
                              (opts.trim_whitespace &&
                               std::all_of(field.begin(), field.end(),
                                           [](unsigned char ch)
                                           { return std::isspace(ch); }))))
                    {
                        // Begin a quoted field; discard any leading whitespace already
                        // accumulated (only possible when trim_whitespace is true).
                        field.clear();
                        field_quoted = true;
                        state = State::QUOTED;
                        ++pos;
                    }
                    else
                    {
                        field += c;
                        ++pos;
                    }
                    break;

                case State::QUOTED:
                    if (c == opts.quote)
                    {
                        state = State::QUOTE_ESCAPE;
                        ++pos;
                    }
                    else
                    {
                        // '\n' and '\r' are valid here — this is a multi-line field.
                        field += c;
                        ++pos;
                    }
                    break;

                case State::QUOTE_ESCAPE:
                    if (c == opts.quote)
                    {
                        // Escaped double-quote: "" → "
                        field += opts.quote;
                        state = State::QUOTED;
                        ++pos;
                    }
                    else if (c == opts.delimiter)
                    {
                        push_field();
                        state = State::UNQUOTED;
                        ++pos;
                    }
                    else if (c == '\n')
                    {
                        push_field();
                        ++pos;
                        return field_index;
                    }
                    else if (c == '\r' && pos + 1 < text.size() && text[pos + 1] == '\n')
                    {
                        push_field();
                        pos += 2;
                        return field_index;
                    }
                    else if (opts.trim_whitespace &&
                             std::isspace(static_cast<unsigned char>(c)))
                    {
                        // Trailing whitespace between closing quote and delimiter/newline
                        // is silently ignored (user-friendly mode).
                        ++pos;
                    }
                    else
                    {
                        // Malformed: treat as unquoted continuation.
                        field += c;
                        state = State::UNQUOTED;
                        ++pos;
                    }
                    break;
                }
            }

            // EOF: push the final field (even if it is empty, to cover a trailing
            // delimiter like "a,b," which should produce three fields).
            push_field();
            return field_index;
        }

        inline std::size_t count_csv_fields(std::string_view line) noexcept
        {
            if (line.empty())
                return 0;

            return static_cast<std::size_t>(std::count(line.begin(), line.end(), ',')) + 1;
        }

        [[noreturn]] inline void throw_row_width_mismatch(
            std::size_t row_index,
            std::size_t expected_cols,
            std::size_t actual_cols)
        {
            throw std::invalid_argument(
                "CSV row width mismatch at row " + std::to_string(row_index) +
                ": expected " + std::to_string(expected_cols) +
                " columns, got " + std::to_string(actual_cols));
        }

        inline unsigned char ascii_lower(unsigned char c) noexcept
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
        }

        inline bool is_true_token(std::string_view sv) noexcept
        {
            return sv.size() == 4 &&
                   ascii_lower(static_cast<unsigned char>(sv[0])) == 't' &&
                   ascii_lower(static_cast<unsigned char>(sv[1])) == 'r' &&
                   ascii_lower(static_cast<unsigned char>(sv[2])) == 'u' &&
                   ascii_lower(static_cast<unsigned char>(sv[3])) == 'e';
        }

        inline bool is_false_token(std::string_view sv) noexcept
        {
            return sv.size() == 5 &&
                   ascii_lower(static_cast<unsigned char>(sv[0])) == 'f' &&
                   ascii_lower(static_cast<unsigned char>(sv[1])) == 'a' &&
                   ascii_lower(static_cast<unsigned char>(sv[2])) == 'l' &&
                   ascii_lower(static_cast<unsigned char>(sv[3])) == 's' &&
                   ascii_lower(static_cast<unsigned char>(sv[4])) == 'e';
        }

        inline bool is_digit_ascii(unsigned char c) noexcept
        {
            return c >= '0' && c <= '9';
        }

        inline bool starts_like_number(std::string_view sv) noexcept
        {
            const unsigned char c0 = static_cast<unsigned char>(sv.front());

            if (is_digit_ascii(c0))
                return true;

            if (c0 == '+' || c0 == '-')
            {
                if (sv.size() == 1)
                    return false;

                const unsigned char c1 = static_cast<unsigned char>(sv[1]);
                return is_digit_ascii(c1) || c1 == '.';
            }

            return c0 == '.';
        }

        inline bool contains_float_marker(std::string_view sv) noexcept
        {
            for (const unsigned char c : sv)
            {
                if (c == '.' || c == 'e' || c == 'E')
                    return true;
            }
            return false;
        }

        inline bool try_parse_bool_token(std::string_view sv, bool &out) noexcept
        {
            if (is_true_token(sv))
            {
                out = true;
                return true;
            }

            if (is_false_token(sv))
            {
                out = false;
                return true;
            }

            return false;
        }

        inline bool try_parse_int64(std::string_view sv, std::int64_t &out)
        {
            if (sv.empty())
                return false;

            const char *begin = sv.data();
            const char *end = begin + sv.size();
            const auto result = std::from_chars(begin, end, out, 10);
            return result.ec == std::errc() && result.ptr == end;
        }

        inline bool try_parse_double(std::string_view sv, double &out)
        {
            if (sv.empty())
                return false;

            const char *begin = sv.data();
            const char *end = begin + sv.size();
            const auto result = std::from_chars(begin, end, out, std::chars_format::general);
            return result.ec == std::errc() && result.ptr == end && std::isfinite(out);
        }

        inline CsvRawCell classify_csv_token(std::string token, bool was_quoted = false)
        {
            const std::string_view token_view(token);

            if (token_view.empty())
                // Quoted empty "" → empty string; unquoted empty → missing value.
                return was_quoted
                           ? CsvRawCell{CsvCellKind::String, 0.0, "", false}
                           : CsvRawCell{};

            if (is_true_token(token_view))
                return CsvRawCell{CsvCellKind::Boolean, 0.0, "", true};

            if (is_false_token(token_view))
                return CsvRawCell{CsvCellKind::Boolean, 0.0, "", false};

            // Most realistic mixed/quoted datasets have many obvious strings
            // (e.g. category / city / names). Avoid expensive numeric attempts.
            if (!starts_like_number(token_view))
                return CsvRawCell{CsvCellKind::String, 0.0, std::move(token), false};

            std::int64_t ival = 0;
            if (try_parse_int64(token_view, ival))
                return CsvRawCell{CsvCellKind::Integer, static_cast<double>(ival), "", false};

            if (contains_float_marker(token_view))
            {
                double dval = 0.0;
                if (try_parse_double(token_view, dval))
                    return CsvRawCell{CsvCellKind::Float, dval, "", false};
            }

            return CsvRawCell{CsvCellKind::String, 0.0, std::move(token), false};
        }

        inline void append_cell_schema_typed(
            tabx::CsvMixedColumn &col,
            std::string token,
            bool was_quoted,
            CsvSchemaType expected)
        {
            const std::string_view token_view(token);

            if (token_view.empty())
            {
                // Quoted empty string in a schema string column is a real value,
                // while all other empty tokens represent missing cells.
                if (expected == CsvSchemaType::String && was_quoted)
                {
                    col.null_mask.push_back(0u);
                    col.string_values.emplace_back();
                }
                else
                {
                    col.has_empty = true;
                    col.null_mask.push_back(1u);

                    switch (expected)
                    {
                    case CsvSchemaType::Integer:
                        col.int_values.push_back(0);
                        break;
                    case CsvSchemaType::Float:
                        col.float_values.push_back(std::numeric_limits<double>::quiet_NaN());
                        break;
                    case CsvSchemaType::Boolean:
                        col.bool_values.push_back(0u);
                        break;
                    case CsvSchemaType::String:
                        col.string_values.emplace_back();
                        break;
                    }
                }

                return;
            }

            switch (expected)
            {
            case CsvSchemaType::Integer:
            {
                std::int64_t ival = 0;
                if (!try_parse_int64(token_view, ival))
                    throw std::invalid_argument("Invalid integer token: " + std::string(token_view));

                col.null_mask.push_back(0u);
                col.int_values.push_back(ival);
                break;
            }

            case CsvSchemaType::Float:
            {
                double dval = 0.0;
                if (!try_parse_double(token_view, dval))
                    throw std::invalid_argument("Invalid float token: " + std::string(token_view));

                col.null_mask.push_back(0u);
                col.float_values.push_back(dval);
                break;
            }

            case CsvSchemaType::Boolean:
            {
                bool bval = false;
                if (!try_parse_bool_token(token_view, bval))
                    throw std::invalid_argument("Invalid boolean token: " + std::string(token_view));

                col.null_mask.push_back(0u);
                col.bool_values.push_back(bval ? 1u : 0u);
                break;
            }

            case CsvSchemaType::String:
                col.null_mask.push_back(0u);
                col.string_values.push_back(std::move(token));
                break;
            }
        }

        inline CsvRawCell make_cell_from_typed(
            CsvCellKind kind,
            double numeric_value,
            std::uint8_t bool_value)
        {
            switch (kind)
            {
            case CsvCellKind::Empty:
                return CsvRawCell{};
            case CsvCellKind::Integer:
                return CsvRawCell{CsvCellKind::Integer, numeric_value, "", false};
            case CsvCellKind::Float:
                return CsvRawCell{CsvCellKind::Float, numeric_value, "", false};
            case CsvCellKind::Boolean:
                return CsvRawCell{CsvCellKind::Boolean, 0.0, "", bool_value != 0};
            case CsvCellKind::String:
                return CsvRawCell{CsvCellKind::String, 0.0, "", false};
            }

            return CsvRawCell{};
        }

        inline void materialize_object_cells(tabx::CsvMixedColumn &col)
        {
            if (col.object_mode)
                return;

            const std::size_t n_rows = col.null_mask.size();
            col.object_cells.reserve(n_rows);

            for (std::size_t i = 0; i < n_rows; ++i)
            {
                if (col.null_mask[i] != 0u)
                {
                    col.object_cells.push_back(CsvRawCell{});
                    continue;
                }

                switch (col.stable_kind)
                {
                case CsvCellKind::Integer:
                    col.object_cells.push_back(CsvRawCell{
                        CsvCellKind::Integer,
                        static_cast<double>(col.int_values[i]),
                        "",
                        false});
                    break;
                case CsvCellKind::Float:
                    col.object_cells.push_back(make_cell_from_typed(
                        CsvCellKind::Float,
                        col.float_values[i],
                        0u));
                    break;
                case CsvCellKind::Boolean:
                    col.object_cells.push_back(make_cell_from_typed(
                        CsvCellKind::Boolean,
                        0.0,
                        col.bool_values[i]));
                    break;
                case CsvCellKind::String:
                    col.object_cells.push_back(CsvRawCell{
                        CsvCellKind::String,
                        0.0,
                        std::move(col.string_values[i]),
                        false});
                    break;
                case CsvCellKind::Empty:
                    col.object_cells.push_back(CsvRawCell{});
                    break;
                }
            }

            col.int_values.clear();
            col.float_values.clear();
            col.bool_values.clear();
            col.string_values.clear();
            col.null_mask.clear();

            col.object_mode = true;
        }

        inline void init_stable_storage(tabx::CsvMixedColumn &col, CsvCellKind kind)
        {
            col.stable_kind = kind;
            const std::size_t n_rows = col.null_mask.size();

            switch (kind)
            {
            case CsvCellKind::Integer:
                col.int_values.resize(n_rows, 0);
                break;
            case CsvCellKind::Float:
                col.float_values.resize(n_rows, std::numeric_limits<double>::quiet_NaN());
                break;
            case CsvCellKind::Boolean:
                col.bool_values.resize(n_rows, 0u);
                break;
            case CsvCellKind::String:
                col.string_values.resize(n_rows);
                break;
            case CsvCellKind::Empty:
                break;
            }
        }

        inline void promote_int_to_float(tabx::CsvMixedColumn &col)
        {
            col.float_values.reserve(col.int_values.size());
            for (std::size_t i = 0; i < col.int_values.size(); ++i)
            {
                col.float_values.push_back(
                    col.null_mask[i] != 0u
                        ? std::numeric_limits<double>::quiet_NaN()
                        : static_cast<double>(col.int_values[i]));
            }
            col.int_values.clear();
            col.stable_kind = CsvCellKind::Float;
        }

        inline void append_empty_cell(tabx::CsvMixedColumn &col)
        {
            col.has_empty = true;

            if (col.object_mode)
            {
                col.object_cells.push_back(CsvRawCell{});
                return;
            }

            col.null_mask.push_back(1u);
            switch (col.stable_kind)
            {
            case CsvCellKind::Integer:
                col.int_values.push_back(0);
                break;
            case CsvCellKind::Float:
                col.float_values.push_back(std::numeric_limits<double>::quiet_NaN());
                break;
            case CsvCellKind::Boolean:
                col.bool_values.push_back(0u);
                break;
            case CsvCellKind::String:
                col.string_values.emplace_back();
                break;
            case CsvCellKind::Empty:
                break;
            }
        }

        inline void append_cell(tabx::CsvMixedColumn &col, CsvRawCell cell)
        {
            if (cell.kind == CsvCellKind::Empty)
            {
                append_empty_cell(col);
                return;
            }

            switch (cell.kind)
            {
            case CsvCellKind::Integer:
                col.has_int = true;
                break;
            case CsvCellKind::Float:
                col.has_float = true;
                break;
            case CsvCellKind::Boolean:
                col.has_bool = true;
                break;
            case CsvCellKind::String:
                col.has_string = true;
                break;
            case CsvCellKind::Empty:
                break;
            }

            if (col.object_mode)
            {
                col.object_cells.push_back(std::move(cell));
                return;
            }

            if (col.stable_kind == CsvCellKind::Empty)
                init_stable_storage(col, cell.kind);

            if (col.stable_kind == CsvCellKind::Integer && cell.kind == CsvCellKind::Float)
                promote_int_to_float(col);

            const bool conflict_to_object =
                (col.stable_kind == CsvCellKind::Integer &&
                 (cell.kind == CsvCellKind::Boolean || cell.kind == CsvCellKind::String)) ||
                (col.stable_kind == CsvCellKind::Float &&
                 (cell.kind == CsvCellKind::Boolean || cell.kind == CsvCellKind::String)) ||
                (col.stable_kind == CsvCellKind::Boolean &&
                 (cell.kind == CsvCellKind::Integer || cell.kind == CsvCellKind::Float ||
                  cell.kind == CsvCellKind::String)) ||
                (col.stable_kind == CsvCellKind::String && cell.kind != CsvCellKind::String);

            if (conflict_to_object)
            {
                materialize_object_cells(col);
                col.object_cells.push_back(std::move(cell));
                return;
            }

            col.null_mask.push_back(0u);

            switch (col.stable_kind)
            {
            case CsvCellKind::Integer:
                col.int_values.push_back(static_cast<std::int64_t>(cell.dval));
                break;
            case CsvCellKind::Float:
                col.float_values.push_back(cell.dval);
                break;
            case CsvCellKind::Boolean:
                col.bool_values.push_back(cell.bval ? 1u : 0u);
                break;
            case CsvCellKind::String:
                col.string_values.push_back(std::move(cell.sval));
                break;
            case CsvCellKind::Empty:
                break;
            }
        }

    } // namespace

    std::vector<int> parse_csv_numbers(const std::string &input)
    {
        std::vector<int> result;
        parse_row_into(std::string_view(input), result);
        return result;
    }

    int sum_csv_numbers(const std::string &input)
    {
        return static_cast<int>(sum_row(std::string_view(input)));
    }

    std::vector<int> parse_csv_flat(const std::string &csv_text, bool skip_header)
    {
        std::vector<int> result;
        const std::string_view text(csv_text);
        std::size_t line_start = 0;
        bool is_first_line = true;

        while (line_start <= text.size())
        {
            const std::size_t line_end = text.find('\n', line_start);
            const std::size_t line_len = (line_end == std::string_view::npos)
                                             ? text.size() - line_start
                                             : line_end - line_start;

            const std::string_view line = trim_view(text.substr(line_start, line_len));

            if (is_first_line && skip_header)
            {
                is_first_line = false;
            }
            else if (!line.empty())
            {
                is_first_line = false;
                parse_row_into(line, result); // appends directly — no per-row vector
            }

            if (line_end == std::string_view::npos)
                break;

            line_start = line_end + 1;
        }

        return result;
    }

    long long sum_csv_all(const std::string &csv_text, bool skip_header)
    {
        long long total = 0;
        const std::string_view text(csv_text);
        std::size_t line_start = 0;
        bool is_first_line = true;

        while (line_start <= text.size())
        {
            const std::size_t line_end = text.find('\n', line_start);
            const std::size_t line_len = (line_end == std::string_view::npos)
                                             ? text.size() - line_start
                                             : line_end - line_start;

            const std::string_view line = trim_view(text.substr(line_start, line_len));

            if (is_first_line && skip_header)
            {
                is_first_line = false;
            }
            else if (!line.empty())
            {
                is_first_line = false;
                total += sum_row(line); // no flat vector built — streaming sum
            }

            if (line_end == std::string_view::npos)
                break;

            line_start = line_end + 1;
        }

        return total;
    }

    CsvShape parse_csv_into_buffer(
        const std::string &csv_text,
        bool skip_header,

        std::vector<int32_t> &out,
        std::vector<std::string> *header_out)
    {
        const std::string_view text(csv_text);

        // One fast scan to estimate data-row count for pre-allocation.
        const std::size_t n_newlines = static_cast<std::size_t>(
            std::count(text.begin(), text.end(), '\n'));

        const std::size_t approx_rows = skip_header
                                            ? (n_newlines > 0 ? n_newlines : 0)
                                            : n_newlines + 1;

        out.clear();

        std::size_t line_start = 0;
        bool is_first = true;
        std::size_t n_cols = 0;
        std::size_t n_rows = 0;

        while (line_start <= text.size())
        {
            const std::size_t line_end = text.find('\n', line_start);
            const std::size_t line_len = (line_end == std::string_view::npos)
                                             ? text.size() - line_start
                                             : line_end - line_start;

            const std::string_view line = trim_view(text.substr(line_start, line_len));

            if (is_first && skip_header)
            {
                is_first = false;
                n_cols = count_csv_fields(line);

                if (header_out)
                {
                    header_out->clear();
                    std::size_t s = 0;

                    while (s <= line.size())
                    {
                        const std::size_t c = line.find(',', s);
                        const std::size_t l = (c == std::string_view::npos)
                                                  ? line.size() - s
                                                  : c - s;

                        header_out->emplace_back(trim_view(line.substr(s, l)));

                        if (c == std::string_view::npos)
                            break;

                        s = c + 1;
                    }
                }
            }
            else if (!line.empty())
            {
                is_first = false;
                const std::size_t row_cols = count_csv_fields(line);

                if (n_cols == 0)
                {
                    n_cols = row_cols;
                    out.reserve(approx_rows * n_cols);
                }
                else if (row_cols != n_cols)
                {
                    throw_row_width_mismatch(n_rows + 1, n_cols, row_cols);
                }

                std::size_t s = 0;
                while (s <= line.size())
                {
                    const std::size_t c = line.find(',', s);
                    const std::size_t l = (c == std::string_view::npos)
                                              ? line.size() - s
                                              : c - s;
                    const std::string_view token = trim_view(line.substr(s, l));

                    if (token.empty())
                        throw std::invalid_argument("Empty CSV field is not allowed");

                    bool ok = true;
                    const int32_t value = static_cast<int32_t>(parse_int(token, ok));

                    if (!ok)
                        throw std::invalid_argument("Invalid integer token: " + std::string(token));

                    out.push_back(value);

                    if (c == std::string_view::npos)
                        break;

                    s = c + 1;
                }

                ++n_rows;
            }

            if (line_end == std::string_view::npos)
                break;

            line_start = line_end + 1;
        }

        return {n_rows, n_cols};
    }

    CsvMixedResult parse_csv_mixed(
        const std::string &csv_text,
        bool skip_header,
        CsvShapeMode shape_mode,
        CsvParseOptions opts)
    {
        const std::string_view text(csv_text);

        std::vector<std::string> headers;
        std::vector<CsvMixedColumn> columns;
        std::size_t max_cols = 0;
        std::size_t expected_cols = 0;
        std::size_t min_observed_cols = 0;
        std::size_t max_observed_cols = 0;
        std::size_t ragged_rows = 0;
        std::size_t n_rows = 0;

        bool is_first = true;
        std::size_t pos = 0;

        while (pos < text.size())
        {
            // Quick blank-line skip: if all bytes until the next physical newline
            // are ASCII whitespace, advance past it and continue.  This matches
            // the pre-RFC behaviour and avoids producing spurious empty rows.
            {
                std::size_t p = pos;
                while (p < text.size() && text[p] != '\n' && text[p] != '\r' &&
                       std::isspace(static_cast<unsigned char>(text[p])))
                    ++p;

                if (p >= text.size() || text[p] == '\n' || text[p] == '\r')
                {
                    if (p < text.size())
                        pos = (text[p] == '\r' && p + 1 < text.size() && text[p + 1] == '\n')
                                  ? p + 2
                                  : p + 1;
                    else
                        pos = p;

                    continue;
                }
            }

            if (is_first && skip_header)
            {
                const std::size_t header_cols = parse_row_at_consume(
                    text,
                    pos,
                    opts,
                    [&](std::string &&value, bool /*quoted*/, std::size_t c)
                    {
                        std::string h = std::move(value);

                        if (h.empty())
                            h = "Column_" + std::to_string(c);

                        headers.push_back(std::move(h));
                    });

                headers.reserve(header_cols);
                expected_cols = header_cols;
                max_cols = expected_cols;
                is_first = false;
                continue;
            }

            is_first = false;
            const std::size_t row_cols = parse_row_at_consume(
                text,
                pos,
                opts,
                [&](std::string &&value, bool quoted, std::size_t c)
                {
                    if (columns.size() <= c)
                    {
                        const std::size_t old_cols = columns.size();
                        columns.resize(c + 1);

                        for (std::size_t cc = old_cols; cc <= c; ++cc)
                        {
                            auto &col = columns[cc];
                            col.int_values.reserve(n_rows + 1);
                            col.float_values.reserve(n_rows + 1);
                            col.bool_values.reserve(n_rows + 1);
                            col.string_values.reserve(n_rows + 1);
                            col.null_mask.reserve(n_rows + 1);
                            for (std::size_t r = 0; r < n_rows; ++r)
                                append_empty_cell(col);
                        }
                    }

                    append_cell(columns[c], classify_csv_token(std::move(value), quoted));
                });

            if (expected_cols == 0)
                expected_cols = row_cols;

            if (n_rows == 0)
            {
                min_observed_cols = row_cols;
                max_observed_cols = row_cols;
            }
            else
            {
                min_observed_cols = std::min(min_observed_cols, row_cols);
                max_observed_cols = std::max(max_observed_cols, row_cols);
            }

            if (row_cols != expected_cols)
            {
                ++ragged_rows;

                if (shape_mode == CsvShapeMode::Strict)
                    throw_row_width_mismatch(n_rows + 1, expected_cols, row_cols);
            }

            max_cols = std::max(max_cols, row_cols);

            for (std::size_t c = row_cols; c < columns.size(); ++c)
                append_empty_cell(columns[c]);

            ++n_rows;
        }

        CsvMixedResult result;
        result.rows = n_rows;
        result.cols = max_cols;
        result.headers = std::move(headers);
        result.shape_info.expected_cols = expected_cols;
        result.shape_info.min_observed_cols = min_observed_cols;
        result.shape_info.max_observed_cols = max_observed_cols;
        result.shape_info.ragged_rows = ragged_rows;

        while (result.headers.size() < result.cols)
            result.headers.push_back("Column_" + std::to_string(result.headers.size()));

        if (columns.size() < result.cols)
            columns.resize(result.cols);

        result.columns = std::move(columns);

        return result;
    }

    CsvMixedResult parse_csv_mixed_schema(
        const std::string &csv_text,
        const std::vector<CsvSchemaType> &schema,
        bool skip_header,
        CsvShapeMode shape_mode,
        CsvParseOptions opts)
    {
        if (schema.empty())
            throw std::invalid_argument("schema must contain at least one column type");

        const std::string_view text(csv_text);
        const std::size_t expected_cols = schema.size();

        std::vector<std::string> headers;
        std::vector<CsvMixedColumn> columns(expected_cols);

        for (std::size_t c = 0; c < expected_cols; ++c)
        {
            auto &col = columns[c];

            switch (schema[c])
            {
            case CsvSchemaType::Integer:
                col.has_int = true;
                init_stable_storage(col, CsvCellKind::Integer);
                break;
            case CsvSchemaType::Float:
                col.has_float = true;
                init_stable_storage(col, CsvCellKind::Float);
                break;
            case CsvSchemaType::Boolean:
                col.has_bool = true;
                init_stable_storage(col, CsvCellKind::Boolean);
                break;
            case CsvSchemaType::String:
                col.has_string = true;
                init_stable_storage(col, CsvCellKind::String);
                break;
            }
        }

        std::size_t min_observed_cols = 0;
        std::size_t max_observed_cols = 0;
        std::size_t ragged_rows = 0;
        std::size_t n_rows = 0;

        bool is_first = true;
        std::size_t pos = 0;

        while (pos < text.size())
        {
            {
                std::size_t p = pos;
                while (p < text.size() && text[p] != '\n' && text[p] != '\r' &&
                       std::isspace(static_cast<unsigned char>(text[p])))
                    ++p;

                if (p >= text.size() || text[p] == '\n' || text[p] == '\r')
                {
                    if (p < text.size())
                        pos = (text[p] == '\r' && p + 1 < text.size() && text[p + 1] == '\n')
                                  ? p + 2
                                  : p + 1;
                    else
                        pos = p;

                    continue;
                }
            }

            if (is_first && skip_header)
            {
                const std::size_t header_cols = parse_row_at_consume(
                    text,
                    pos,
                    opts,
                    [&](std::string &&value, bool /*quoted*/, std::size_t c)
                    {
                        std::string h = std::move(value);
                        if (h.empty())
                            h = "Column_" + std::to_string(c);
                        headers.push_back(std::move(h));
                    });

                if (header_cols != expected_cols)
                    throw_row_width_mismatch(1, expected_cols, header_cols);

                is_first = false;
                continue;
            }

            is_first = false;

            bool row_has_extra = false;
            const std::size_t row_cols = parse_row_at_consume(
                text,
                pos,
                opts,
                [&](std::string &&value, bool quoted, std::size_t c)
                {
                    if (c >= expected_cols)
                    {
                        row_has_extra = true;
                        if (shape_mode == CsvShapeMode::Strict)
                            throw_row_width_mismatch(n_rows + 1, expected_cols, c + 1);
                        return;
                    }

                    append_cell_schema_typed(
                        columns[c],
                        std::move(value),
                        quoted,
                        schema[c]);
                });

            if (n_rows == 0)
            {
                min_observed_cols = row_cols;
                max_observed_cols = row_cols;
            }
            else
            {
                min_observed_cols = std::min(min_observed_cols, row_cols);
                max_observed_cols = std::max(max_observed_cols, row_cols);
            }

            if (row_cols != expected_cols || row_has_extra)
            {
                ++ragged_rows;
                if (shape_mode == CsvShapeMode::Strict)
                    throw_row_width_mismatch(n_rows + 1, expected_cols, row_cols);
            }

            const std::size_t parsed_cols = std::min(row_cols, expected_cols);
            for (std::size_t c = parsed_cols; c < expected_cols; ++c)
                append_empty_cell(columns[c]);

            ++n_rows;
        }

        CsvMixedResult result;
        result.rows = n_rows;
        result.cols = expected_cols;
        result.headers = std::move(headers);
        result.shape_info.expected_cols = expected_cols;
        result.shape_info.min_observed_cols = min_observed_cols;
        result.shape_info.max_observed_cols = max_observed_cols;
        result.shape_info.ragged_rows = ragged_rows;

        while (result.headers.size() < result.cols)
            result.headers.push_back("Column_" + std::to_string(result.headers.size()));

        result.columns = std::move(columns);
        return result;
    }

} // namespace tabx