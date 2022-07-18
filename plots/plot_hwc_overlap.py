from pathlib import Path
import pandas as pd
import re
import click
import numpy as np
from typing import Callable
import plot_functions

data_center_app = ["cassandra", "drupal", "finagle-chirper", "finagle-http", "kafka", "mediawiki", "tomcat", "verilator", "wordpress"]


def read_one_file(
        input_path: Path, func: Callable[[pd.Series], pd.Series], index: str = "PC"
):
    df = pd.read_fwf(input_path, header=0, infer_nrows=100000)
    result = df.apply(func, axis=1)
    result.set_index(index, inplace=True)
    result.rename(columns={"HWCType": input_path.stem}, inplace=True)
    return result


def get_hwc(line: pd.Series) -> pd.Series:
    row = line[0].split(",")
    pc = row[0]
    target = row[1]
    distance = abs(int(target, 16) - int(pc, 16))
    branch_type = row[2]
    data = np.array(row[3:], dtype=int)
    unique, counts = np.unique(data, return_counts=True)
    counter = dict(zip(unique, counts))
    aaa = [counter.get(i, 0) for i in range(3)]
    hit_access = aaa[0] / sum(aaa)
    if hit_access <= 0.5:
        hwc_type = 0  # cold
    elif hit_access <= 0.8:
        hwc_type = 1  # warm
    else:
        hwc_type = 2  # hot
    return pd.Series(
        [pc, hwc_type],
        index=["PC", "HWCType"],
    )
    # return pd.Series(
    #     [pc, distance, branch_type, hit_access, hwc_type],
    #     index=["PC", "Distance", "BranchType", "HitAccess", "HWCType"],
    # )


def parse_overlap(line: pd.Series) -> pd.Series:
    same_value = line[0]
    for a in line:
        if a != same_value:
            same_value = -1
    return pd.Series(
        [line.index, same_value],
        index=["PC", "HWCType"],
    )


def parse_one_app(input_dir: Path, app: str, output_dir: Path, read_summary: bool, pattern: str = "_train_*"):
    output_dir.mkdir(exist_ok=True)
    if not read_summary:
        all_df = []
        for input_path in input_dir.glob(f"*{app}{pattern}.csv"):
            print(app, input_path.stem)
            all_df.append(read_one_file(input_path, get_hwc))
        summary = pd.concat(all_df, axis=1, copy=False).fillna(0)
        summary.to_csv(output_dir / "raw_summary.csv")
    else:
        summary = pd.read_csv(output_dir / "raw_summary.csv", index_col=0, header=0)
    for first in summary.columns:
        # for second in summary.columns:
        #     s = summary.apply(lambda x: x[first] if x[first] == x[second] else -1, axis=1)
        df = pd.DataFrame(
            [
                summary.apply(lambda x: x[first] if x[first] == x[second] else -1, axis=1)
                # summary[first] if summary[first] == summary[second] else summary["not_equal"]
                for second in summary.columns
            ],
            index=summary.columns
        ).transpose()
        count = pd.DataFrame(
            [
                df[col].value_counts() for col in df.columns
            ],
            index=df.columns
        )
        count = count.fillna(0)
        count.to_csv(output_dir / f"train_{first}.csv")


def find_best_for_one_app(data_dir: Path, app: str):
    min_value = 1
    best = app
    for input_file in data_dir.glob("train_*.csv"):
        df = pd.read_csv(input_file, index_col=0, header=0)
        df["total"] = df.sum(axis=1)
        df["different ratio"] = df["-1.0"] / df["total"]
        x = df["different ratio"].mean()
        if x < min_value:
            min_value = x
            best = input_file.stem
    print(best)
    return best, min_value


