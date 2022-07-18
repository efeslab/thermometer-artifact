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

key_result_name_map = {
    "ChampSim_fdip_lru": "LRU",
    "ChampSim_fdip_srrip": "SRRIP",
    # "ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter": "Thermometer-random",
    "ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru": "Thermometer",
    "ChampSim_fdip_opt": "OPT",
}


def parse_one_file(input_path: Path) -> float:
    with input_path.open(mode="r") as input_file:
        s = input_file.read()
        # Get IPC
        ipc_str = re.search("CPU 0 cumulative IPC.*\n", s).group(0)
        ipc_arr = ipc_str.split(" ")
        return float(ipc_arr[4])


def get_dir_name(program_name: str, btb_size: int, total_ways: int):
    prefix = ("%sK_" % btb_size) if btb_size != 8 else ""
    return prefix + program_name + str(total_ways) + "_result"


def get_input_filename(
        pt_trace: str,
        use_default_btb_record: bool,
        train_btb_ways: int,
        train_btb_entries: int,
        column_name: str
):
    if use_default_btb_record and column_name == "Thermometer":
        return f"{pt_trace}_use_default_btb_record_{train_btb_ways}_{train_btb_entries}_result.txt"
    else:
        return f"{pt_trace}_result.txt"


def get_output_filename(
        prefix: str,
        suffix: str,
        use_default_btb_record: bool,
        train_btb_ways: int,
        train_btb_entries: int
):
    if use_default_btb_record:
        return f"{prefix}_use_default_btb_record_{train_btb_ways}_{train_btb_entries}{suffix}"
    else:
        return f"{prefix}{suffix}"


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


def generate_ipc_table(big_input_dir: Path, output_path: Path, name_map: dict,
                       use_default_btb_record: bool,
                       train_btb_ways: int,
                       train_btb_entries: int,
                       way_list: list, size_list: list):
    df = pd.DataFrame(columns=list(name_map.values()))
    for pt_trace in pt_traces:
        for total_ways in way_list:
            for btb_size in size_list:
                values = []
                columns = []
                for program_name, column_name in name_map.items():
                    input_filename = get_input_filename(
                        pt_trace=pt_trace,
                        use_default_btb_record=use_default_btb_record,
                        train_btb_ways=train_btb_ways,
                        train_btb_entries=train_btb_entries,
                        column_name=column_name
                    )
                    input_file = (
                            big_input_dir
                            / get_dir_name(program_name, btb_size, total_ways)
                            / input_filename
                    )
                    ipc = parse_one_file(input_file)
                    values.append(ipc)
                    columns.append(column_name)
                df = df.append(
                    pd.DataFrame(
                        data=[values],
                        index=[f"{pt_trace};{btb_size}K;{total_ways}"],
                        columns=columns,
                    )
                )
    df.to_csv(output_path)
    return df


def gen_sensitivity_result(
        big_input_dir: Path,
        output_dir: Path,
        sensitivity_type: str,
        use_default_btb_record: bool,
        way_list: list,
        size_list: list,
        train_btb_ways: int,
        train_btb_entries: int,
):
    output_dir.mkdir(exist_ok=True)

    ipc_filename = get_output_filename(
        prefix=f"ipc_{sensitivity_type}",
        suffix=".csv",
        use_default_btb_record=use_default_btb_record,
        train_btb_ways=train_btb_ways,
        train_btb_entries=train_btb_entries
    )

    ipc_speedup_filename = get_output_filename(
        prefix=f"ipc_speedup_{sensitivity_type}",
        suffix=".csv",
        use_default_btb_record=use_default_btb_record,
        train_btb_ways=train_btb_ways,
        train_btb_entries=train_btb_entries
    )

    opt_percentage_filename = get_output_filename(
        prefix=f"percentage_opt_ipc_speedup_{sensitivity_type}",
        suffix=".csv",
        use_default_btb_record=use_default_btb_record,
        train_btb_ways=train_btb_ways,
        train_btb_entries=train_btb_entries
    )

    ipc_result_df = generate_ipc_table(
        big_input_dir, output_dir / ipc_filename, key_result_name_map,
        use_default_btb_record=use_default_btb_record,
        train_btb_ways=train_btb_ways,
        train_btb_entries=train_btb_entries,
        way_list=way_list,
        size_list=size_list
    )
    print(ipc_result_df)
    ipc_speedup_df = ipc_result_df.apply(cal_ipc_speedup, axis=1)
    ipc_speedup_df.to_csv(output_dir / ipc_speedup_filename)
    print(ipc_speedup_df)
    opt_percentage_df = ipc_speedup_df.apply(cal_percentage_of_opt, axis=1)
    opt_percentage_df.to_csv(output_dir / opt_percentage_filename)
    print(opt_percentage_df)


