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
# import scipy.stats as ss

import plot_functions

from .utils import scripts_dir, plots_dir, project_dir, pt_traces

existing_name_map = {
    "ChampSim_fdip_lru": "LRU",
    "ChampSim_fdip_srrip": "SRRIP",
    "ChampSim_fdip_ghrp": "GHRP",
    "ChampSim_fdip_hawkeye": "Hawkeye",
    "ChampSim_fdip_opt": "OPT",
}

key_result_name_map = {
    "ChampSim_fdip_lru": "LRU",
    "ChampSim_fdip_srrip": "SRRIP",
    "ChampSim_fdip_ghrp": "GHRP",
    "ChampSim_fdip_hawkeye": "Hawkeye",
    "ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru": "Thermometer",
    "7979ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru": "Thermometer-7979-entry",
    "ChampSim_fdip_opt": "OPT",
}

twig_prefetch_result_name_map = {
    "ChampSim_fdip_lru_twig": "LRU",
    "ChampSim_fdip_srrip_twig": "SRRIP",
    "ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru_twig": "Thermometer",
    "ChampSim_fdip_opt_twig": "OPT",
}

motivation_name_map = {
    "ChampSim_fdip_lru": "LRU",
    "ChampSim_fdip_predecoder_btb_lru": "Confluence-LRU",
    "ChampSim_fdip_shotgun_lru": "Shotgun-LRU",
    "ChampSim_fdip_opt": "OPT",
    "ChampSim_fdip_predecoder_btb_opt": "Confluence-OPT",
    "ChampSim_fdip_shotgun_opt": "Shotgun-OPT",
    "ChampSim_fdip_perfect_btb_champsim": "Perfect-BTB",
}

num_cate_name_map = {
    "ChampSim_fdip_lru": "LRU",
    "ChampSim_fdip_hwc_80_f_keep_curr_hotter_lru": "2",
    "ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru": "3",
    "ChampSim_fdip_hwc_50_65_80_f_keep_curr_hotter_lru": "4",
    "ChampSim_fdip_hwc_8_cate_f_keep_curr_hotter_lru": "8",
    "ChampSim_fdip_hwc_16_cate_f_keep_curr_hotter_lru": "16",
    "ChampSim_fdip_opt": "OPT",
}


very_first_name_map = {
    "ChampSim_fdip_lru": "LRU",
    "ChampSim_fdip_perfect_btb_champsim": "Perfect-BTB",
    "ChampSim_fdip_perfect_bp": "Perfect-BP",
    "ChampSim_icache_lru": "Perfect-I-Cache",
}


def cal_ipc_speedup(line: pd.Series) -> pd.Series:
    # keys = ["SRRIP", "GHRP", "Hawkeye", "Thermometer-random", "Thermometer-LRU", "OPT"]
    result = []
    new_index = list(line.index)
    new_index.remove("LRU")
    for key in new_index:
        result.append(100 * (line[key] - line["LRU"]) / line["LRU"])
    return pd.Series(result, index=new_index)


def cal_branch_mpki_reduction(line: pd.Series) -> pd.Series:
    # keys = ["SRRIP", "GHRP", "Hawkeye", "Thermometer-random", "Thermometer-LRU", "OPT"]
    result = []
    new_index = list(line.index)
    new_index.remove("LRU")
    for key in new_index:
        result.append(100 * (line["LRU"] - line[key]) / line["LRU"])
    return pd.Series(result, index=new_index)


def cal_percentage_of_opt(line: pd.Series) -> pd.Series:
    result = []
    new_index = list(line.index)
    new_index.remove("OPT")
    # new_index.remove("Thermometer-random")
    for key in new_index:
        result.append(100 * line[key] / line["OPT"])
    return pd.Series(result, index=new_index)


def parse_result_csv(
    input_dir: Path,
    output_dir: Path,
    name_map: dict,
    name_prefix: str = "",
):
    total_ways = 4
    index_col = None
    ipc_cols = []
    mpki_cols = []
    for name in name_map.keys():
        file = input_dir / ("%s%s.csv" % (name, total_ways))
        print(file)
        df = pd.read_csv(file, header=0, index_col=0)
        if index_col is None:
            index_col = df.index
        ipc_df = df[["IPC"]].copy().rename(columns={"IPC": name_map[name]})
        ipc_cols.append(ipc_df)
        mpki_df = (
            df[["Branch MPKI"]].copy().rename(columns={"Branch MPKI": name_map[name]})
        )
        mpki_cols.append(mpki_df)

    ipc_dir = output_dir / "ipc"
    ipc_dir.mkdir(exist_ok=True)
    ipc_result_df = pd.concat(ipc_cols, axis=1)
    ipc_result_df.to_csv(ipc_dir / (name_prefix + "ipc_result_pt.csv"))
    print(ipc_result_df)
    ipc_speedup_df = ipc_result_df.apply(cal_ipc_speedup, axis=1)
    ipc_speedup_df.to_csv(ipc_dir / (name_prefix + "ipc_speedup_result_pt.csv"))
    print(ipc_speedup_df)

    mpki_result_df = pd.concat(mpki_cols, axis=1)
    branch_mpki_dir = output_dir / "branch_mpki"
    branch_mpki_dir.mkdir(exist_ok=True)
    mpki_result_df.to_csv(branch_mpki_dir / (name_prefix + "branch_mpki_result_pt.csv"))
    print(mpki_result_df)
    mpki_reduction_df = mpki_result_df.apply(cal_branch_mpki_reduction, axis=1)
    mpki_reduction_df.to_csv(
        branch_mpki_dir / (name_prefix + "branch_mpki_reduction_result_pt.csv")
    )
    print(mpki_reduction_df)

    return ipc_speedup_df, mpki_reduction_df