def compare_two_opt_access_record():
    output_dir = Path("/home/shixinsong/Desktop/UM/SURE/plots/mysql_analysis")
    output_dir.mkdir(exist_ok=True)
    raw_df_path = output_dir / "read-only_read-write.csv"
    if not raw_df_path.exists():
        read_only_path = Path("/home/shixinsong/Desktop/UM/SURE/plots/opt_access_record/way4/mysql_train_read-only.csv")
        read_write_path = Path("/home/shixinsong/Desktop/UM/SURE/plots/opt_access_record/way4/mysql_train_read-write.csv")
        # read_only_path = Path("/home/shixinsong/Desktop/UM/SURE/plots/opt_access_record/way4/mysql_train_read-only.csv")
        # read_write_path = Path("/home/shixinsong/Desktop/UM/SURE/plots/opt_access_record/way4/mysql_train_read-write.csv")
        df_read_only = read_one_file(read_only_path, get_hwc)
        df_read_write = read_one_file(read_write_path, get_hwc)
        df_read_only.to_csv(output_dir / "read_only.csv")
        df_read_write.to_csv(output_dir / "read_write.csv")
        df = pd.concat(
            [
                df_read_only,
                df_read_write,
            ],
            axis=1
        )
        df.to_csv(raw_df_path)
    else:
        df = pd.read_csv(raw_df_path, index_col=0, header=0)
    df.fillna(-1, inplace=True)
    x = df.value_counts()
    x.to_csv(output_dir / "value_counts.csv")


def plot_manually_generated_df():
    manually_generated_df = pd.read_csv(
        Path("/home/shixinsong/Desktop/UM/SURE/plots/rebuttal/hwc_overlap/all_app.csv"),
        sep=" ",
        index_col=0,
        header=0
    )
    manually_generated_df.rename({"postgres": "postgresql"}, inplace=True)
    print(manually_generated_df)
    print(manually_generated_df.mean())
    plot_functions.plot_bar_chart(
        output_path=Path("/home/shixinsong/Desktop/UM/SURE/plots/rebuttal/hwc_overlap/all_app.pdf"),
        df=manually_generated_df,
        stacked=True,
        ytitle="Percentage (\%)",
        add_average=True,
        figsize=(0.35, 0.9)
    )
    return


def plot_mysql_overlap():
    sql_path = Path("/home/shixinsong/Desktop/UM/SURE/paper_results/rebuttal/hwc_overlap/mysql/train_largest_trace_mysql_1thread_20000tbSz_250tbNum_oltp_read_write.lua_60time.gz.csv")
    df = pd.read_csv(sql_path, index_col=0, header=0)
    # new_index = list(map(lambda x: x.split(".")[0].replace("largest_trace_mysql_1thread_20000tbSz_250tbNum_oltp_", ""),
    #                      df.index))
    df = df.rename(lambda x: x.split(".")[0].replace("largest_trace_mysql_1thread_20000tbSz_250tbNum_oltp_", "").replace("_", " "), axis="index")
    print(df)
    plot_df = pd.DataFrame()
    plot_df["Same"] = 100 * (df["0.0"] + df["1.0"] + df["2.0"]) / df.sum(axis=1)
    plot_df["Different"] = 100 * df["-1.0"] / df.sum(axis=1)
    plot_df.drop(["read write"], inplace=True)
    print(plot_df)
    print(plot_df.mean())
    plot_functions.plot_bar_chart(
        output_path=Path("/home/shixinsong/Desktop/UM/SURE/paper_results/rebuttal/hwc_overlap/mysql/read_write.pdf"),
        df=plot_df,
        stacked=True,
        ytitle="Percentage (\%)",
        add_average=True,
    )
    return


    input_dir = Path("/home/shixinsong/Desktop/UM/SURE/paper_results/rebuttal/way4_8192K")
    output_dir = Path("/home/shixinsong/Desktop/UM/SURE/paper_results/rebuttal/hwc_overlap")
    output_dir.mkdir(exist_ok=True)

    for app in ["mysql"]:
        parse_one_app(input_dir, app, output_dir / app, True, "*")


    names = []
    values = []
    for app in ["mysql"]:
        best, diff_ratio = find_best_for_one_app(output_dir / app, app)
        names.append(best)
        values.append(diff_ratio)
    df = pd.DataFrame(
        [values],
        index=["different temperature"],
        columns=names
    )
    print(df)
    df.to_csv(output_dir / "best_summary.csv")


def main():
    plot_manually_generated_df()
    # compare_two_opt_access_record()


if __name__ == '__main__':
    main()
