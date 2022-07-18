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
from tqdm import tqdm
from typing import Callable, Tuple, Optional
import scipy.stats as ss

import plot_functions


def parse_one_file(input_path: Path) -> float:
    with input_path.open(mode="r") as input_file:
        s = input_file.read()
        # Get IPC
        ipc_str = re.search("CPU 0 cumulative IPC.*\n", s).group(0)
        ipc_arr = ipc_str.split(" ")
        return float(ipc_arr[4])


def cal_ipc_speedup(line: pd.Series) -> pd.Series:
    # keys = ["SRRIP", "GHRP", "Hawkeye", "Thermometer-random", "Thermometer-LRU", "OPT"]
    result = []
    new_index = list(line.index)
    new_index.remove("LRU")
    for key in new_index:
        result.append(100 * (line[key] - line["LRU"]) / line["LRU"])
    return pd.Series(result, index=new_index)


def generate_csv(
    big_input_dir: Path, output_dir: Path, name_map: dict, file_list: list
):
    df = pd.DataFrame(columns=name_map.values())
    print(name_map.keys())
    for file in file_list:
        file_index = file.stem.split(".")[0]
        ipcs = []
        for dir_name in name_map.keys():
            input_path = big_input_dir / ("ipc1_fdip_%s4_result" % dir_name) / file.name
            ipcs.append(parse_one_file(input_path))
        print(ipcs)
        df = df.append(
            pd.DataFrame([ipcs], columns=name_map.values(), index=[file_index])
        )
    print(df)
    df.to_csv(output_dir / "ipc_ipc1.csv")
    df = df.apply(cal_ipc_speedup, axis=1)
    df = df[df["OPT"] > 3]
    print(df)
    fix_latex(df)
    print(df)
    df.to_csv(output_dir / "ipc_speedup_ipc1.csv")
    return df


def fix_latex(df: pd.DataFrame):
    new_index = []
    for row in df.index:
        row = row.replace("_", "-")
        new_index.append(row)
    df.index = pd.Index(new_index)


@click.command()
@click.option(
    "--local",
    default=False,
    type=bool,
)
def main(local: bool):
    if not local:
        key_result_name_map = {
            "ChampSim_fdip_lru": "LRU",
            "ChampSim_fdip_srrip": "SRRIP",
            "ChampSim_fdip_ghrp": "GHRP",
            "ChampSim_fdip_hawkeye": "Hawkeye",
            # "ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter": "Thermometer-random",
            "ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru": "Thermometer",
            "ChampSim_fdip_opt": "OPT",
        }
        input_big_dir = Path("/mnt/storage/shixins/champsim_pt/correct_new_btb_fdip_result")
        output_dir = Path("/mnt/storage/shixins/champsim_pt/paper_results/ipc")
        df = generate_csv(
            input_big_dir,
            output_dir,
            key_result_name_map,
            sorted(
                (
                    input_big_dir
                    / "ipc1_fdip_ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter4_result/"
                ).iterdir()
            ),
        )
    else:
        output_dir = Path("/Users/shixinsong/Desktop/plots/paper_results/ipc")
        df = pd.read_csv(output_dir / "ipc_speedup_ipc1.csv", header=0, index_col=0)

    plot_functions.plot_bar_chart(
        output_path=output_dir / "ipc_speedup_ipc1.pdf",
        df=df,
        ytitle=r"Speedup (\%)",
        two_level=False,
        add_average=True,
        cut_upper_bound=8.5,
        figsize=(0.35, 0.9),
        legend_size=17,
        legend_col_num=2,
    )


if __name__ == "__main__":
    main()
