#include "xlsx_parser.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cmath>

#include <zlib.h>

namespace tabx
{

namespace
{

struct ZipEntry
{
    std::string name;
    std::uint16_t method = 0;
    std::uint32_t compressed_size = 0;
    std::uint32_t uncompressed_size = 0;
    std::uint32_t local_header_offset = 0;
};

inline std::uint16_t read_u16_le(const std::vector<std::uint8_t>& data, std::size_t off)
{
    if (off + 2 > data.size())
        throw std::runtime_error("Unexpected end of ZIP while reading uint16");

    return static_cast<std::uint16_t>(data[off]) |
           (static_cast<std::uint16_t>(data[off + 1]) << 8);
}

inline std::uint32_t read_u32_le(const std::vector<std::uint8_t>& data, std::size_t off)
{
    if (off + 4 > data.size())
        throw std::runtime_error("Unexpected end of ZIP while reading uint32");

    return static_cast<std::uint32_t>(data[off]) |
           (static_cast<std::uint32_t>(data[off + 1]) << 8) |
           (static_cast<std::uint32_t>(data[off + 2]) << 16) |
           (static_cast<std::uint32_t>(data[off + 3]) << 24);
}

std::vector<std::uint8_t> read_file_bytes(const std::string& file_path)
{
    std::ifstream file(file_path, std::ios::binary);

    if (!file)
        throw std::runtime_error("Cannot open XLSX file: " + file_path);

    file.seekg(0, std::ios::end);

    const std::streamoff end = file.tellg();

    if (end < 0)
        throw std::runtime_error("Cannot determine XLSX file size: " + file_path);

    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));

    if (!bytes.empty())
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

    if (!file && !bytes.empty())
        throw std::runtime_error("Failed to read XLSX file: " + file_path);

    return bytes;
}

std::unordered_map<std::string, ZipEntry> parse_zip_central_directory(
    const std::vector<std::uint8_t>& bytes)
{
    constexpr std::uint32_t sig_eocd = 0x06054b50;
    constexpr std::uint32_t sig_cdfh = 0x02014b50;

    if (bytes.size() < 22)
        throw std::runtime_error("Invalid XLSX: file too small for ZIP EOCD");

    const std::size_t search_window = std::min<std::size_t>(bytes.size(), 22 + 65535);
    const std::size_t start = bytes.size() - search_window;

    std::size_t eocd_off = std::string::npos;
    for (std::size_t off = bytes.size() - 22 + 1; off-- > start;)
    {
        if (read_u32_le(bytes, off) == sig_eocd)
        {
            eocd_off = off;
            break;
        }
    }

    if (eocd_off == std::string::npos)
        throw std::runtime_error("Invalid XLSX: ZIP EOCD not found");

    const std::uint16_t entries = read_u16_le(bytes, eocd_off + 10);
    const std::uint32_t cd_size = read_u32_le(bytes, eocd_off + 12);
    const std::uint32_t cd_off = read_u32_le(bytes, eocd_off + 16);

    if (static_cast<std::size_t>(cd_off) + static_cast<std::size_t>(cd_size) > bytes.size())
        throw std::runtime_error("Invalid XLSX: central directory out of bounds");

    std::unordered_map<std::string, ZipEntry> out;
    out.reserve(entries);

    std::size_t off = cd_off;
    for (std::uint16_t i = 0; i < entries; ++i)
    {
        if (off + 46 > bytes.size() || read_u32_le(bytes, off) != sig_cdfh)
            throw std::runtime_error("Invalid XLSX: malformed central directory");

        const std::uint16_t method = read_u16_le(bytes, off + 10);
        const std::uint32_t compressed_size = read_u32_le(bytes, off + 20);
        const std::uint32_t uncompressed_size = read_u32_le(bytes, off + 24);
        const std::uint16_t name_len = read_u16_le(bytes, off + 28);
        const std::uint16_t extra_len = read_u16_le(bytes, off + 30);
        const std::uint16_t comment_len = read_u16_le(bytes, off + 32);
        const std::uint32_t local_header_offset = read_u32_le(bytes, off + 42);

        const std::size_t name_off = off + 46;
        if (name_off + name_len > bytes.size())
            throw std::runtime_error("Invalid XLSX: file name outside archive");

        std::string name(
            reinterpret_cast<const char*>(bytes.data() + name_off),
            static_cast<std::size_t>(name_len));

        out.emplace(name, ZipEntry{name, method, compressed_size, uncompressed_size, local_header_offset});

        off = name_off + name_len + extra_len + comment_len;
    }

    return out;
}

