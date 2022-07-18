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
from matplotlib import colors
from matplotlib.ticker import PercentFormatter
from tqdm import tqdm
from typing import Callable

trace_of_app_order = {}


def judge_key_order(key_order: list, app: str):
    # print("app", app)
    # print("key order", key_order)
    global trace_of_app_order
    if app in trace_of_app_order:
        assert trace_of_app_order[app] == key_order
    else:
        trace_of_app_order[app] = key_order


def parse_one_file(input_path: Path) -> float:
    with input_path.open(mode="r") as input_file:
        s = input_file.read()
        # Get IPC
        ipc_str = re.search("CPU 0 cumulative IPC.*\n", s).group(0)
        ipc_arr = ipc_str.split(" ")
        return float(ipc_arr[4])


def parse_first_pass_one_app(input_dir: Path) -> list:
    result = []
    key_order = []
    for input_path in sorted(
        input_dir.iterdir(), key=lambda x: int(x.stem.split("_")[0])
    ):
        trace_file, _ = input_path.stem.split("_")
        key_order.append(trace_file)
        result.append(parse_one_file(input_path))

    judge_key_order(key_order, input_dir.name)

    return result


def parse_second_pass_one_app(input_dir: Path) -> list:
    all_ipc = {}
    for input_path in sorted(input_dir.iterdir()):
        trace_file, train_file = input_path.stem.split("_")
        if trace_file not in all_ipc:
            all_ipc[trace_file] = {}
        all_ipc[trace_file][train_file] = parse_one_file(input_path)

    best_set = []
    for trace_file, train_collection in all_ipc.items():
        best_set.append(max(train_collection, key=train_collection.get))

    # print(best_set)
    final_train_file = statistics.mode(best_set)
    # print(final_train_file)

    result = []

    for trace_file in sorted(all_ipc.keys(), key=lambda x: int(x)):
        result.append(all_ipc[trace_file][final_train_file])

    judge_key_order(sorted(all_ipc.keys(), key=lambda x: int(x)), input_dir.name)

    # print(result)
    return result


def parse_all_app(
    large_input_dir: Path, parse_one_app: Callable[[Path], list]
) -> pd.Series:
    columns = list(map(lambda x: str(x), range(10)))
    df = pd.DataFrame(columns=columns)
    for input_dir in sorted(large_input_dir.iterdir()):
        one_app_result = parse_one_app(input_dir)
        df = df.append(
            pd.DataFrame([one_app_result], columns=columns, index=[input_dir.name])
        )

    print(df)
    return df


def single_train_generalize(pt_ignore_outlier: bool):
    baseline_df = parse_all_app(
        Path(
            "/mnt/storage/shixins/champsim_pt/input_generalization/simulation_output/ChampSim_fdip_opt_generate"
        ),
        parse_first_pass_one_app,
    )
    hwc_df = parse_all_app(
        Path(
            "/mnt/storage/shixins/champsim_pt/input_generalization/simulation_output/ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru"
        ),
        parse_second_pass_one_app,
    )
    opt_df = parse_all_app(
        Path(
            "/mnt/storage/shixins/champsim_pt/input_generalization/simulation_output/ChampSim_fdip_opt"
        ),
        parse_first_pass_one_app,
    )

    hwc_speedup_df = ((hwc_df / baseline_df) - 1) * 100
    print(hwc_speedup_df.mean(axis=1))

    if pt_ignore_outlier:
        hwc_speedup_df = hwc_speedup_df.drop(["verilator"])

    plt.figure(figsize=(10, 6))
    ax = plt.gca()
    hwc_speedup_df.plot(
        y=list(map(lambda x: str(x), range(10))),
        ylabel="IPC speedup (%)",
        kind="bar",
        ax=ax,
    )
    ax.set_axisbelow(True)
    plt.grid(axis="y")
    ax.spines["bottom"].set_position("zero")
    for pos in ["top", "right", "left"]:
        ax.spines[pos].set_visible(False)
    plt.xticks(rotation=45)
    ignore_outlier_str = "_no_outlier" if pt_ignore_outlier else ""
    plt.savefig(
        "/mnt/storage/shixins/champsim_pt/input_generalization/plots/hwc_speedup%s.pdf"
        % ignore_outlier_str
    )
    opt_speedup_df = ((opt_df / baseline_df) - 1) * 100
    # print(hwc_df / baseline_df)
    # print(opt_df / baseline_df)
    # print(((hwc_df / baseline_df) - 1) * 100)


def speedup(ipc: float, baseline_ipc: float) -> float:
    return 100 * (ipc - baseline_ipc) / baseline_ipc


