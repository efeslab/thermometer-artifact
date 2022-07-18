from pathlib import Path
import pandas as pd
import re
import click
import numpy as np
from typing import Callable
import plot_functions

repl_policies = {
    "LRU": "ChampSim_fdip_opt_generate",
    "SRRIP": "ChampSim_fdip_srrip",
    "OPT": "ChampSim_fdip_opt",
    "Thermometer": "ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru",
}


def get_input_list(input_dir: Path):
    input_files = input_dir.glob("*.txt")
    return list(map(lambda x: x.stem.split("_")[0], input_files))


def one_app_generate(input_dir: Path, app_name: str, output_dir: Path):
    sys_name = "Thermometer"
    input_list = get_input_list(input_dir / repl_policies["LRU"] / app_name)
    df = pd.DataFrame()
    mpki_df = pd.DataFrame()
    for repl_policy, subdir_name in repl_policies.items():
        if repl_policy == sys_name:
            continue
        ipc_list = []
        mpki_list = []
        for input_name in input_list:
            input_path = input_dir / subdir_name / app_name / f"{input_name}_{input_name}.txt"
            # print(input_path)
            with input_path.open(mode="r") as input_file:
                s = input_file.read()
                ipc_str = re.search("CPU 0 cumulative IPC.*\n", s).group(0)
                ipc_arr = ipc_str.split(" ")
                ipc = float(ipc_arr[4])
                ipc_list.append(ipc)
                branch = re.search("BTB taken num:.*\n", s).group(0)
                branch_mpki = float((branch.split())[11])
                mpki_list.append(branch_mpki)
        one_repl = pd.Series(ipc_list, index=input_list, name=repl_policy)
        df = df.append(one_repl)
        mpki_df = mpki_df.append(pd.Series(mpki_list, index=input_list, name=repl_policy))
    # print(df)
    for train_name in input_list:
        ipc_list = []
        mpki_list = []
        for trace_name in input_list:
            input_path = input_dir / repl_policies[sys_name] / app_name / f"{trace_name}_{train_name}.txt"
            # print(input_path)
            with input_path.open(mode="r") as input_file:
                s = input_file.read()
                ipc_str = re.search("CPU 0 cumulative IPC.*\n", s).group(0)
                ipc_arr = ipc_str.split(" ")
                ipc = float(ipc_arr[4])
                ipc_list.append(ipc)
                branch = re.search("BTB taken num:.*\n", s).group(0)
                branch_mpki = float((branch.split())[11])
                mpki_list.append(branch_mpki)
        one_repl = pd.Series(ipc_list, index=input_list, name=f"{sys_name}-{train_name}")
        df = df.append(one_repl)
        mpki_df = mpki_df.append(pd.Series(mpki_list, index=input_list, name=f"{sys_name}-{train_name}"))
    # print(df)
    df = df.transpose()
    df.to_csv(output_dir / f"{app_name}.csv")
    speedup_df = cal_speedup(df)
    speedup_df.to_csv(output_dir / f"{app_name}_speedup_new.csv")

    mpki_df = mpki_df.transpose()
    mpki_df.to_csv(output_dir / f"{app_name}_btb_miss_rate.csv")
    reduction_df = cal_reduction(mpki_df)
    reduction_df.to_csv(output_dir / f"{app_name}_btb_miss_rate_reduction.csv")


def cal_speedup(df: pd.DataFrame):
    speedup_df = pd.DataFrame()
    for name in df.columns:
        if name != "LRU":
            speedup_df[name] = 100 * (df[name] - df["LRU"]) / df["LRU"]
    return speedup_df


def cal_reduction(df: pd.DataFrame):
    reduction_df = pd.DataFrame()
    for name in df.columns:
        if name != "LRU":
            reduction_df[name] = 100 * (df["LRU"] - df[name]) / df["LRU"]
    return reduction_df


