#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <cmath>
#include <limits>
#include <string>

#include "csv_parser.h"
#include "xlsx_parser.h"

namespace py = pybind11;

namespace
{

tabx::CsvShapeMode parse_csv_shape_mode(const std::string& shape_mode)
{
	if (shape_mode == "strict")
		return tabx::CsvShapeMode::Strict;

	if (shape_mode == "permissive")
		return tabx::CsvShapeMode::Permissive;

	throw py::value_error("shape_mode must be either 'strict' or 'permissive'");
}

void maybe_warn_about_ragged_csv(const tabx::CsvShapeInfo& shape_info)
{
	if (shape_info.ragged_rows == 0)
		return;

	std::string message =
		"Permissive CSV shape mode detected " +
		std::to_string(shape_info.ragged_rows) +
		" ragged row(s); expected " +
		std::to_string(shape_info.expected_cols) +
		" column(s), observed between " +
		std::to_string(shape_info.min_observed_cols) +
		" and " +
		std::to_string(shape_info.max_observed_cols) +
		". Missing cells were padded with nulls.";

	if (shape_info.max_observed_cols > shape_info.expected_cols)
		message += " Wider rows expanded the inferred schema.";

	py::module_::import("warnings").attr("warn")(
		py::str(message),
		py::module_::import("builtins").attr("RuntimeWarning"));
}

}  // namespace