def three_best_input_comparison(
    baseline_input_dir: Path, hwc_input_dir: Path, opt_input_dir: Path, no_zero: bool
):
    # Get IPCs for HWC
    hwc_all_ipc = {}
    for input_path in hwc_input_dir.iterdir():
        trace_file, train_file = input_path.stem.split("_")
        if trace_file not in hwc_all_ipc:
            hwc_all_ipc[trace_file] = {}
        hwc_all_ipc[trace_file][train_file] = parse_one_file(input_path)

    # Get IPCs for baseline
    baseline_all_ipc = {}
    for input_path in baseline_input_dir.iterdir():
        trace_file, train_file = input_path.stem.split("_")
        assert trace_file == train_file
        baseline_all_ipc[trace_file] = parse_one_file(input_path)

    # Get IPCs for opt
    opt_all_ipc = {}
    for input_path in opt_input_dir.iterdir():
        trace_file, train_file = input_path.stem.split("_")
        assert trace_file == train_file
        opt_all_ipc[trace_file] = parse_one_file(input_path)

    # Get speedup
    opt_all_speedup = {}
    for trace_file, ipc in opt_all_ipc.items():
        opt_all_speedup[trace_file] = speedup(ipc, baseline_all_ipc[trace_file])

    hwc_all_speedup = {}
    for trace_file in hwc_all_ipc.keys():
        baseline_ipc = baseline_all_ipc[trace_file]
        trace_trace_speedup = speedup(hwc_all_ipc[trace_file][trace_file], baseline_ipc)
        if trace_trace_speedup == 0:
            print(hwc_input_dir.name, trace_file)
        for train_file in hwc_all_ipc[trace_file].keys():
            if train_file == trace_file:
                continue
            trace_train_speedup = speedup(
                hwc_all_ipc[trace_file][train_file], baseline_ipc
            )
            hwc_all_speedup[(trace_file, train_file)] = (
                trace_trace_speedup,
                trace_train_speedup,
            )

    if no_zero:
        sorted_result = sorted(
            hwc_all_speedup.items(),
            key=lambda x: -float("inf")
            if x[1][0] <= 0
            else (x[1][1] - x[1][0]) / abs(x[1][0]),
            reverse=True,
        )
    else:
        sorted_result = sorted(
            hwc_all_speedup.items(),
            key=lambda x: x[1][1]
            if x[1][0] == 0
            else (x[1][1] - x[1][0]) / abs(x[1][0]),
            reverse=True,
        )
    result = []
    columns = []
    for i in range(3):
        result.extend(
            [
                sorted_result[i][1][1],
                sorted_result[i][1][0],
                opt_all_speedup[sorted_result[i][0][0]],
            ]
        )
        columns.extend(
            [
                "hwc-training-profile #%s" % i,
                "hwc-same-input-profile #%s" % i,
                "opt #%s" % i,
            ]
        )
    return pd.DataFrame([result], columns=columns, index=[hwc_input_dir.name])


def three_best_generalize(pt_ignore_outlier: bool, no_zero: bool):
    large_dir = Path(
        "/mnt/storage/shixins/champsim_pt/input_generalization/simulation_output"
    )
    hwc_dir = large_dir / "ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru"
    columns = []
    for i in range(3):
        columns.extend(
            [
                "hwc-training-profile #%s" % i,
                "hwc-same-input-profile #%s" % i,
                "opt #%s" % i,
            ]
        )
    df = pd.DataFrame(columns=columns)
    for hwc_input_dir in sorted(hwc_dir.iterdir()):
        app_name = hwc_input_dir.name
        df = df.append(
            three_best_input_comparison(
                large_dir / "ChampSim_fdip_opt_generate" / app_name,
                hwc_input_dir,
                large_dir / "ChampSim_fdip_opt" / app_name,
                no_zero,
            )
        )
    print(df)
    no_zero_str = "_no_zero" if no_zero else ""
    ignore_outlier_str = "_no_outlier" if pt_ignore_outlier else ""
    df.to_csv("/mnt/storage/shixins/champsim_pt/input_generalization/plots/three_best%s%s.csv"
              % (ignore_outlier_str, no_zero_str))

    if pt_ignore_outlier:
        df = df.drop(["verilator"])

    plt.figure(figsize=(10, 6))
    ax = plt.gca()
    df.plot(y=columns, ylabel="IPC speedup (%)", kind="bar", ax=ax)
    ax.set_axisbelow(True)
    plt.grid(axis="y")
    ax.spines["bottom"].set_position("zero")
    for pos in ["top", "right", "left"]:
        ax.spines[pos].set_visible(False)
    plt.xticks(rotation=45)
    no_zero_str = "_no_zero" if no_zero else ""
    ignore_outlier_str = "_no_outlier" if pt_ignore_outlier else ""
    plt.savefig(
        "/mnt/storage/shixins/champsim_pt/input_generalization/plots/three_best%s%s.pdf"
        % (ignore_outlier_str, no_zero_str)
    )


@click.command()
@click.option("--pt-ignore-outlier", default=False, type=bool)
@click.option("--no-zero", default=False, type=bool)
def main(pt_ignore_outlier: bool, no_zero: bool):
    # single_train_generalize(pt_ignore_outlier)
    three_best_generalize(pt_ignore_outlier, no_zero)


if __name__ == "__main__":
    main()