@click.command()
@click.option(
    "--local",
    default=False,
    type=bool,
)
def main(local: bool):
    # big_input_dir = Path("/mnt/storage/shixins/champsim_pt/pt_result")
    big_input_dir = Path("/home/shixinsong/Desktop/UM/SURE/plots/pt_result")
    way_output_dir = Path("/home/shixinsong/Desktop/UM/SURE/plots/paper_results/btb_way")
    size_output_dir = Path("/home/shixinsong/Desktop/UM/SURE/plots/paper_results/btb_size")
    size_list = [1, 2, 4, 8, 16, 32]
    way_list = [4, 8, 16, 32, 64, 128]

    # Gen for different ways
    for train_btb_ways in way_list:
        train_btb_entries = 8  # default size is 8K
        gen_sensitivity_result(
            big_input_dir=big_input_dir,
            output_dir=way_output_dir,
            sensitivity_type="way",
            use_default_btb_record=True,
            way_list=way_list,
            size_list=[train_btb_entries],
            train_btb_ways=train_btb_ways,
            train_btb_entries=train_btb_entries
        )

    for train_btb_entries in size_list:
        train_btb_ways = 4  # default associativity is 4 way
        gen_sensitivity_result(
            big_input_dir=big_input_dir,
            output_dir=size_output_dir,
            sensitivity_type="size",
            use_default_btb_record=True,
            way_list=[train_btb_ways],
            size_list=size_list,
            train_btb_ways=train_btb_ways,
            train_btb_entries=train_btb_entries
        )

    # if not local:
    #     # output_dir = Path("/mnt/storage/shixins/champsim_pt/paper_results/btb_size")
    #     output_dir = Path("/home/shixinsong/Desktop/UM/SURE/plots/paper_results/btb_size")
    #     output_dir.mkdir(exist_ok=True)
    #     ipc_result_df = generate_ipc_table(
    #         big_input_dir, output_dir / "ipc_result.csv", key_result_name_map,
    #         use_default_btb_record=True,
    #         way_list=[4],
    #         size_list=size_list
    #     )
    #     print(ipc_result_df)
    #     ipc_speedup_df = ipc_result_df.apply(cal_ipc_speedup, axis=1)
    #     print(ipc_speedup_df)
    #     opt_percentage_df = ipc_speedup_df.apply(cal_percentage_of_opt, axis=1)
    #     print(opt_percentage_df)
    #     opt_percentage_df.to_csv(output_dir / "percentage_opt_ipc_speedup_size.csv")
    #
    #     plot_functions.plot_bar_chart(
    #         output_dir / "ipc_speedup_size.pdf",
    #         ipc_speedup_df,
    #         r"IPC Speedup over FDIP with LRU (\%)",
    #         two_level=True,
    #         add_average=False,
    #         cut_upper_bound=50,
    #         figsize=(0.3, 0.9),
    #         legend_col_num=4,
    #         second_level_y_pos=-15,
    #     )
    # else:
    #     output_dir = Path("/home/shixinsong/Desktop/UM/SURE/plots/paper_results/btb_size")
    #     opt_percentage_df = pd.read_csv(output_dir / "percentage_opt_ipc_speedup_size.csv",
    #                                     header=0, index_col=0)
    #
    # # Generate speedup summary for different btb size
    # plot_functions.plot_bar_chart(
    #     output_path=output_dir / "percentage_opt_ipc_speedup_size.pdf",
    #     df=opt_percentage_df,
    #     ytitle=r"{\huge\% of optimal policy speedup}",
    #     two_level=True,
    #     add_average=False,
    #     figsize=(0.4, 0.9),
    #     second_level_y_pos=-23,
    #     legend_col_num=1,
    # )
    #
    #
    # output_dir = Path("/home/shixinsong/Desktop/UM/SURE/plots/paper_results/btb_way")
    # output_dir.mkdir(exist_ok=True)
    # ipc_result_df = generate_ipc_table(
    #     big_input_dir, output_dir / "ipc_result.csv", key_result_name_map,
    #     use_default_btb_record=True,
    #     way_list=[4, 8, 16, 32, 64, 128],
    #     size_list=[8]
    # )
    # print(ipc_result_df)
    # ipc_speedup_df = ipc_result_df.apply(cal_ipc_speedup, axis=1)
    # print(ipc_speedup_df)
    # opt_percentage_df = ipc_speedup_df.apply(cal_percentage_of_opt, axis=1)
    # print(opt_percentage_df)
    # opt_percentage_df.to_csv(output_dir / "percentage_opt_ipc_speedup_way.csv")


if __name__ == "__main__":
    main()
