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


def cal_percentage_of_opt(line: pd.Series) -> pd.Series:
    result = []
    new_index = list(line.index)
    new_index.remove("OPT")
    # new_index.remove("Thermometer-random")
    for key in new_index:
        result.append(100 * line[key] / line["OPT"])
    return pd.Series(result, index=new_index)


def merge_df(input_dir: Path, all_ways: list, name_map: dict, cal_percentage: bool):
    all_df = {}
    columns = None
    for total_ways in all_ways:
        ipc_speedup_result_path = input_dir / ("ipc_speedup_pt_way%s.csv" % total_ways)
        one_df = pd.read_csv(ipc_speedup_result_path, header=0, index_col=0)
        plot_functions.remove_index_suffix(one_df, "_result")
        one_df = one_df.rename(name_map, axis=1)
        one_df = one_df.drop(columns=["hwc 80 50 curr hotter"])
        if cal_percentage:
            one_df = one_df.apply(cal_percentage_of_opt, axis=1)
        all_df[total_ways] = one_df
        if columns is None:
            columns = one_df.columns
        else:
            assert list(columns) == list(one_df.columns)

    result_df = pd.DataFrame(columns=columns)
    for trace in pt_traces:
        for total_ways in all_ways:
            the_index = trace + ";" + total_ways
            line_df = all_df[total_ways].loc[[trace]]
            result_df = result_df.append(line_df.rename({trace: the_index}))

    print(result_df)
    return result_df


def main():
    name_map = {
        "opt": "OPT",
        "srrip": "SRRIP",
        # "hwc 80 50 curr hotter": "Thermometer-random",
        "hwc 80 50 curr hotter lru": "Thermometer",
    }
    df = merge_df(
        Path("/Users/shixinsong/Desktop/plots/simulation_result_ipc_summary"),
        ["4", "8", "16", "32", "64", "128"],
        name_map=name_map,
        cal_percentage=True,
    )

    print(df)
    df.to_csv("/Users/shixinsong/Desktop/plots/paper_results/way_speedup.csv")

    # TODO: Separate it into three plots, and use % of opt speedup.

    plot_functions.plot_bar_chart(
        output_path=Path(
            "/Users/shixinsong/Desktop/plots/paper_results/way_speedup.pdf"
        ),
        df=df,
        ytitle=r"{\huge\% of optimal policy speedup}",
        two_level=True,
        add_average=False,
        figsize=(0.4, 0.9),
        second_level_y_pos=-12,
        legend_col_num=1,
        # cut_upper_bound=20,
        # cut_lower_bound=-5,
    )


if __name__ == "__main__":
    main()