PYBIND11_MODULE(_core, module)
{
	module.doc() =
		"Native C++17 CSV/XLSX parser exposed via pybind11.\n\n"
		"Includes both integer fast paths (NumPy int32) and mixed-type\n"
		"paths for pandas-like DataFrame construction.";

	module.def(
		"parse_csv_numbers",
		&tabx::parse_csv_numbers,
		py::arg("input"),
		"Parse a single-row CSV string into a list of integers.\n\n"
		"Args:\n"
		"    input (str): Comma-separated integer values, for example '\n"
		"        1, 2, 3'.\n\n"
		"Returns:\n"
		"    list[int]: Parsed integer values in input order.\n\n"
		"Raises:\n"
		"    ValueError: If a field is empty or cannot be parsed as an\n"
		"        integer."
	);

	module.def(
		"sum_csv_numbers",
		&tabx::sum_csv_numbers,
		py::arg("input"),
		"Return the sum of all integers in a single-row CSV string.\n\n"
		"Args:\n"
		"    input (str): Comma-separated integer values, for example '\n"
		"        1, 2, 3'.\n\n"
		"Returns:\n"
		"    int: Sum of all parsed integers.\n\n"
		"Raises:\n"
		"    ValueError: If a field is empty or cannot be parsed as an\n"
		"        integer."
	);

	module.def(
		"parse_csv_flat",
		&tabx::parse_csv_flat,
		py::arg("csv_text"),
		py::arg("skip_header") = false,
		"Parse every integer cell in a multi-row CSV string into a flat list.\n\n"
		"Rows are separated by newline characters.  Blank lines are silently\n"
		"skipped.  When skip_header is True the first line is treated as a\n"
		"column-name header and excluded from the output.\n\n"
		"Args:\n"
		"    csv_text (str): Full CSV text with rows separated by '\\n'.\n"
		"    skip_header (bool): If True, skip the first row.\n\n"
		"Returns:\n"
		"    list[int]: All integer values in document order.\n\n"
		"Raises:\n"
		"    ValueError: If any field is empty or cannot be parsed as an\n"
		"        integer."
	);

	module.def(
		"sum_csv_all",
		&tabx::sum_csv_all,
		py::arg("csv_text"),
		py::arg("skip_header") = false,
		"Sum every integer in a multi-row CSV string.\n\n"
		"Args:\n"
		"    csv_text (str): Full CSV text with rows separated by '\\n'.\n"
		"    skip_header (bool): If True, skip the first row.\n\n"
		"Returns:\n"
		"    int: Sum of all parsed integers.\n\n"
		"Raises:\n"
		"    ValueError: If any field is empty or cannot be parsed as an\n"
		"        integer."
	);
	module.def(
		"parse_csv_numpy",
		[](const std::string& csv_text, bool skip_header) -> py::tuple
		{
			std::vector<int32_t>     data;
			std::vector<std::string> headers;

			const tabx::CsvShape shape = tabx::parse_csv_into_buffer(
				csv_text, skip_header, data, skip_header ? &headers : nullptr);

			if (shape.rows == 0 || shape.cols == 0)
			{
				py::list empty;
				return py::make_tuple(
					py::array_t<int32_t>(
						std::vector<py::ssize_t>{0, 0}),
					empty);
			}

			// Transfer vector's heap buffer to NumPy with zero copy via capsule.
			auto* heap = new std::vector<int32_t>(std::move(data));
			py::capsule owner(heap, [](void* p)
			{
				delete static_cast<std::vector<int32_t>*>(p);
			});

			py::array_t<int32_t> arr(
				{static_cast<py::ssize_t>(shape.rows),
				 static_cast<py::ssize_t>(shape.cols)},
				{static_cast<py::ssize_t>(shape.cols * sizeof(int32_t)),
				 static_cast<py::ssize_t>(sizeof(int32_t))},
				heap->data(),
				owner);

			py::list col_names;
			if (skip_header && !headers.empty())
				for (auto& h : headers) col_names.append(py::str(h));
			else
				for (std::size_t i = 0; i < shape.cols; ++i)
					col_names.append(py::int_(static_cast<long long>(i)));

			return py::make_tuple(arr, col_names);
		},
		py::arg("csv_text"),
		py::arg("skip_header") = false,
		"Parse a multi-row integer CSV into a zero-copy 2-D NumPy int32 array.\n\n"
		"Performs a fast newline-count scan to pre-allocate, then a single\n"
		"parsing pass that writes directly into the NumPy buffer. The\n"
		"returned array shares memory with no intermediate copy.\n\n"
		"Args:\n"
		"    csv_text (str): Full CSV text with rows separated by '\\n'.\n"
		"    skip_header (bool): If True, treat the first row as column names.\n\n"
		"Returns:\n"
		"    tuple[np.ndarray, list]: (array of shape (rows, cols), column names).\n\n"
		"Example:\n"
		"    arr, cols = parse_csv_numpy(text, skip_header=True)\n"
		"    df = pd.DataFrame(arr, columns=cols)"
	);

	module.def(
		"parse_csv_mixed",
		[](const std::string& csv_text,
		   bool skip_header,
		   const std::string& shape_mode,
		   bool warn_on_ragged) -> py::tuple
		{
			tabx::CsvMixedResult res = tabx::parse_csv_mixed(
				csv_text,
				skip_header,
				parse_csv_shape_mode(shape_mode));

			if (warn_on_ragged)
				maybe_warn_about_ragged_csv(res.shape_info);

			const std::size_t R = res.rows;
			const std::size_t C = res.cols;
			constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

			py::list col_arrays;
			py::list col_names;

			if (!res.headers.empty())
				for (const auto& h : res.headers)
					col_names.append(py::str(h));
					
			else
				for (std::size_t c = 0; c < C; ++c)
					col_names.append(py::int_(static_cast<long long>(c)));

			for (std::size_t c = 0; c < C; ++c)
			{
				bool has_empty = false;
				bool has_int = false;
				bool has_float = false;
				bool has_bool = false;
				bool has_string = false;

				for (std::size_t r = 0; r < R; ++r)
				{
					switch (res.cells[r * C + c].kind)
					{
						case tabx::CsvCellKind::Empty: has_empty = true; break;
						case tabx::CsvCellKind::Integer: has_int = true; break;
						case tabx::CsvCellKind::Float: has_float = true; break;
						case tabx::CsvCellKind::Boolean: has_bool = true; break;
						case tabx::CsvCellKind::String: has_string = true; break;
					}
				}

				if (has_string || (has_bool && (has_int || has_float)))
				{
					py::list lst;
					for (std::size_t r = 0; r < R; ++r)
					{
						const auto& cell = res.cells[r * C + c];

						switch (cell.kind)
						{
							case tabx::CsvCellKind::Empty:
								lst.append(py::none()); break;
							case tabx::CsvCellKind::Integer:
								lst.append(py::int_(static_cast<long long>(
									static_cast<std::int64_t>(cell.dval)))); break;
							case tabx::CsvCellKind::Float:
								lst.append(py::float_(cell.dval)); break;
							case tabx::CsvCellKind::Boolean:
								lst.append(py::bool_(cell.bval)); break;
							case tabx::CsvCellKind::String:
								lst.append(py::str(cell.sval)); break;
						}
					}
					col_arrays.append(lst);
				}
				else if (has_bool && !has_int && !has_float)
				{
					if (has_empty)
					{
						py::list lst;

						for (std::size_t r = 0; r < R; ++r)
						{
							const auto& cell = res.cells[r * C + c];

							if (cell.kind == tabx::CsvCellKind::Boolean)
								lst.append(py::bool_(cell.bval));

							else
								lst.append(py::none());
						}
						col_arrays.append(lst);
					}
					else
					{
						auto* buf = new std::vector<std::uint8_t>(R);

						for (std::size_t r = 0; r < R; ++r)
							(*buf)[r] = res.cells[r * C + c].bval ? 1u : 0u;

						py::capsule owner(buf, [](void* p)
						{
							delete static_cast<std::vector<std::uint8_t>*>(p);
						});

						col_arrays.append(py::array(
							py::dtype("bool"),
							{static_cast<py::ssize_t>(R)},
							{static_cast<py::ssize_t>(1)},
							buf->data(), owner));
					}
				}
				else if ((has_int || has_float) && !has_bool && !has_string)
				{
					if (!has_empty && !has_float)
					{
						auto* buf = new std::vector<std::int64_t>(R);

						for (std::size_t r = 0; r < R; ++r)
							(*buf)[r] = static_cast<std::int64_t>(res.cells[r * C + c].dval);

						py::capsule owner(buf, [](void* p)
						{
							delete static_cast<std::vector<std::int64_t>*>(p);
						});

						py::array_t<std::int64_t> arr(
							{static_cast<py::ssize_t>(R)},
							{static_cast<py::ssize_t>(sizeof(std::int64_t))},
							buf->data(), owner);
						col_arrays.append(arr);
					}
					else
					{
						auto* buf = new std::vector<double>(R, kNaN);

						for (std::size_t r = 0; r < R; ++r)
						{
							const auto& cell = res.cells[r * C + c];
							if (cell.kind == tabx::CsvCellKind::Integer ||
								cell.kind == tabx::CsvCellKind::Float)
								(*buf)[r] = cell.dval;
						}

						py::capsule owner(buf, [](void* p)
						{
							delete static_cast<std::vector<double>*>(p);
						});

						py::array_t<double> arr(
							{static_cast<py::ssize_t>(R)},
							{static_cast<py::ssize_t>(sizeof(double))},
							buf->data(), owner);
						col_arrays.append(arr);
					}
				}
				else
				{
					py::list lst;
					for (std::size_t r = 0; r < R; ++r)
						lst.append(py::none());
					col_arrays.append(lst);
				}
			}

			return py::make_tuple(col_arrays, col_names);
		},
		py::arg("csv_text"),
		py::arg("skip_header") = true,
		py::arg("shape_mode") = "permissive",
		py::arg("warn_on_ragged") = false,
		"Parse CSV into per-column typed arrays (pandas-like inference).\n\n"
		"Args:\n"
		"    csv_text (str): Full CSV text with rows separated by '\\n'.\n"
		"    skip_header (bool): If True, treat the first row as column names.\n"
		"    shape_mode (str): 'strict' rejects ragged rows, 'permissive' pads\n"
		"        missing cells with nulls and preserves wider rows.\n"
		"    warn_on_ragged (bool): Emit a RuntimeWarning when permissive mode\n"
		"        encounters inconsistent row widths.\n\n"
		"Returns:\n"
		"    tuple[list, list]: (column_arrays, column_names)."
	);

	module.def(
		"list_xlsx_sheets",
		[](const std::string& file_path)
		{
			py::list out;
			for (const auto& s : tabx::list_xlsx_sheets(file_path))
			{
				py::dict item;
				item["name"] = py::str(s.name);
				item["index"] = py::int_(static_cast<long long>(s.index));
				out.append(item);
			}
			return out;
		},
		py::arg("file_path"),
		"List worksheets in an XLSX workbook.\n\n"
		"Returns:\n"
		"    list[dict]: [{\"name\": str, \"index\": int}, ...]"
	);

	module.def(
		"parse_xlsx_numpy",
		[](const std::string& file_path, const std::string& sheet_name, bool skip_header) -> py::tuple
		{
			std::vector<int32_t>     data;
			std::vector<std::string> headers;

			const tabx::CsvShape shape = tabx::parse_xlsx_into_buffer(
				file_path,
				sheet_name,
				skip_header,
				data,
				skip_header ? &headers : nullptr);

			if (shape.rows == 0 || shape.cols == 0)
			{
				py::list empty;

				return py::make_tuple(
					py::array_t<int32_t>(std::vector<py::ssize_t>{0, 0}),
					empty);
			}

			auto* heap = new std::vector<int32_t>(std::move(data));
			py::capsule owner(heap, [](void* p)
			{
				delete static_cast<std::vector<int32_t>*>(p);
			});

			py::array_t<int32_t> arr(
				{static_cast<py::ssize_t>(shape.rows),
				 static_cast<py::ssize_t>(shape.cols)},
				{static_cast<py::ssize_t>(shape.cols * sizeof(int32_t)),
				 static_cast<py::ssize_t>(sizeof(int32_t))},
				heap->data(),
				owner);

			py::list col_names;

			if (skip_header && !headers.empty())
				for (auto& h : headers) col_names.append(py::str(h));

			else
				for (std::size_t i = 0; i < shape.cols; ++i)
					col_names.append(py::int_(static_cast<long long>(i)));

			return py::make_tuple(arr, col_names);
		},
		py::arg("file_path"),
		py::arg("sheet_name") = "",
		py::arg("skip_header") = true,
		"Parse an integer XLSX worksheet into a zero-copy 2-D NumPy int32 array.\n\n"
		"Args:\n"
		"    file_path (str): Path to .xlsx file.\n"
		"    sheet_name (str): Sheet to parse; empty string selects first sheet.\n"
		"    skip_header (bool): If True, first row is treated as column names.\n\n"
		"Returns:\n"
		"    tuple[np.ndarray, list]: (array of shape (rows, cols), column names)."
	);

	module.def(
		"parse_xlsx_mixed",
		[](const std::string& file_path,
		   const std::string& sheet_name,
		   bool skip_header) -> py::tuple
		{
			tabx::XlsxMixedResult res =
				tabx::parse_xlsx_mixed(file_path, sheet_name, skip_header);

			const std::size_t R = res.rows;
			const std::size_t C = res.cols;
			constexpr double  kNaN = std::numeric_limits<double>::quiet_NaN();

			py::list col_arrays;
			py::list col_names;

			if (!res.headers.empty())
				for (const auto& h : res.headers)
					col_names.append(py::str(h));

			else
				for (std::size_t c = 0; c < C; ++c)
					col_names.append(py::int_(static_cast<long long>(c)));

			for (std::size_t c = 0; c < C; ++c)
			{
				bool has_empty  = false;
				bool has_int    = false;
				bool has_float  = false;
				bool has_bool   = false;
				bool has_string = false;

				for (std::size_t r = 0; r < R; ++r)
				{
					switch (res.cells[r * C + c].kind)
					{
						case tabx::XlsxCellKind::Empty:
						case tabx::XlsxCellKind::Error:   has_empty  = true; break;
						case tabx::XlsxCellKind::Integer: has_int    = true; break;
						case tabx::XlsxCellKind::Float:   has_float  = true; break;
						case tabx::XlsxCellKind::Boolean: has_bool   = true; break;
						case tabx::XlsxCellKind::String:  has_string = true; break;
					}
				}

				// --- Object column (string present, or bool mixed with numeric) ---
				if (has_string || (has_bool && (has_int || has_float)))
				{
					py::list lst;
					for (std::size_t r = 0; r < R; ++r)
					{
						const auto& cell = res.cells[r * C + c];

						switch (cell.kind)
						{
							case tabx::XlsxCellKind::Empty:
							case tabx::XlsxCellKind::Error:
								lst.append(py::none()); break;
							case tabx::XlsxCellKind::Integer:
								lst.append(py::int_(static_cast<long long>(
									static_cast<std::int64_t>(cell.dval)))); break;
							case tabx::XlsxCellKind::Float:
								lst.append(py::float_(cell.dval)); break;
							case tabx::XlsxCellKind::Boolean:
								lst.append(py::bool_(cell.bval)); break;
							case tabx::XlsxCellKind::String:
								lst.append(py::str(cell.sval)); break;
						}
					}
					col_arrays.append(lst);
				}
				// --- Boolean column ---
				else if (has_bool && !has_int && !has_float)
				{
					if (has_empty)
					{
						// NaN-like → Python object list
						py::list lst;

						for (std::size_t r = 0; r < R; ++r)
						{
							const auto& cell = res.cells[r * C + c];

							if (cell.kind == tabx::XlsxCellKind::Boolean)
								lst.append(py::bool_(cell.bval));

							else
								lst.append(py::none());
						}
						col_arrays.append(lst);
					}
					else
					{
						auto* buf = new std::vector<std::uint8_t>(R);

						for (std::size_t r = 0; r < R; ++r)
							(*buf)[r] = res.cells[r * C + c].bval ? 1u : 0u;

						py::capsule owner(buf, [](void* p) {
							delete static_cast<std::vector<std::uint8_t>*>(p);
						});

						col_arrays.append(py::array(
							py::dtype("bool"),
							{static_cast<py::ssize_t>(R)},
							{static_cast<py::ssize_t>(1)},
							buf->data(), owner));
					}
				}
				// --- Numeric column (int64 or float64) ---
				else if ((has_int || has_float) && !has_bool && !has_string)
				{
					if (!has_empty && !has_float)
					{
						// All integers, no nulls → int64
						auto* buf = new std::vector<std::int64_t>(R);

						for (std::size_t r = 0; r < R; ++r)
							(*buf)[r] = static_cast<std::int64_t>(res.cells[r * C + c].dval);

						py::capsule owner(buf, [](void* p) {
							delete static_cast<std::vector<std::int64_t>*>(p);
						});
						py::array_t<std::int64_t> arr(
							{static_cast<py::ssize_t>(R)},
							{static_cast<py::ssize_t>(sizeof(std::int64_t))},
							buf->data(), owner);
						col_arrays.append(arr);
					}
					else
					{
						// float64; integers upcast, empty/error → NaN
						auto* buf = new std::vector<double>(R, kNaN);

						for (std::size_t r = 0; r < R; ++r)
						{
							const auto& cell = res.cells[r * C + c];
							
							if (cell.kind == tabx::XlsxCellKind::Integer ||
							    cell.kind == tabx::XlsxCellKind::Float)
								(*buf)[r] = cell.dval;
						}
						py::capsule owner(buf, [](void* p) {
							delete static_cast<std::vector<double>*>(p);
						});
						py::array_t<double> arr(
							{static_cast<py::ssize_t>(R)},
							{static_cast<py::ssize_t>(sizeof(double))},
							buf->data(), owner);
						col_arrays.append(arr);
					}
				}
				// --- All-empty column ---
				else
				{
					py::list lst;
					for (std::size_t r = 0; r < R; ++r)
						lst.append(py::none());
					col_arrays.append(lst);
				}
			}

			return py::make_tuple(col_arrays, col_names);
		},
		py::arg("file_path"),
		py::arg("sheet_name") = "",
		py::arg("skip_header") = true,
		"Parse an XLSX worksheet into per-column typed arrays.\n\n"
		"Numeric columns become numpy int64 (no nulls) or float64 (with NaN for\n"
		"empty/error cells).  Boolean columns become numpy bool.  String or mixed\n"
		"columns become Python object lists.  Column-type inference mirrors pandas.\n\n"
		"Args:\n"
		"    file_path (str): Path to the .xlsx file.\n"
		"    sheet_name (str): Worksheet to parse; empty string → first sheet.\n"
		"    skip_header (bool): If True, first row becomes column names.\n\n"
		"Returns:\n"
		"    tuple[list, list]: (column_arrays, column_names)."
	);
}