std::string inflate_raw_deflate(const std::uint8_t* compressed,
                                std::size_t compressed_size,
                                std::size_t uncompressed_size)
{
    std::string out;
    out.resize(uncompressed_size);

    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed));
    strm.avail_in = static_cast<uInt>(compressed_size);
    strm.next_out = reinterpret_cast<Bytef*>(&out[0]);
    strm.avail_out = static_cast<uInt>(uncompressed_size);

    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
        throw std::runtime_error("zlib inflateInit2 failed for XLSX entry");

    const int rc = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);

    if (rc != Z_STREAM_END)
        throw std::runtime_error("Failed to inflate XLSX entry (deflate stream error)");

    if (strm.total_out != uncompressed_size)
        throw std::runtime_error("Inflated XLSX entry size mismatch");

    return out;
}

std::string extract_zip_entry(const std::vector<std::uint8_t>& bytes,
                              const std::unordered_map<std::string, ZipEntry>& entries,
                              const std::string& name)
{
    constexpr std::uint32_t sig_lfh = 0x04034b50;

    const auto it = entries.find(name);

    if (it == entries.end())
        throw std::runtime_error("Missing XLSX part: " + name);

    const ZipEntry& entry = it->second;
    const std::size_t lfh_off = static_cast<std::size_t>(entry.local_header_offset);

    if (lfh_off + 30 > bytes.size() || read_u32_le(bytes, lfh_off) != sig_lfh)
        throw std::runtime_error("Malformed local file header for XLSX part: " + name);

    const std::uint16_t name_len = read_u16_le(bytes, lfh_off + 26);
    const std::uint16_t extra_len = read_u16_le(bytes, lfh_off + 28);
    const std::size_t data_off = lfh_off + 30 + name_len + extra_len;
    const std::size_t comp_size = static_cast<std::size_t>(entry.compressed_size);
    const std::size_t uncomp_size = static_cast<std::size_t>(entry.uncompressed_size);

    if (data_off + comp_size > bytes.size())
        throw std::runtime_error("Compressed XLSX entry out of bounds: " + name);

    const std::uint8_t* payload = bytes.data() + data_off;

    if (entry.method == 0)
    {
        return std::string(reinterpret_cast<const char*>(payload), comp_size);
    }

    if (entry.method == 8)
    {
        return inflate_raw_deflate(payload, comp_size, uncomp_size);
    }

    throw std::runtime_error("Unsupported XLSX ZIP compression method for " + name);
}

inline std::string trim_copy(const std::string& s)
{
    std::size_t b = 0;

    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;

    std::size_t e = s.size();

    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;

    return s.substr(b, e - b);
}

std::string xml_unescape(std::string s)
{
    const std::pair<const char*, const char*> entities[] = {
        {"&amp;", "&"},
        {"&lt;", "<"},
        {"&gt;", ">"},
        {"&quot;", "\""},
        {"&apos;", "'"},
    };

    for (const auto& ent : entities)
    {
        std::size_t pos = 0;

        while ((pos = s.find(ent.first, pos)) != std::string::npos)
        {
            s.replace(pos, std::strlen(ent.first), ent.second);
            pos += std::strlen(ent.second);
        }
    }

    return s;
}