def generate_summary():
    output_dir = Path("/mnt/storage/shixins/champsim_pt/input_generalization/summary")
    output_dir.mkdir(exist_ok=True)
    pt_traces = [
        "cassandra",
        "clang",
        "drupal",
        "finagle-chirper",
        "finagle-http",
        "kafka",
        "mediawiki",
        "pgbench",
        "python",
        "tomcat",
        "verilator",
        "wordpress",
    ]
    for app in pt_traces:
        one_app_generate(Path("/mnt/storage/shixins/champsim_pt/input_generalization/simulation_output"), app,
                         output_dir)
    one_app_generate(Path("/mnt/storage/shixins/champsim_pt/input_generalization/simulation_output"), "mysqllarge",
                     output_dir)


def find_three_pairs(input_file: Path):
    df = pd.read_csv(input_file, index_col=0, header=0)
    sys_list = list(map(lambda x: f"Thermometer-{x}", df.columns))
    pos_df = df[df["SRRIP"] > 0]
    filtered_dict = {}
    for trace_file in pos_df.index:
        srrip_speedup = pos_df["SRRIP"][trace_file]
        opt_speedup = pos_df["OPT"][trace_file]
        same_speedup = pos_df[f"Thermometer-{trace_file}"][trace_file]
        for train_file in df.index:
            if train_file == trace_file:
                continue
            sys_speedup = pos_df[f"Thermometer-{train_file}"][trace_file]
            if sys_speedup < srrip_speedup:
                continue
            filtered_dict[(trace_file, train_file)] = (sys_speedup - same_speedup) / same_speedup
    sorted_result = sorted(filtered_dict.items(), key=lambda item: item[1], reverse=True)
    print(sorted_result)
    app_name = input_file.stem.replace("_speedup", "")
    result = pd.DataFrame(
        [
            [pos_df["SRRIP"][x[0][0]], pos_df["OPT"][x[0][0]], pos_df[f"Thermometer-{x[0][1]}"][x[0][0]],
             pos_df[f"Thermometer-{x[0][0]}"][x[0][0]]]
            for x in sorted_result[0:3]
        ],
        index=[
            f"{app_name};\#{i + 1}"
            for i in range(3)
        ],
        columns=["SRRIP", "OPT", "Thermometer-training-profile", "Thermometer-same-input-profile"]
    )
    return result


def parse_all_app(input_dir: Path):
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
    df = pd.DataFrame()
    for app_name in pt_traces:
        input_path = input_dir / f"{app_name}_speedup.csv"
        df = df.append(find_three_pairs(input_path))
    print(df)
    df.to_csv(input_dir / "all.csv")
    rename_map = {}
    for i in range(1, 4):
        rename_map[f"pgbench;\#{i}"] = f"postgresql;\#{i}"
        rename_map[f"mysqllarge;\#{i}"] = f"mysql;\#{i}"
    df.rename(index=rename_map, inplace=True)
    per_df = pd.DataFrame()
    for col in df.columns:
        if col != "OPT":
            per_df[col] = 100 * df[col] / df["OPT"]
    print(per_df)
    per_df.to_csv(input_dir / "all_speedup.csv")
    per_df = per_df.drop(["verilator;\#1", "verilator;\#2", "verilator;\#3"])
    print(per_df)

    plot_functions.plot_bar_chart(
        output_path=input_dir / "all_speedup.pdf",
        df=per_df,
        ytitle=r"{\huge\% of optimal policy speedup}",
        xlabel_rotation=0,
        two_level=True,
        add_average=True,
        figsize=(0.2, 0.9),
        second_level_y_pos=-15,
        second_level_rotation=20,
        legend_col_num=3,
        two_column=1.5,
        legend_size=20,
        cut_upper_bound=120,
    )


def main():
    generate_summary()
    # parse_all_app(Path("/home/shixinsong/Desktop/UM/SURE/plots/input_generalization/summary"))


if __name__ == '__main__':
    main()
