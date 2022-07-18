import asyncio
import enum
import math
import pathlib
from functools import wraps
from pathlib import Path
import re
import statistics

import click
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib import colors, rc
from matplotlib.ticker import PercentFormatter
from matplotlib.figure import figaspect
# from tqdm import tqdm
from typing import Callable, Tuple, Optional
import scipy.stats as ss

import plot_functions
from .utils import scripts_dir, plots_dir, project_dir, pt_traces


existing_app = ["cassandra", "drupal", "finagle-chirper", "finagle-http", "kafka", "mediawiki", "tomcat", "verilator", "wordpress"]
# new_app = ["mysql", "nginx", "postgres"]
new_app = ["clang", "pgbench", "python", "mysqllarge"]


def parse_one_file(input_path: Path) -> float:
    with input_path.open(mode="r") as input_file:
        s = input_file.read()
        # Get IPC
        ipc_str = re.search("Eviction accuracy:.*\n", s).group(0)
        ipc_arr = ipc_str.split(" ")
        return float(ipc_arr[2]) * 100


def parse_one_program(input_dir: Path, col_name: str, app_list: list) -> pd.DataFrame:
    index = []
    result = []
    for input_filename in app_list:
        index.append(f"{input_filename}_result")
        input_file = input_dir / f"{input_filename}_result.txt"
        result.append([parse_one_file(input_file)])
    return pd.DataFrame(result, columns=[col_name], index=index)


def parse_all_program(
    big_input_dir: Path, name_map: dict, app_list: list
) -> pd.DataFrame:
    total_ways = 4
    cols = []
    index_col = None
    for name in name_map:
        input_dir = big_input_dir / ("%s%s_result" % (name, total_ways))
        df = parse_one_program(input_dir, name_map[name], app_list)
        print(df)
        if index_col is None:
            index_col = df.index
        else:
            assert list(index_col) == list(df.index)
        cols.append(df)

    result_df = pd.concat(cols, axis=1)
    print(result_df)
    # result_df.to_csv(output_dir / "accuracy.csv")
    return result_df


def main():
    input_dir = plots_dir / "pt_result"
    output_dir = plots_dir / "paper_results/accuracy"
    output_dir.mkdir(exist_ok=True, parents=True)
    name_map = {
        "ChampSim_fdip_lru": "Transient",
        "ChampSim_fdip_hwc_50_80_f_keep_curr_hotter": "Holistic",
        "ChampSim_fdip_hwc_50_80_f_keep_curr_hotter_lru": "Thermometer",
    }
    result_df = parse_all_program(input_dir, name_map, new_app)
    existing_df = pd.read_csv("paper_results/accuracy/accuracy.csv", header=0, index_col=0)
    result_df = existing_df.append(result_df)
    print(result_df)
    result_df.sort_index(inplace=True)
    result_df.to_csv(output_dir / "accuracy.csv")
    # output_dir = Path("/Users/shixinsong/Desktop/plots/paper_results/accuracy")
    # result_df = pd.read_csv(output_dir / "accuracy.csv", index_col=0, header=0)
    # result_df = result_df.rename({"Thermometer-LRU": "Overall"}, axis=1)
    plot_functions.remove_index_suffix(result_df, "_result")
    rename_map = {"pgbench": "postgresql", "mysqllarge": "mysql"}
    result_df.rename(index=rename_map, inplace=True)
    # result_df = result_df.drop(["verilator"])
    plot_functions.plot_bar_chart(
        output_path=output_dir / "accuracy.pdf",
        df=result_df,
        ytitle=r"Accuracy (\%)",
        two_level=False,
        add_average=True,
        cut_upper_bound=105,
        figsize=(0.32, 0.9),
        legend_col_num=4,
    )


if __name__ == "__main__":
    main()
