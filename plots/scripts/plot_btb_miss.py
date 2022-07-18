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


def cal_btb_miss_reduction(line: pd.Series) -> pd.Series:
    # keys = ["SRRIP", "GHRP", "Hawkeye", "Thermometer-random", "Thermometer-LRU", "OPT"]
    result = []
    new_index = list(line.index)
    new_index.remove("LRU")
    for key in new_index:
        result.append(100 * (line["LRU"] - line[key]) / line["LRU"])
    return pd.Series(result, index=new_index)


def parse_one_file(input_path: Path) -> float:
    with input_path.open(mode="r") as input_file:
        s = input_file.read()
        # Get IPC
        ipc_str = re.search("BTB miss rate:.*\n", s).group(0)
        ipc_arr = ipc_str.split(" ")
        return float(ipc_arr[3]) * 100


def parse_one_program(input_dir: Path, col_name: str) -> pd.DataFrame:
    index = []
    result = []
    for short_index in pt_traces:
        index.append(f"{short_index}_result")
        input_file = input_dir / f"{short_index}_result.txt"
    # for input_file in sorted(input_dir.iterdir()):
    #     index.append(input_file.stem)
        result.append([parse_one_file(input_file)])
    return pd.DataFrame(result, columns=[col_name], index=index)


def parse_all_program(
    big_input_dir: Path, output_dir: Path, name_map: dict, prefix: str = ""
) -> pd.DataFrame:
    total_ways = 4
    cols = []
    index_col = None
    for name in name_map:
        input_dir = big_input_dir / ("%s%s_result" % (name, total_ways))
        df = parse_one_program(input_dir, name_map[name])
        print(df)
        if index_col is None:
            index_col = df.index
        else:
            assert list(index_col) == list(df.index)
        cols.append(df)

    result_df = pd.concat(cols, axis=1)
    print(result_df)
    result_df.to_csv(output_dir / "btb_miss.csv")

    reduction_df = result_df.apply(cal_btb_miss_reduction, axis=1)
    reduction_df.to_csv(output_dir / f"{prefix}btb_miss_reduction.csv")
    return reduction_df


@click.command()
@click.option(
    "--local",
    default=False,
    type=bool,
)
def main(local: bool):
    input_dir = plots_dir / "pt_result"
    output_dir = plots_dir / "paper_results/btb_miss"
    if not local:
        output_dir.mkdir(exist_ok=True, parents=True)
        name_map = {
            "ChampSim_fdip_lru": "LRU",
            "ChampSim_fdip_srrip": "SRRIP",
            "ChampSim_fdip_ghrp": "GHRP",
            "ChampSim_fdip_hawkeye": "Hawkeye",
            "ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru": "Thermometer",
            "ChampSim_fdip_opt": "OPT",
        }
        result_df = parse_all_program(input_dir, output_dir, name_map)
    else:
        result_df = pd.read_csv(output_dir / "btb_miss_reduction.csv", header=0, index_col=0)

    plot_functions.remove_index_suffix(result_df, "_result")
    rename_map = {"pgbench": "postgresql", "mysqllarge": "mysql"}
    result_df.rename(index=rename_map, inplace=True)
    # result_df = result_df.drop(["verilator"])
    plot_functions.plot_bar_chart(
        output_path=output_dir / "btb_miss_reduction.pdf",
        df=result_df,
        ytitle=r"BTB miss reduction (\%)",
        two_level=False,
        add_average=True,
        # cut_upper_bound=20,
        figsize=(0.35, 0.9),
        legend_col_num=7,
        cut_upper_bound=90,
        legend_size=19,
    )


if __name__ == "__main__":
    main()
