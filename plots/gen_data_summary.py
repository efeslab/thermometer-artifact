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

line_names = list(map(lambda x: f"{x}_result", pt_traces))


def to_excel(input_path: Path, output_excel_file, keep_app_only: bool = False):
    df = pd.read_csv(input_path, index_col=0, header=0)
    if keep_app_only:
        df = df.loc[line_names]
    df.to_excel(output_excel_file, sheet_name=input_path.stem[0:31])


def main():
    big_dir = Path("/home/shixinsong/Desktop/UM/SURE/plots/paper_results")
    output_excel_path = big_dir / "all_plot_data.xlsx"
    ipc_dir = big_dir / "ipc"


    output_excel_file = pd.ExcelWriter(output_excel_path, engine="xlsxwriter")

    prefix_list = [
        "twig_prefetch_",
        "",
        "motivation_",
        "num_cate_",
        "existing_",
        "very_first_",
    ]

    for dir_name in prefix_list:
        to_excel(ipc_dir / f"{dir_name}ipc_speedup_result_pt.csv", output_excel_file, True)

    to_excel(big_dir / "btb_miss" / "btb_miss_reduction.csv", output_excel_file)

    to_excel(Path("/home/shixinsong/Desktop/UM/SURE/plots/rebuttal/simulate_time.csv"), output_excel_file)

    to_excel(big_dir / "accuracy" / "accuracy.csv", output_excel_file)

    to_excel(big_dir / "coverage" / "coverage.csv", output_excel_file)

    output_excel_file.save()


if __name__ == '__main__':
    main()