std::size_t find_tag_end(const std::string& xml, std::size_t start)
{
    bool in_quote = false;

    for (std::size_t i = start; i < xml.size(); ++i)
    {
        const char c = xml[i];

        if (c == '"')
            in_quote = !in_quote;

        else if (c == '>' && !in_quote)
            return i;
    }

    throw std::runtime_error("Malformed XML: unterminated tag");
}

std::string get_attr_value(const std::string& tag, const std::string& attr)
{
    const std::string key = attr + "=\"";
    const std::size_t pos = tag.find(key);

    if (pos == std::string::npos)
        return {};

    const std::size_t val_start = pos + key.size();
    const std::size_t val_end = tag.find('"', val_start);

    if (val_end == std::string::npos)
        throw std::runtime_error("Malformed XML attribute: " + attr);

    return xml_unescape(tag.substr(val_start, val_end - val_start));
}

std::string extract_first_tag_text(const std::string& xml, const std::string& tag_name)
{
    const std::string open1 = "<" + tag_name + ">";
    const std::string open2 = "<" + tag_name + " ";

    std::size_t open = xml.find(open1);
    std::size_t open_len = open1.size();

    if (open == std::string::npos)
    {
        open = xml.find(open2);

        if (open == std::string::npos)
            return {};

        const std::size_t open_end = find_tag_end(xml, open);
        open_len = open_end - open + 1;
    }

    const std::size_t content_start = open + open_len;
    const std::string close = "</" + tag_name + ">";
    const std::size_t close_pos = xml.find(close, content_start);

    if (close_pos == std::string::npos)
        throw std::runtime_error("Malformed XML: missing closing tag for " + tag_name);

    return xml_unescape(xml.substr(content_start, close_pos - content_start));
}

std::int32_t parse_int32_token(const std::string& token)
{
    const std::string t = trim_copy(token);
    if (t.empty())
        throw std::runtime_error("Empty XLSX numeric token");

    std::size_t i = 0;
    bool negative = false;

    if (t[i] == '+' || t[i] == '-')
    {
        negative = (t[i] == '-');
        ++i;
    }

    if (i >= t.size())
        throw std::runtime_error("Invalid XLSX numeric token: " + token);

    std::int64_t value = 0;

    for (; i < t.size(); ++i)
    {
        const char c = t[i];
        if (c < '0' || c > '9')
            throw std::runtime_error("Non-integer XLSX numeric token: " + token);

        value = value * 10 + static_cast<std::int64_t>(c - '0');
    }

    if (negative)
        value = -value;

    if (value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max())
    {
        throw std::runtime_error("XLSX integer out of int32 range: " + token);
    }

    return static_cast<std::int32_t>(value);
}

std::vector<XlsxSheet> parse_workbook_sheets(const std::string& workbook_xml)
{
    std::vector<XlsxSheet> sheets;
    std::size_t pos = 0;

    while ((pos = workbook_xml.find("<sheet", pos)) != std::string::npos)
    {
        const std::size_t end = find_tag_end(workbook_xml, pos);
        const std::string tag = workbook_xml.substr(pos, end - pos + 1);
        const std::string name = get_attr_value(tag, "name");

        if (!name.empty())
            sheets.push_back(XlsxSheet{name, sheets.size()});

        pos = end + 1;
    }

    if (sheets.empty())
        throw std::runtime_error("Invalid XLSX: no <sheet> entries in workbook.xml");

    return sheets;
}

std::unordered_map<std::string, std::string> parse_workbook_sheet_rids(const std::string& workbook_xml)
{
    std::unordered_map<std::string, std::string> map;
    std::size_t pos = 0;

    while ((pos = workbook_xml.find("<sheet", pos)) != std::string::npos)
    {
        const std::size_t end = find_tag_end(workbook_xml, pos);
        const std::string tag = workbook_xml.substr(pos, end - pos + 1);
        const std::string name = get_attr_value(tag, "name");
        const std::string rid = get_attr_value(tag, "r:id");

        if (!name.empty() && !rid.empty())
            map.emplace(name, rid);

        pos = end + 1;
    }

    return map;
}

