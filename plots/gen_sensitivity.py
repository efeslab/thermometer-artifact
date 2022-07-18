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


def to_excel(data_dir: Path, way_list: list, size_list: list, filename_prefix: str, output_excel_name: str):
    output_excel_path = data_dir / output_excel_name
    output_excel_file = pd.ExcelWriter(output_excel_path, engine='xlsxwriter')
    for way in way_list:
        for size in size_list:
            input_path = data_dir / f"{filename_prefix}_use_default_btb_record_{way}_{size}.csv"
            df = pd.read_csv(input_path, index_col=0, header=0)
            df.to_excel(output_excel_file, sheet_name=f"{filename_prefix}_train_{way}_{size}")
    output_excel_file.save()


def main():
    big_dir = Path("/home/shixinsong/Desktop/UM/SURE/plots/paper_results")
    way_dir = big_dir / "btb_way"
    size_dir = big_dir / "btb_size"
    way_list = [4, 8, 16, 32, 64, 128]
    size_list = [1, 2, 4, 8, 16, 32]

    to_excel(
        data_dir=way_dir,
        way_list=way_list,
        size_list=[8],
        filename_prefix="ipc_speedup_way",
        output_excel_name="ipc_speedup_way.xlsx"
    )

    to_excel(
        data_dir=size_dir,
        way_list=[4],
        size_list=size_list,
        filename_prefix="ipc_speedup_size",
        output_excel_name="ipc_speedup_size.xlsx"
    )


if __name__ == '__main__':
    main()
