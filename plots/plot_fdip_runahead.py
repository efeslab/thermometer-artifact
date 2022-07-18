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


pt_traces = [
    "cassandra",
    "drupal",
    # "finagle-chirper",
    # "finagle-http",
    # "kafka",
    # "mediawiki",
    "tomcat",
    # "verilator",
    # "wordpress",
]


key_result_name_map = {
    "ChampSim_fdip%s_lru": "LRU",
    "ChampSim_fdip%s_srrip": "SRRIP",
    "ChampSim_fdip%s_hot_warm_cold_80_50_f_keep_curr_hotter_lru": "Thermometer",
    "ChampSim_fdip%s_opt": "OPT",
}


def parse_one_file(input_path: Path) -> float:
    with input_path.open(mode="r") as input_file:
        s = input_file.read()
        # Get IPC
        ipc_str = re.search("CPU 0 cumulative IPC.*\n", s).group(0)
        ipc_arr = ipc_str.split(" ")
        return float(ipc_arr[4])


def get_dir_name(program_name: str, btb_size: int, total_ways: int, run_ahead: str):
    prefix = ("%sK_" % btb_size) if btb_size != 8 else ""
    return prefix + (program_name % run_ahead) + str(total_ways) + "_result"


def cal_ipc_speedup(line: pd.Series) -> pd.Series:
    # keys = ["SRRIP", "GHRP", "Hawkeye", "Thermometer-random", "Thermometer-LRU", "OPT"]
    result = []
    new_index = list(line.index)
    new_index.remove("LRU")
    for key in new_index:
        result.append(100 * (line[key] - line["LRU"]) / line["LRU"])
    return pd.Series(result, index=new_index)


def cal_percentage_of_opt(line: pd.Series) -> pd.Series:
    result = []
    new_index = list(line.index)
    new_index.remove("OPT")
    # new_index.remove("Thermometer-random")
    for key in new_index:
        result.append(100 * line[key] / line["OPT"])
    return pd.Series(result, index=new_index)


def generate_ipc_table(big_input_dir: Path, output_path: Path, name_map: dict):
    total_ways = 4
    btb_size = 8
    df = pd.DataFrame(columns=list(name_map.values()))
    for pt_trace in pt_traces:
        for run_ahead in ["64", "128", "192", "256"]:
            values = []
            columns = []
            name_ahead = run_ahead if run_ahead != "192" else ""
            for program_name, column_name in name_map.items():
                input_file = (
                    big_input_dir
                    / get_dir_name(program_name, btb_size, total_ways, name_ahead)
                    / (pt_trace + "_result.txt")
                )
                ipc = parse_one_file(input_file)
                values.append(ipc)
                columns.append(column_name)
            df = df.append(
                pd.DataFrame(
                    data=[values],
                    index=["%s;%s" % (pt_trace, run_ahead)],
                    columns=columns,
                )
            )
    df.to_csv(output_path)
    return df


@click.command()
@click.option(
    "--local",
    default=False,
    type=bool,
)
def main(local: bool):
    if not local:
        big_input_dir = Path("/mnt/storage/shixins/champsim_pt/pt_result")
        output_dir = Path("/mnt/storage/shixins/champsim_pt/paper_results/fdip_runahead")
        output_dir.mkdir(exist_ok=True)
        ipc_result_df = generate_ipc_table(
            big_input_dir, output_dir / "ipc_result.csv", key_result_name_map
        )
        print(ipc_result_df)
        ipc_speedup_df = ipc_result_df.apply(cal_ipc_speedup, axis=1)
        print(ipc_speedup_df)
        opt_percentage_df = ipc_speedup_df.apply(cal_percentage_of_opt, axis=1)
        print(opt_percentage_df)
        opt_percentage_df.to_csv(output_dir / "percentage_opt_ipc_speedup_runahead.csv")

        plot_functions.plot_bar_chart(
            output_path=output_dir / "ipc_speedup_runahead.pdf",
            df=ipc_speedup_df,
            ytitle=r"IPC speedup over FDIP baseline with LRU (\%)",
            two_level=True,
            add_average=False,
            figsize=(0.3, 0.9),
            second_level_y_pos=-25,
        )
    else:
        output_dir = Path("/Users/shixinsong/Desktop/plots/paper_results/fdip_runahead")
        opt_percentage_df = pd.read_csv(output_dir / "percentage_opt_ipc_speedup_runahead.csv",
                                        header=0, index_col=0)

    plot_functions.plot_bar_chart(
        output_path=output_dir / "percentage_opt_ipc_speedup_runahead.pdf",
        df=opt_percentage_df,
        ytitle=r"{\huge\% of optimal policy speedup}",
        two_level=True,
        add_average=False,
        figsize=(0.4, 0.9),
        second_level_y_pos=-14,
        legend_col_num=1
    )


if __name__ == "__main__":
    main()
