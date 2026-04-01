#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <cstdint>

#include "parser.h"

namespace py = pybind11;

PYBIND11_MODULE(_core, module)
{
	module.doc() =
		"Native C++17 CSV integer parser exposed via pybind11.\n\n"
		"All parsing functions accept only integer fields.  Whitespace\n"
		"surrounding each field is ignored.  Non-integer fields raise\n"
		"ValueError at runtime.";

	module.def(
		"parse_csv_numbers",
		&fast_parser::parse_csv_numbers,
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
		&fast_parser::sum_csv_numbers,
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
		&fast_parser::parse_csv_flat,
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
		&fast_parser::sum_csv_all,
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

			const fast_parser::CsvShape shape = fast_parser::parse_csv_into_buffer(
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
	);}