std::unordered_map<std::string, std::string> parse_relationship_targets(const std::string& rels_xml)
{
    std::unordered_map<std::string, std::string> map;
    std::size_t pos = 0;

    while ((pos = rels_xml.find("<Relationship", pos)) != std::string::npos)
    {
        const std::size_t end = find_tag_end(rels_xml, pos);
        const std::string tag = rels_xml.substr(pos, end - pos + 1);
        const std::string id = get_attr_value(tag, "Id");
        std::string target = get_attr_value(tag, "Target");

        if (!id.empty() && !target.empty())
        {
            if (!target.empty() && target.front() == '/')
                target.erase(target.begin());
            else if (target.rfind("xl/", 0) != 0)
                target = "xl/" + target;

            map.emplace(id, target);
        }

        pos = end + 1;
    }

    return map;
}

std::string resolve_sheet_xml_path(const std::string& workbook_xml,
                                   const std::string& rels_xml,
                                   const std::string& sheet_name)
{
    const auto sheet_rids = parse_workbook_sheet_rids(workbook_xml);

    if (sheet_rids.empty())
        throw std::runtime_error("Invalid XLSX: workbook has no sheets");

    std::string selected_name = sheet_name;

    if (selected_name.empty())
        selected_name = sheet_rids.begin()->first;

    const auto rid_it = sheet_rids.find(selected_name);

    if (rid_it == sheet_rids.end())
        throw std::runtime_error("Sheet not found in workbook: " + selected_name);

    const auto targets = parse_relationship_targets(rels_xml);
    const auto t_it = targets.find(rid_it->second);

    if (t_it == targets.end())
        throw std::runtime_error("Missing relationship target for sheet: " + selected_name);

    return t_it->second;
}

std::vector<std::string> parse_shared_strings(const std::string& shared_xml)
{
    std::vector<std::string> shared;
    std::size_t pos = 0;

    while ((pos = shared_xml.find("<si", pos)) != std::string::npos)
    {
        const std::size_t start_end = find_tag_end(shared_xml, pos);
        const std::size_t close = shared_xml.find("</si>", start_end + 1);

        if (close == std::string::npos)
            throw std::runtime_error("Malformed sharedStrings.xml: missing </si>");

        const std::string si = shared_xml.substr(start_end + 1, close - (start_end + 1));

        std::string value;
        std::size_t p = 0;

        while (true)
        {
            std::size_t t_open = si.find("<t>", p);
            std::size_t t_open_len = 3;

            if (t_open == std::string::npos)
            {
                t_open = si.find("<t ", p);

                if (t_open == std::string::npos)
                    break;

                const std::size_t t_end = find_tag_end(si, t_open);
                t_open_len = t_end - t_open + 1;
            }

            const std::size_t data_start = t_open + t_open_len;
            const std::size_t t_close = si.find("</t>", data_start);

            if (t_close == std::string::npos)
                throw std::runtime_error("Malformed sharedStrings.xml: missing </t>");

            value += xml_unescape(si.substr(data_start, t_close - data_start));
            p = t_close + 4;
        }

        shared.push_back(value);
        pos = close + 5;
    }

    return shared;
}

std::size_t count_rows_in_sheet(const std::string& sheet_xml)
{
    std::size_t count = 0;
    std::size_t pos = 0;

    while ((pos = sheet_xml.find("<row", pos)) != std::string::npos)
    {
        ++count;
        pos += 4;
    }

    return count;
}

inline std::size_t col_letters_to_index(const char* begin, const char* end) noexcept
{
    std::size_t index = 0;

    for (const char* p = begin; p != end; ++p)
    {
        index = index * 26 + static_cast<std::size_t>(*p - 'A' + 1);
    }

    return index - 1;
}