def generate_and_plot(
    name_map: dict,
    prefix: str,
    input_dir: Path,
    output_dir: Path,
    upper_bound: float = 20,
    lower_bound: float = 0,
    figsize: tuple = (0.33, 0.9),
    legend_col_num: int = 4,
    only_opt_percentage: bool = False,
    two_column: float = 1,
    extra_label_size: int = 10,
    legend_size: int = 20,
    xlabel_rotation: int=35,
    show_legend: bool = True,
    add_avg_without_verilator: bool = False,
    add_avg: bool = True,
    app_list=None,
    rename_map=None,
):
    if app_list is None:
        app_list = pt_traces
    if rename_map is None:
        rename_map = {"pgbench": "postgresql", "mysqllarge": "mysql"}
    ipc_speedup_df, mpki_reduction_df = parse_result_csv(
        input_dir, output_dir, name_map, name_prefix=prefix
    )
    plot_functions.remove_index_suffix(ipc_speedup_df, "_result")

    ipc_speedup_df = ipc_speedup_df.loc[app_list]
    mpki_reduction_df = ipc_speedup_df.loc[app_list]

    if only_opt_percentage:

        percentage_opt_df = ipc_speedup_df.apply(cal_percentage_of_opt, axis=1)
        percentage_opt_df.rename(index=rename_map, inplace=True)
        percentage_opt_df = percentage_opt_df.loc[["cassandra", "drupal", "tomcat"]]


        plot_functions.plot_bar_chart(
            output_path=output_dir / "ipc" / (prefix + "percentage_opt_pt.pdf"),
            df=percentage_opt_df,
            ytitle=r"{\huge\% of optimal policy speedup}",
            two_level=False,
            add_average=add_avg,
            figsize=figsize,
            legend_col_num=5,
            xlabel_rotation=0,
            cut_upper_bound=80,
            add_avg_without_verilator=add_avg_without_verilator,
        )
        return

    ipc_speedup_df.rename(index=rename_map, inplace=True)
    plot_functions.plot_bar_chart(
        output_path=output_dir / "ipc" / (prefix + "ipc_speedup_pt.pdf"),
        df=ipc_speedup_df,
        ytitle=r"Speedup (\%)",
        two_level=False,
        add_average=add_avg,
        cut_upper_bound=upper_bound,
        cut_lower_bound=lower_bound,
        figsize=figsize,
        legend_col_num=legend_col_num,
        two_column=two_column,
        extra_label_size=extra_label_size,
        legend_size=legend_size,
        xlabel_rotation=xlabel_rotation,
        show_legend=show_legend,
        add_avg_without_verilator=add_avg_without_verilator,
    )

    plot_functions.remove_index_suffix(mpki_reduction_df, "_result")
    mpki_reduction_df.rename(index=rename_map, inplace=True)
    plot_functions.plot_bar_chart(
        output_path=output_dir
        / "branch_mpki"
        / (prefix + "branch_mpki_reduction_pt.pdf"),
        df=mpki_reduction_df,
        ytitle=r"Branch-MPKI Reduction over FDIP with LRU(\%)",
        two_level=False,
        add_average=add_avg,
        cut_upper_bound=upper_bound,
        figsize=figsize,
        legend_col_num=legend_col_num,
        add_avg_without_verilator=add_avg_without_verilator,
    )


def main():
    input_dir = plots_dir / "simulation_result/pt_result_analysis"
    output_dir = plots_dir / "paper_results"
    output_dir.mkdir(exist_ok=True)

    # Fig. 1
    generate_and_plot(
        existing_name_map,
        "existing_",
        input_dir,
        output_dir,
        upper_bound=12.5,
        lower_bound=-3,
        figsize=(0.35, 0.9),
        two_column=1.2,
        legend_col_num=4,
        extra_label_size=15
    )

    # Fig. 2
    generate_and_plot(
        very_first_name_map,
        "very_first_",
        input_dir,
        output_dir,
        upper_bound=75,
        legend_col_num=6,
        extra_label_size=14,
        figsize=(0.35, 0.9),
        legend_size=16
    )

    # Fig. 4
    generate_and_plot(
        motivation_name_map,
        "motivation_",
        input_dir,
        output_dir,
        upper_bound=30,
        lower_bound=-40,
        legend_col_num=3,
        legend_size=16,
        figsize=(0.35, 0.9),
        two_column=1.2,
        extra_label_size=9,
    )

    # Fig. 11
    generate_and_plot(
        key_result_name_map,
        "",
        input_dir,
        output_dir,
        upper_bound=15.5,
        lower_bound=-3,
        two_column=1.4,
        legend_col_num=3,
        extra_label_size=14,
        figsize=(0.25, 0.9),
        xlabel_rotation=30,
        add_avg_without_verilator=True,
    )

    # Fig. 21
    # generate_and_plot(
    #     twig_prefetch_result_name_map,
    #     "twig_prefetch_",
    #     input_dir,
    #     output_dir,
    #     upper_bound=5,
    #     lower_bound=-3,
    #     # two_column=1.2,
    #     legend_col_num=3,
    #     extra_label_size=15,
    #     figsize=(0.35, 0.9),
    #     # xlabel_rotation=20,
    #     add_avg_without_verilator=True,
    # )

    # # of Temperature categories
    # generate_and_plot(
    #     num_cate_name_map,
    #     "num_cate_",
    #     input_dir,
    #     output_dir,
    #     upper_bound=15,
    #     only_opt_percentage=True,
    #     legend_col_num=3,
    #     figsize=(0.35, 0.9),
    #     two_column=2,
    #     legend_size=20,
    #     add_avg=False
    # )


if __name__ == "__main__":
    main()
