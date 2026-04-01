"""Demonstrate basic usage of the csvpp module.

This script shows all public API variants:

- single-row parsing and summation,
- multi-row parsing and summation with optional header skipping,
- direct CSV → DataFrame loading.

Install the package first, then run:

    pip install -e .
    python examples/example.py
"""

try:
    import csvpp

except ImportError:
    raise SystemExit("csvpp is not installed.\nRun: pip install -e .")

# Single-row API:
numbers = csvpp.parse_csv_numbers("10, 20, 30")
total = csvpp.sum_csv_numbers("10, 20, 30")

print(f"parse_csv_numbers('10, 20, 30') -> {numbers}")
print(f"sum_csv_numbers  ('10, 20, 30') -> {total}")

# Multi-row API:
CSV = "value\n100\n200\n300\n400"

flat = csvpp.parse_csv_flat(CSV, skip_header=True)
grand = csvpp.sum_csv_all(CSV, skip_header=True)

print(f"\nparse_csv_flat(skip_header=True) -> {flat}")
print(f"sum_csv_all   (skip_header=True) -> {grand}")

# DataFrame API:
MULTI_CSV = "id,qty,price\n1,10,100\n2,20,200\n3,30,300"

df = csvpp.parse_csv_dataframe(MULTI_CSV)
print(f"\nparse_csv_dataframe:\n{df}")
print(f"dtypes: {df.dtypes.to_dict()}")
