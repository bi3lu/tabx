#include "parser.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string_view>

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
inline int parse_int(std::string_view sv, bool& ok) noexcept
{
    const char* p   = sv.data();
    const char* end = p + sv.size();

    bool negative = false;

    if (p < end && *p == '-') { negative = true;  ++p; }
    else if (p < end && *p == '+') { ++p; }

    if (p == end) { ok = false; return 0; }

    int value = 0;
    while (p < end)
    {
        const auto c = static_cast<unsigned char>(*p);

        if (c < '0' || c > '9') { ok = false; return 0; }

        value = value * 10 + static_cast<int>(c - '0');
        ++p;
    }

    ok = true;
    return negative ? -value : value;
}

// Parse all comma-separated tokens in `line` and append integers to `out`.
// No intermediate vector is allocated.
inline void parse_row_into(std::string_view line, std::vector<int>& out)
{
    std::size_t start = 0;

    while (start <= line.size())
    {
        const std::size_t comma = line.find(',', start);
        const std::size_t len   = (comma == std::string_view::npos)
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
        const std::size_t len   = (comma == std::string_view::npos)
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

}  // namespace

std::vector<int> parse_csv_numbers(const std::string& input)
{
    std::vector<int> result;
    parse_row_into(std::string_view(input), result);
    return result;
}

int sum_csv_numbers(const std::string& input)
{
    return static_cast<int>(sum_row(std::string_view(input)));
}

std::vector<int> parse_csv_flat(const std::string& csv_text, bool skip_header)
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
            parse_row_into(line, result);  // appends directly — no per-row vector
        }

        if (line_end == std::string_view::npos)
            break;

        line_start = line_end + 1;
    }

    return result;
}

long long sum_csv_all(const std::string& csv_text, bool skip_header)
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
            total += sum_row(line);  // no flat vector built — streaming sum
        }

        if (line_end == std::string_view::npos)
            break;

        line_start = line_end + 1;
    }

    return total;
}

CsvShape parse_csv_into_buffer(
    const std::string&        csv_text,
    bool                      skip_header,

    std::vector<int32_t>&     out,
    std::vector<std::string>* header_out)
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
    bool        is_first   = true;
    std::size_t n_cols     = 0;
    std::size_t n_rows     = 0;

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

            if (header_out)
            {
                header_out->clear();
                std::size_t s = 0;

                while (s <= line.size())
                {
                    const std::size_t c = line.find(',', s);
                    const std::size_t l = (c == std::string_view::npos)
                        ? line.size() - s : c - s;

                    header_out->emplace_back(trim_view(line.substr(s, l)));

                    if (c == std::string_view::npos) break;

                    s = c + 1;
                }
            }
        }
        else if (!line.empty())
        {
            is_first = false;

            if (n_rows == 0)
            {
                n_cols = 1;
                for (char ch : line) if (ch == ',') ++n_cols;
                out.reserve(approx_rows * n_cols);
            }

            std::size_t s = 0;
            while (s <= line.size())
            {
                const std::size_t c     = line.find(',', s);
                const std::size_t l     = (c == std::string_view::npos)
                    ? line.size() - s : c - s;
                const std::string_view token = trim_view(line.substr(s, l));

                if (token.empty())
                    throw std::invalid_argument("Empty CSV field is not allowed");

                bool ok = true;
                const int32_t value = static_cast<int32_t>(parse_int(token, ok));

                if (!ok)
                    throw std::invalid_argument("Invalid integer token: " + std::string(token));

                out.push_back(value);

                if (c == std::string_view::npos) break;
                
                s = c + 1;
            }

            ++n_rows;
        }

        if (line_end == std::string_view::npos) break;

        line_start = line_end + 1;
    }

    return {n_rows, n_cols};
}

}  // namespace tabx