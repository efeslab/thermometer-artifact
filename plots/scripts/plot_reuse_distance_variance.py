import asyncio
import enum
import statistics

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
from dataclasses import dataclass
# import scipy.stats as ss
from matplotlib.patches import Ellipse

import plot_functions
from .utils import scripts_dir, plots_dir, project_dir, pt_traces


rc("font", **{"size": "23", "family": "serif", "serif": ["Palatino"]})
rc("text", usetex=True)

hatches = ["/", ".", "x", "\\", "+"]
line_styles = ["-", "--", "-.", ":"]
bar_colors = ["#5BB8D7", "#57A86B", "#A8A857", "#6E4587", "#ADEBCC", "#EBDCAD"]


@dataclass
class ReuseEntry:
    mean: float
    variance: float
    hwc_type: int


def cal_average_variance(line: pd.Series) -> pd.Series:
    row = line[0].split(",")
    pc = row[0]
    # First entry of reuse distance is always 0, which should not be considered.
    data = np.array(list(map(lambda x: int(x, 16), row[5:])), dtype=int)
    mean = np.mean(data)
    variance = np.var(data)
    return pd.Series([pc, mean, variance], index=["PC", "Mean", "Variance"])


def cal_difference_average_variance(line: pd.Series) -> pd.Series:
    row = line[0].split(",")
    pc = row[0]
    data = np.array(list(map(lambda x: int(x, 16), row[5:])), dtype=int)
    difference = np.diff(data)
    mean = np.mean(data)
    variance = 0 if len(difference) == 0 else sum(difference ** 2) / len(difference)
    # variance = np.var(difference)
    return pd.Series([pc, mean, variance], index=["PC", "Mean", "Variance"])


def cal_hit_access(line: pd.Series) -> pd.Series:
    row = line[0].split(",")
    pc = row[0]
    data = np.array(row[3:], dtype=int)
    unique, counts = np.unique(data, return_counts=True)
    counter = dict(zip(unique, counts))
    aaa = [counter.get(i, 0) for i in range(3)]
    return pd.Series([pc, aaa[0] / sum(aaa)], index=["PC", "HitAccess"])


def read_one_file(input_path: Path, func: Callable[[pd.Series], pd.Series], index: str = "PC"):
    df = pd.read_fwf(input_path, header=0, infer_nrows=100000)
    result = df.apply(func, axis=1)
    result = result.set_index(index)
    return result


def hwc_reuse_corr(
    reuse_distance_taken_path: Path,
    opt_access_record_path: Path,
    cal_average_variance_func: Callable[[pd.Series], pd.Series],
):
    df1 = read_one_file(reuse_distance_taken_path, cal_average_variance_func)
    df2 = read_one_file(opt_access_record_path, cal_hit_access)
    # df2 = df2.drop(columns="PC")
    df2 = df2 * 100
    result = pd.concat([df1, df2], axis=1)
    # result = result.set_index("PC")
    result = result.drop(["Variance"], axis=1)
    assert len(result.columns) == 2
    corr = result.corr()
    return corr["Mean"]["HitAccess"]


def foo(
    reuse_distance_taken_path: Path,
    opt_access_record_path: Path,
    scatter_plot_path: Path,
    cal_average_variance_func: Callable[[pd.Series], pd.Series],
    variance_type: str,
):
    df1 = read_one_file(reuse_distance_taken_path, cal_average_variance_func)
    df2 = read_one_file(opt_access_record_path, cal_hit_access)
    df2 = df2.drop(columns="PC")
    df2 = df2 * 100
    result = pd.concat([df1, df2], axis=1)
    # print(result)
    # result.to_csv("~/Desktop/plots/data/result.csv")
    result = result.rename(
        {
            # "Mean": "Mean of reuse distances of each branch",
            "Variance": "%s variance" % variance_type,
            "HitAccess": r"Hit-to-taken (\%)",
        },
        axis=1,
    )
    plt.figure(figsize=figaspect(0.3 / 0.9))
    result.plot.scatter(
        "Mean",
        "%s variance" % variance_type,
        c=r"Hit-to-taken (\%)",
        colormap="jet",
        s=1,
    )
    ax = plt.gca()
    ax.set_ylim(ymax=75)
    ax.set_ylim(ymin=-10)
    plt.tight_layout()
    plt.savefig(scatter_plot_path)
    plt.close()