inline void parse_cell_address(
    const char* address,
    std::size_t address_len,
    std::size_t& col_out,
    std::size_t& row_out)
{
    const char* p = address;
    const char* end = address + address_len;

    const char* col_end = p;

    while (col_end != end && *col_end >= 'A' && *col_end <= 'Z')
        ++col_end;

    if (col_end == p || col_end == end)
        throw std::invalid_argument("Malformed cell address: " + std::string(address, address_len));

    col_out = col_letters_to_index(p, col_end);

    std::size_t row = 0;
    for (const char* q = col_end; q != end; ++q)
    {
        if (*q < '0' || *q > '9')
            throw std::invalid_argument("Malformed cell address: " + std::string(address, address_len));

        row = row * 10 + static_cast<std::size_t>(*q - '0');
    }

    if (row == 0)
        throw std::invalid_argument("Malformed cell address: " + std::string(address, address_len));

    row_out = row - 1;
}

}  // namespace

    // ---- Helpers for mixed-type parsing ----------------------------------------

    bool try_parse_int64(const std::string& s, std::int64_t& out)
    {
        if (s.empty())
            return false;

        // Presence of '.', 'e', or 'E' means it is a floating-point literal.
        for (const char c : s)
        {
            if (c == '.' || c == 'e' || c == 'E')
                return false;
        }

        try
        {
            std::size_t pos = 0;
            out = std::stoll(s, &pos);
            return pos == s.size();
        }
        catch (...)
        {
            return false;
        }
    }

    bool try_parse_double(const std::string& s, double& out)
    {
        if (s.empty())
            return false;

        try
        {
            std::size_t pos = 0;
            out = std::stod(s, &pos);
            return pos == s.size() && std::isfinite(out);
        }
        catch (...)
        {
            return false;
        }
    }

    // Classify one XLSX cell.
    // resolved_value: the raw <v> text, with shared-string lookup already applied.
    // xlsx_type: the raw value of the t="" attribute on <c> (may be empty).
    XlsxRawCell classify_xlsx_cell(const std::string& resolved_value,
                                    const std::string& xlsx_type)
    {
        // Boolean: t="b", value is "0" or "1"
        if (xlsx_type == "b")
        {
            const std::string t = trim_copy(resolved_value);
            return XlsxRawCell{XlsxCellKind::Boolean, 0.0, "", t == "1"};
        }

        // Formula error: t="e"
        if (xlsx_type == "e")
            return XlsxRawCell{XlsxCellKind::Error, 0.0, resolved_value, false};

        // Text: shared-string (already resolved), formula string, or inline string
        if (xlsx_type == "s" || xlsx_type == "str" || xlsx_type == "inlineStr")
            {
                const std::string text = trim_copy(resolved_value);
                if (text.empty())
                    return XlsxRawCell{XlsxCellKind::Empty, 0.0, "", false};
                return XlsxRawCell{XlsxCellKind::String, 0.0, resolved_value, false};
            }

        // Numeric cell: t="" or t="n"
        const std::string t = trim_copy(resolved_value);

        if (t.empty())
            return XlsxRawCell{XlsxCellKind::Empty, 0.0, "", false};

        std::int64_t ival = 0;

        if (try_parse_int64(t, ival))
            return XlsxRawCell{XlsxCellKind::Integer, static_cast<double>(ival), "", false};

        double dval = 0.0;

        if (try_parse_double(t, dval))
            return XlsxRawCell{XlsxCellKind::Float, dval, "", false};

        // Last resort: treat unexpected text in a numeric cell as a string
        return XlsxRawCell{XlsxCellKind::String, 0.0, t, false};
    }

std::vector<XlsxSheet> list_xlsx_sheets(const std::string& file_path)
{
    const std::vector<std::uint8_t> bytes = read_file_bytes(file_path);
    const auto entries = parse_zip_central_directory(bytes);
    const std::string workbook_xml = extract_zip_entry(bytes, entries, "xl/workbook.xml");

    return parse_workbook_sheets(workbook_xml);
}

