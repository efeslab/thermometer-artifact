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
    baseline_input_dir: Path, srrip_input_dir: Path, hwc_input_dir: Path, opt_input_dir: Path, no_zero: bool
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

    # Get IPCs for srrip
    srrip_all_ipc = {}
    for input_path in srrip_input_dir.iterdir():
        trace_file, train_file = input_path.stem.split("_")
        assert trace_file == train_file
        srrip_all_ipc[trace_file] = parse_one_file(input_path)

    # Get speedup
    srrip_all_speedup = {}
    for trace_file, ipc in srrip_all_ipc.items():
        srrip_all_speedup[trace_file] = speedup(ipc, baseline_all_ipc[trace_file])

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
    columns = ["SRRIP", "Thermometer-training-profile", "Thermometer-same-input-profile", "OPT"]
    for i in range(3):
        result.append(
            [
                srrip_all_speedup[sorted_result[i][0][0]],
                sorted_result[i][1][1],
                sorted_result[i][1][0],
                opt_all_speedup[sorted_result[i][0][0]],
            ]
        )
    return pd.DataFrame(
        result,
        columns=columns,
        index=[hwc_input_dir.name + r";\#" + str(i + 1) for i in range(3)],
    )


def cal_percentage_of_opt(line: pd.Series) -> pd.Series:
    result = []
    new_index = list(line.index)
    new_index.remove("OPT")
    for key in new_index:
        result.append(100 * line[key] / line["OPT"])
    return pd.Series(result, index=new_index)


def three_best_generalize(no_zero: bool, prefix: str, local: bool, app_list: list):
    large_dir = Path(
        "/mnt/storage/shixins/champsim_pt/input_generalization/simulation_output"
    )
    hwc_dir = large_dir / "ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru"
    columns = ["SRRIP", "Thermometer-training-profile", "Thermometer-same-input-profile", "OPT"]
    # for i in range(3):
    #     columns.extend(
    #         [
    #             "hwc-training-profile #%s" % i,
    #             "hwc-same-input-profile #%s" % i,
    #             "opt #%s" % i,
    #             ]
    #     )
    if not local:
        df = pd.DataFrame(columns=columns)
        for app_name in sorted(app_list):
        # for hwc_input_dir in sorted(hwc_dir.iterdir()):
        #     app_name = hwc_input_dir.name
            hwc_input_dir = hwc_dir / app_name
            df = df.append(
                three_best_input_comparison(
                    large_dir / "ChampSim_fdip_opt_generate" / app_name,
                    large_dir / "ChampSim_fdip_srrip" / app_name,
                    hwc_input_dir,
                    large_dir / "ChampSim_fdip_opt" / app_name,
                    no_zero,
                )
            )
        print(df)
        no_zero_str = "_no_zero" if no_zero else ""
        df.to_csv(
            "/mnt/storage/shixins/champsim_pt/input_generalization/plots/%s_three_best%s.csv"
            % (prefix, no_zero_str)
        )

        # Cal percentage of OPT
        per_df = df.apply(cal_percentage_of_opt, axis=1)
        print(per_df)
        per_df.to_csv(
            "/mnt/storage/shixins/champsim_pt/input_generalization/plots/%s_opt_percentage_three_best%s.csv"
            % (prefix, no_zero_str)
        )

        print("??")
        rename_index = {}
        for a in df.index:
            rename_index[a] = a.replace("_", " ")

        df.rename(rename_index, inplace=True)
        print(df)

        plot_functions.plot_bar_chart(
            output_path=Path(
                "/mnt/storage/shixins/champsim_pt/input_generalization/plots/%s_three_best%s.pdf"
                % (prefix, no_zero_str)
            ),
            df=df,
            ytitle=r"IPC Speedup over FDIP with LRU (\%)",
            two_level=True,
            add_average=False,
            cut_upper_bound=6,
            figsize=(0.3, 0.9),
            second_level_y_pos=-5.5,
            second_level_rotation=35,
            legend_col_num=1,
        )
        output_path = Path(
            "/mnt/storage/shixins/champsim_pt/input_generalization/plots/%s_opt_percentage_three_best%s.pdf"
            % (prefix, no_zero_str)
        )
    else:
        no_zero_str = "_no_zero" if no_zero else ""
        # per_df = pd.read_csv("/Users/shixinsong/Desktop/plots/paper_results/plots/percentage_three_best.csv",
        #                      header=0, index_col=0)
        per_df = pd.read_csv("/mnt/storage/shixins/champsim_pt/input_generalization/plots/percentage_three_best.csv",
                             header=0, index_col=0)
        # output_path = Path(
        #     "/Users/shixinsong/Desktop/plots/paper_results/plots/%s_opt_percentage_three_best%s.pdf"
        #     % (prefix, no_zero_str)
        # )
        output_path = Path(
            "/mnt/storage/shixins/champsim_pt/input_generalization/plots/%s_opt_percentage_three_best%s.pdf"
            % (prefix, no_zero_str)
        )

    print("??")
    rename_index = {}
    for a in per_df.index:
        rename_index[a] = a.replace("_", " ")

    per_df.rename(rename_index, inplace=True)
    print(per_df)

    rename_map = {}
    for i in range(1, 4):
        rename_map[f"pgbench;\#{i}"] = f"postgres;\#{i}"
    per_df.rename(index=rename_map, inplace=True)

    print(per_df)

    plot_functions.plot_bar_chart(
        output_path=output_path,
        df=per_df,
        ytitle=r"{\huge\% of optimal policy speedup}",
        xlabel_rotation=0,
        two_level=True,
        add_average=True,
        figsize=(0.2, 0.9),
        second_level_y_pos=-80,
        second_level_rotation=20,
        legend_col_num=1,
        two_column=1.5,
        legend_size=20,
        cut_upper_bound=135,
    )

    # plt.figure(figsize=(10, 6))
    # ax = plt.gca()
    # df.plot(y=columns, ylabel="IPC speedup (%)", kind="bar", ax=ax)
    # ax.set_axisbelow(True)
    # plt.grid(axis="y")
    # ax.spines["bottom"].set_position("zero")
    # for pos in ["top", "right", "left"]:
    #     ax.spines[pos].set_visible(False)
    # plt.xticks(rotation=45)
    # plt.savefig(
    #     "/mnt/storage/shixins/champsim_pt/input_generalization/plots/%s_three_best%s.pdf"
    #     % (prefix, no_zero_str)
    # )


@click.command()
@click.option("--no-zero", default=False, type=bool)
@click.option("--not-trained", type=str)
@click.option("--local", default=False, type=bool)
@click.option(
    "--app-list-str",
    default="cassandra,drupal,finagle-chirper,finagle-http,kafka,mediawiki,mysql,nginx,postgres,tomcat,verilator,wordpress",
    type=click.STRING
)
def main(no_zero: bool, not_trained: str, local: bool, app_list_str: str):
    app_list = app_list_str.split(",")
    # single_train_generalize(pt_ignore_outlier)
    three_best_generalize(no_zero, not_trained, local, app_list)


if __name__ == "__main__":
    main()