def variance_mean(
    reuse_distance_dir: Path,
    access_record_dir: Path,
    output_path: Path,
    cal_average_variance_func: Callable[[pd.Series], pd.Series],
    trace_type: str,
):
    columns = ["Cold", "Warm", "Hot", "Overall"]
    result = pd.DataFrame(columns=columns)
    for reuse_distance_path in sorted(reuse_distance_dir.iterdir()):
        if trace_type == "pt" and reuse_distance_path.stem not in pt_traces:
            continue
        if trace_type == "ipc1" and reuse_distance_path.stem in pt_traces:
            continue
        name = reuse_distance_path.name
        opt_access_record_path = access_record_dir / name
        df1 = read_one_file(reuse_distance_path, cal_average_variance_func)
        df1 = df1.drop(columns="Mean")
        df2 = read_one_file(opt_access_record_path, cal_hit_access)
        df2 = df2.drop(columns="PC")
        all_variance = pd.concat([df1, df2], axis=1)
        cold_variance = all_variance[all_variance["HitAccess"] <= 0.5].mean(axis=0)[
            "Variance"
        ]
        warm_variance = all_variance[(all_variance["HitAccess"] <= 0.8) & (all_variance["HitAccess"] > 0.5)].mean(axis=0)[
            "Variance"
        ]
        hot_variance = all_variance[all_variance["HitAccess"] > 0.8].mean(axis=0)[
            "Variance"
        ]
        overall_variance = all_variance.mean(axis=0)["Variance"]
        df = pd.DataFrame(
            [[cold_variance, warm_variance, hot_variance, overall_variance]],
            columns=columns,
            index=[reuse_distance_path.stem],
        )
        result = result.append(df)

    print(result)

    return result


@click.command()
@click.option(
    "--reuse-distance-dir",
    default=Path("/mnt/storage/shixins/champsim_pt/reuse_distance_taken"),
    type=pathlib.Path,
)
@click.option(
    "--access-record-dir",
    default=Path("/mnt/storage/shixins/champsim_pt/opt_access_record/way4"),
    type=pathlib.Path,
)
@click.option(
    "--output-dir",
    default=Path("/mnt/storage/shixins/champsim_pt"),
    type=pathlib.Path,
)
@click.option(
    "--trace-type",
    type=click.Choice(["ipc1", "pt", "both"], case_sensitive=False),
    default="pt",
)
@click.option(
    "--local",
    default=False,
    type=bool,
)
@click.option(
    "--only-hwc-reuse-corr",
    default=False,
    type=bool
)
@click.option(
    "--only-hwc-reuse-corr-plot",
    default=False,
    type=bool
)
def main(
    reuse_distance_dir: Path, access_record_dir: Path, output_dir: Path, trace_type: str, local: bool,
        only_hwc_reuse_corr: bool, only_hwc_reuse_corr_plot: bool,
):
    if only_hwc_reuse_corr:
        hwc_reuse_corr_df = pd.DataFrame(
            [[hwc_reuse_corr(reuse_distance_dir / ("%s.csv" % pt_trace),
                             access_record_dir / ("%s.csv" % pt_trace),
                             cal_difference_average_variance)]
             for pt_trace in pt_traces],
            columns=["hwc reuse corr"],
            index=pt_traces
        )
        o_dir = output_dir / "paper_results" / "rebuttal"
        o_dir.mkdir(exist_ok=True, parents=True)
        hwc_reuse_corr_df.to_csv(o_dir / "hwc_reuse_corr.csv")
        return
    if only_hwc_reuse_corr_plot:
        o_dir = output_dir / "paper_results" / "rebuttal"
        hwc_reuse_corr_df = pd.read_csv(o_dir / "hwc_resuse_corr.csv", header=0, index_col=0)
        plot_functions.plot_bar_chart(
            o_dir / "hwc_resuse_corr.pdf",
            hwc_reuse_corr_df,
            ytitle="Temp reuse corr",
            show_legend=False,
            figsize=(0.4, 0.9),
            cut_lower_bound=-1.5
        )
        plt.close()
        return
    if not local:
        output_dir.mkdir(exist_ok=True, parents=True)
        o_dir = output_dir / "paper_results" / "variance"
        o_dir.mkdir(exist_ok=True)

        local_df = variance_mean(
            reuse_distance_dir,
            access_record_dir,
            output_dir
            / "reuse_distance_difference_variance_plot"
            / "way4"
            / "average_difference_variance.pdf",
            cal_difference_average_variance,
            trace_type,
        )

        global_df = variance_mean(
            reuse_distance_dir,
            access_record_dir,
            output_dir
            / "reuse_distance_variance_plot"
            / "way4"
            / "average_original_variance.pdf",
            cal_average_variance,
            trace_type,
        )

        new_cols = [
            local_df[["Overall"]].copy().rename(columns={"Overall": "Transient"}),
            global_df[["Overall"]].copy().rename(columns={"Overall": "Holistic"}),
        ]

        new_df = pd.concat(new_cols, axis=1)
        new_df.to_csv(o_dir / "transient_aggregate_reuse_variance.csv")
    else:
        o_dir = plots_dir / "paper_results/variance"
        new_df = pd.read_csv(o_dir / "transient_aggregate_reuse_variance.csv", header=0, index_col=0)

    rename_map = {"pgbench": "postgresql", "mysqllarge": "mysql"}
    new_df.rename(index=rename_map, inplace=True)
    # new_df = new_df.drop(["verilator"])
    plot_functions.plot_bar_chart(
        o_dir / "transient_aggregate_reuse_variance.pdf",
        new_df,
        ytitle="Variance",
        figsize=(0.35, 0.9),
    )


if __name__ == "__main__":
    main()