CsvShape parse_xlsx_into_buffer(
    const std::string& file_path,
    const std::string& sheet_name,
    bool skip_header,
    std::vector<int32_t>& out,
    std::vector<std::string>* header_out)
{
    const std::vector<std::uint8_t> bytes = read_file_bytes(file_path);

    return parse_xlsx_into_buffer(bytes, sheet_name, skip_header, out, header_out);
}

CsvShape parse_xlsx_into_buffer(
    const std::vector<uint8_t>& xlsx_bytes,
    const std::string& sheet_name,
    bool skip_header,
    std::vector<int32_t>&       out,
    std::vector<std::string>*   header_out)
{
    const auto entries = parse_zip_central_directory(xlsx_bytes);

    const std::string workbook_xml = extract_zip_entry(xlsx_bytes, entries, "xl/workbook.xml");
    const std::string rels_xml = extract_zip_entry(xlsx_bytes, entries, "xl/_rels/workbook.xml.rels");
    const std::string sheet_xml_path = resolve_sheet_xml_path(workbook_xml, rels_xml, sheet_name);
    const std::string sheet_xml = extract_zip_entry(xlsx_bytes, entries, sheet_xml_path);

    std::vector<std::string> shared_strings;
    const auto sst_it = entries.find("xl/sharedStrings.xml");

    if (sst_it != entries.end())
    {
        const std::string shared_xml = extract_zip_entry(xlsx_bytes, entries, "xl/sharedStrings.xml");
        shared_strings = parse_shared_strings(shared_xml);
    }

    out.clear();

    if (header_out)
        header_out->clear();

    const std::size_t row_count = count_rows_in_sheet(sheet_xml);
    const std::size_t reserve_rows = (skip_header && row_count > 0) ? row_count - 1 : row_count;
    out.reserve(reserve_rows * 8);

    std::size_t cols = 0;
    std::size_t data_rows = 0;
    bool header_done = !skip_header;

    std::size_t pos = 0;

    while ((pos = sheet_xml.find("<row", pos)) != std::string::npos)
    {
        const std::size_t row_open_end = find_tag_end(sheet_xml, pos);
        const std::size_t row_close = sheet_xml.find("</row>", row_open_end + 1);
        if (row_close == std::string::npos)
            throw std::runtime_error("Malformed sheet XML: missing </row>");

        const std::string row_xml = sheet_xml.substr(row_open_end + 1, row_close - (row_open_end + 1));

        std::unordered_map<std::size_t, std::string> row_cells;
        std::size_t max_col = 0;
        std::size_t cpos = 0;

        while ((cpos = row_xml.find("<c", cpos)) != std::string::npos)
        {
            const std::size_t c_open_end = find_tag_end(row_xml, cpos);
            const std::string c_tag = row_xml.substr(cpos, c_open_end - cpos + 1);

            const std::string r_attr = get_attr_value(c_tag, "r");

            if (r_attr.empty())
                throw std::runtime_error("Malformed XLSX cell: missing r attribute");

            std::size_t col_idx = 0;
            std::size_t row_idx_dummy = 0;
            parse_cell_address(r_attr.c_str(), r_attr.size(), col_idx, row_idx_dummy);
            max_col = std::max(max_col, col_idx);

            const std::string type = get_attr_value(c_tag, "t");
            const bool self_closing = c_open_end > cpos && row_xml[c_open_end - 1] == '/';

            std::string raw_value;
            std::size_t next_pos = c_open_end + 1;

            if (!self_closing)
            {
                const std::size_t c_close = row_xml.find("</c>", c_open_end + 1);

                if (c_close == std::string::npos)
                    throw std::runtime_error("Malformed XLSX cell: missing </c>");

                const std::string cell_inner = row_xml.substr(c_open_end + 1, c_close - (c_open_end + 1));

                if (type == "inlineStr")
                    raw_value = extract_first_tag_text(cell_inner, "t");

                else
                    raw_value = extract_first_tag_text(cell_inner, "v");

                next_pos = c_close + 4;
            }

            if (type == "s")
            {
                if (raw_value.empty())
                    throw std::runtime_error("Shared-string cell without <v> index");

                const std::int32_t idx = parse_int32_token(raw_value);

                if (idx < 0 || static_cast<std::size_t>(idx) >= shared_strings.size())
                    throw std::runtime_error("Shared-string index out of range");

                raw_value = shared_strings[static_cast<std::size_t>(idx)];
            }

            row_cells[col_idx] = raw_value;
            cpos = next_pos;
        }

        const std::size_t row_cols = row_cells.empty() ? 0 : (max_col + 1);

        if (row_cols == 0)
        {
            pos = row_close + 6;
            continue;
        }

        if (!header_done)
        {
            cols = row_cols;

            if (header_out)
            {
                header_out->reserve(cols);

                for (std::size_t c = 0; c < cols; ++c)
                {
                    const auto it = row_cells.find(c);

                    if (it == row_cells.end())
                        throw std::runtime_error("Header row has missing cells");

                    const std::string h = trim_copy(it->second);

                    if (h.empty())
                        throw std::runtime_error("Header contains empty cell");

                    header_out->push_back(h);
                }
            }
            header_done = true;
            pos = row_close + 6;
            continue;
        }

        if (cols == 0)
            cols = row_cols;

        if (row_cols != cols)
            throw std::runtime_error("Inconsistent XLSX row width");

        for (std::size_t c = 0; c < cols; ++c)
        {
            const auto it = row_cells.find(c);

            if (it == row_cells.end())
                throw std::runtime_error("Sparse XLSX rows are not supported");

            const std::string t = trim_copy(it->second);
            
            if (t.empty())
                throw std::runtime_error("Empty XLSX cell is not allowed");

            out.push_back(parse_int32_token(t));
        }

        ++data_rows;
        pos = row_close + 6;
    }

    return CsvShape{data_rows, cols};
}


