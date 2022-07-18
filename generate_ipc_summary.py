import enum
import math
import pathlib
from functools import wraps
from pathlib import Path

import click
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib import colors
from matplotlib.ticker import PercentFormatter
from tqdm import tqdm

pd.set_option("display.max_columns", 5)
pd.set_option("display.width", 1000)

name_map = {
    "ChampSim_fdip_lru": "lru",
    "ChampSim_fdip_opt": "opt",
    "ChampSim_fdip_srrip": "srrip",
    "ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter": "hwc 80 50 curr hotter",
    "ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru": "hwc 80 50 curr hotter lru",
}

hit_access_9995_cdf = [
    0.875,
    0,
    0,
    0,
    0,
    0.66666667,
    0.5,
    0.5,
    0.06451613,
    0.5,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0.5,
    0.5,
    0,
    0.94444444,
    0.93333333,
    0.9,
    0.9375,
    0.95,
    0.89473684,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0.66666667,
    0.75,
    0.51795977,
    0.5,
    0.95833333,
    0.96428571,
    0.98,
    0,
    0.81818182,
    0.80533333,
    0.6,
    0.5,
    0.75,
    0.9375,
]


def cal_ipc_speedup(line: pd.Series) -> pd.Series:
    keys = ["opt", "srrip", "hwc 80 50 curr hotter", "hwc 80 50 curr hotter lru"]
    result = []
    for key in keys:
        result.append(100 * (line[key] - line["lru"]) / line["lru"])
    return pd.Series([line["Trace"]] + result, index=(["Trace"] + keys))


@click.command()
@click.option(
    "--input-dir",
    default=Path("/mnt/storage/shixins/champsim_pt/simulation_result"),
    type=pathlib.Path,
)
@click.option(
    "--output-dir",
    default=Path("/mnt/storage/shixins/champsim_pt/simulation_result_ipc_summary"),
    type=pathlib.Path,
)
@click.option("--pt", default=False, type=bool)
@click.option("--pt-ignore-outlier", default=False, type=bool)
def main(input_dir: Path, output_dir: Path, pt: bool, pt_ignore_outlier: bool):
    base_dir = (
        input_dir / "pt_result_analysis"
        if pt
        else input_dir / "ipc1_correct_new_btb_result_analysis"
    )
    # base_dir = Path("/Users/shixinsong/Desktop/UM/SURE/Code/ChampSim-pt/ipc1_correct_new_btb_result_analysis/")
    mean_columns = [
        "num_ways",
        "opt",
        "srrip",
        "hwc 80 50 curr hotter",
        "hwc 80 50 curr hotter lru",
    ]
    mean_df = pd.DataFrame(columns=mean_columns)
    for total_ways in [4, 8, 16, 32, 64, 128]:
        # The commented part is mean to generate ipc speedup csv.
        # If the experiment is not rerun, there is no need to re run this part.
        # index_col = None
        # cols = []
        # for name in name_map.keys():
        #     # for file in sorted(base_dir.glob("*.csv")):
        #     file = base_dir / ("%s%s.csv" % (name, total_ways))
        #     print(file)
        #     df = pd.read_csv(file, header=0)
        #     if index_col is None:
        #         index_col = df[["Trace"]].copy()
        #     # new_df = df[["IPC"]].copy().rename(columns={"IPC": file.stem})
        #     new_df = df[["IPC"]].copy().rename(columns={"IPC": name_map[name]})
        #     cols.append(new_df)
        # result_df = pd.concat([index_col, *cols], axis=1)
        # new_df = result_df.apply(cal_ipc_speedup, axis=1)
        # output_dir.mkdir(exist_ok=True)
        # result_path = output_dir / ("pt_way%s.csv" % total_ways) if pt else output_dir / ("ipc1_way%s.csv" % total_ways)
        # result_df.to_csv(result_path, index=False)
        ipc_speedup_result_path = (
            output_dir / ("ipc_speedup_pt_way%s.csv" % total_ways)
            if pt
            else output_dir / ("ipc_speedup_ipc1_way%s.csv" % total_ways)
        )
        # new_df.to_csv(ipc_speedup_result_path, index=False)
        new_df = pd.read_csv(ipc_speedup_result_path)
        print(new_df)
        if pt and pt_ignore_outlier:
            new_df = new_df.drop([7])
        print(new_df)
        hit_access_df = pd.DataFrame(hit_access_9995_cdf, columns=["99.95"])
        if not pt:
            new_df = pd.concat([new_df, hit_access_df], axis=1)
            new_df = new_df.sort_values(by=["99.95", "Trace"])
        # print(new_df)
        plt.figure(figsize=(20, 12))
        ax = plt.gca()
        new_df.iloc[0:24].plot(
            x="Trace",
            y=["opt", "srrip", "hwc 80 50 curr hotter", "hwc 80 50 curr hotter lru"],
            xlabel="",
            ylabel="IPC speedup (%)",
            kind="bar",
            ax=ax,
            width=0.4,
        )
        ax.set_axisbelow(True)
        plt.grid(axis="y")
        ax.spines["bottom"].set_position("zero")
        for pos in ["top", "right", "left"]:
            ax.spines[pos].set_visible(False)
        plt.xticks(rotation=45)
        if pt:
            if pt_ignore_outlier:
                filename = "plot_pt_ignore_outlier_way%s.pdf" % total_ways
            else:
                filename = "plot_pt_way%s.pdf" % total_ways
        else:
            filename = "plot_ipc1_way%s.pdf" % total_ways
        plot_result_path = output_dir / filename
        plt.savefig(str(plot_result_path))
        plt.close()
        mean_line = list(new_df.iloc[0:24].mean(axis=0))
        if not pt:
            mean_line = mean_line[:-1]
        mean_line = [total_ways] + mean_line
        print(list(mean_line))
        mean_df = mean_df.append(pd.DataFrame([mean_line], columns=mean_columns))

    plt.figure(figsize=(10, 6))
    ax = plt.gca()
    mean_df.plot(
        x="num_ways",
        y=mean_columns[1:],
        xlabel="Num of ways",
        ylabel="IPC speedup (%)",
        kind="bar",
        ax=ax,
        width=0.4,
    )
    ax.set_axisbelow(True)
    plt.grid(axis="y")
    ax.spines["bottom"].set_position("zero")
    for pos in ["top", "right", "left"]:
        ax.spines[pos].set_visible(False)
    if pt:
        if pt_ignore_outlier:
            filename = "plot_pt_mean_ignore_outlier.pdf"
        else:
            filename = "plot_pt_mean.pdf"
    else:
        filename = "plot_ipc1_mean.pdf"
    plot_result_path = output_dir / filename
    plt.savefig(str(plot_result_path))
    plt.close()
    print(mean_df)


if __name__ == "__main__":
    main()
