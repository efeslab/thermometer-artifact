import click
import pandas as pd
import re
from pathlib import Path


pt_traces = [
    "cassandra",
    "clang",
    "drupal",
    "finagle-chirper",
    "finagle-http",
    "kafka",
    "mediawiki",
    "mysqllarge",
    "pgbench",
    "python",
    "tomcat",
    "verilator",
    "wordpress",
]


def to_excel(data_dir: Path, suffix_list: list, output_excel_name: str):
    output_excel_path = data_dir / output_excel_name
    output_excel_file = pd.ExcelWriter(output_excel_path, engine='xlsxwriter')
    for app in pt_traces:
        for suffix in suffix_list:
            input_path = data_dir / f"{app}{suffix}.csv"
            df = pd.read_csv(input_path, index_col=0, header=0)
            df.to_excel(output_excel_file, sheet_name=f"{app}{suffix}"[0:31])
    output_excel_file.save()


def main():
    data_dir = Path("/home/shixinsong/Desktop/UM/SURE/plots/input_generalization/summary")
    # suffix_list = ["", "_speedup"]
    # output_excel_name = "input_generalize_all_speedup.xlsx"
    # to_excel(data_dir, suffix_list, output_excel_name)
    # suffix_list = ["_mpki", "_reduction"]
    # output_excel_name = "input_generalize_all_mpki_reduction.xlsx"
    # to_excel(data_dir, suffix_list, output_excel_name)

    suffix_list = ["_btb_miss_rate", "_btb_miss_rate_reduction"]
    output_excel_name = "input_generalize_all_btb_miss_reduction.xlsx"
    to_excel(data_dir, suffix_list, output_excel_name)


if __name__ == '__main__':
    main()