XlsxMixedResult parse_xlsx_mixed(
    const std::string& file_path,
    const std::string& sheet_name,
    bool skip_header)
{
    const std::vector<std::uint8_t> bytes = read_file_bytes(file_path);
    return parse_xlsx_mixed(bytes, sheet_name, skip_header);
}

XlsxMixedResult parse_xlsx_mixed(
    const std::vector<uint8_t>& xlsx_bytes,
    const std::string& sheet_name,
    bool skip_header)
{
    const auto entries          = parse_zip_central_directory(xlsx_bytes);
    const std::string workbook_xml  = extract_zip_entry(xlsx_bytes, entries, "xl/workbook.xml");
    const std::string rels_xml      = extract_zip_entry(xlsx_bytes, entries, "xl/_rels/workbook.xml.rels");
    const std::string sheet_xml_path = resolve_sheet_xml_path(workbook_xml, rels_xml, sheet_name);
    const std::string sheet_xml     = extract_zip_entry(xlsx_bytes, entries, sheet_xml_path);

    std::vector<std::string> shared_strings;
    if (entries.count("xl/sharedStrings.xml"))
    {
        const std::string sst = extract_zip_entry(xlsx_bytes, entries, "xl/sharedStrings.xml");
        shared_strings = parse_shared_strings(sst);
    }

    // Each data row is a sparse map: col_index → classified cell
    using RowMap = std::unordered_map<std::size_t, XlsxRawCell>;

    std::vector<RowMap> rows_data;
    std::vector<std::string> headers;
    std::size_t max_cols  = 0;

    bool hdr_done  = !skip_header;

    std::size_t pos = 0;
    while ((pos = sheet_xml.find("<row", pos)) != std::string::npos)
    {
        const std::size_t row_open_end = find_tag_end(sheet_xml, pos);
        const std::size_t row_close    = sheet_xml.find("</row>", row_open_end + 1);

        if (row_close == std::string::npos)
            throw std::runtime_error("Malformed sheet XML: missing </row>");

        const std::string row_xml =
            sheet_xml.substr(row_open_end + 1, row_close - (row_open_end + 1));

        // Collect raw (resolved) values for every cell in this row
        // key: col_index, value: (resolved_text, xlsx_type_attr)
        std::unordered_map<std::size_t, std::pair<std::string, std::string>> raw_row;
        std::size_t max_col_in_row = 0;
        std::size_t cpos = 0;

        while ((cpos = row_xml.find("<c", cpos)) != std::string::npos)
        {
            const std::size_t c_open_end = find_tag_end(row_xml, cpos);
            const std::string c_tag = row_xml.substr(cpos, c_open_end - cpos + 1);
            const std::string r_attr = get_attr_value(c_tag, "r");

            if (r_attr.empty())
                throw std::runtime_error("Malformed XLSX cell: missing r attribute");

            std::size_t col_idx = 0, row_dummy = 0;
            parse_cell_address(r_attr.c_str(), r_attr.size(), col_idx, row_dummy);
            max_col_in_row = std::max(max_col_in_row, col_idx);

            const std::string xlsx_type  = get_attr_value(c_tag, "t");
            const bool self_closing = c_open_end > cpos && row_xml[c_open_end - 1] == '/';

            std::string raw_value;
            std::size_t next_pos = c_open_end + 1;

            if (!self_closing)
            {
                const std::size_t c_close = row_xml.find("</c>", c_open_end + 1);

                if (c_close == std::string::npos)
                    throw std::runtime_error("Malformed XLSX cell: missing </c>");

                const std::string inner =
                    row_xml.substr(c_open_end + 1, c_close - (c_open_end + 1));

                raw_value = (xlsx_type == "inlineStr")
                    ? extract_first_tag_text(inner, "t")
                    : extract_first_tag_text(inner, "v");

                next_pos = c_close + 4;
            }

            // Resolve shared-string index → actual text
            if (xlsx_type == "s" && !raw_value.empty())
            {
                const std::int32_t idx = parse_int32_token(raw_value);

                if (idx < 0 || static_cast<std::size_t>(idx) >= shared_strings.size())
                    throw std::runtime_error("Shared-string index out of range");

                raw_value = shared_strings[static_cast<std::size_t>(idx)];
            }

            raw_row[col_idx] = {raw_value, xlsx_type};
            cpos = next_pos;
        }

        const std::size_t row_width = raw_row.empty() ? 0 : (max_col_in_row + 1);
        
        if (row_width == 0)
        {
            pos = row_close + 6;
            continue;
        }

        if (!hdr_done)
        {
            max_cols = std::max(max_cols, row_width);
            headers.reserve(row_width);

            for (std::size_t c = 0; c < row_width; ++c)
            {
                const auto it = raw_row.find(c);
                std::string h = (it != raw_row.end()) ? trim_copy(it->second.first) : "";

                if (h.empty())
                    h = "Column_" + std::to_string(c);

                headers.push_back(std::move(h));
            }

            hdr_done = true;
            pos = row_close + 6;
            continue;
        }

        // Data row: classify each cell and store in sparse map
        max_cols = std::max(max_cols, row_width);
        RowMap row_map;
        row_map.reserve(raw_row.size());

        for (const auto& kv : raw_row)
            row_map[kv.first] = classify_xlsx_cell(kv.second.first, kv.second.second);

        rows_data.push_back(std::move(row_map));

        pos = row_close + 6;
    }

    // Build the result: fill a dense row-major matrix, Empty for missing cells
    XlsxMixedResult result;
    result.rows = rows_data.size();
    result.cols = max_cols;
    result.headers = std::move(headers);

    // Auto-generate names for columns that exceed the header row width
    while (result.headers.size() < result.cols)
        result.headers.push_back("Column_" + std::to_string(result.headers.size()));

    result.cells.resize(result.rows * result.cols);  // default-constructed = Empty

    for (std::size_t r = 0; r < result.rows; ++r)
    {
        for (const auto& kv : rows_data[r])
        {
            if (kv.first < result.cols)
                result.cells[r * result.cols + kv.first] = kv.second;
        }
    }

    return result;
}

}  // namespace tabx
