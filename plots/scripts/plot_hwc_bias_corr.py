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
# from tqdm import tqdm
from typing import Callable
import scipy.stats as ss

import plot_functions
from .utils import scripts_dir, plots_dir, project_dir, pt_traces


def read_one_file(
    input_path: Path, func: Callable[[pd.Series], pd.Series], index: str = "PC"
):
    df = pd.read_fwf(input_path, header=0, infer_nrows=100000)
    result = df.apply(func, axis=1)
    result = result.set_index(index)
    return result


def read_one_csv(input_path: Path, func: Callable[[pd.Series], pd.Series]):
    df = pd.read_csv(input_path, header=0, index_col=0)
    result = df.apply(func, axis=1)
    return result


def cal_average_variance(line: pd.Series) -> pd.Series:
    row = line[0].split(",")
    pc = row[0]
    # First entry of reuse distance is always 0, which should not be considered.
    data = np.array(list(map(lambda x: int(x, 16), row[5:])), dtype=int)
    mean = np.mean(data)
    return pd.Series([pc, mean], index=["PC", "ReuseMean"])


def get_corr_raw_data(line: pd.Series) -> pd.Series:
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
        [pc, distance, branch_type, hit_access, hwc_type],
        index=["PC", "Distance", "BranchType", "HitAccess", "HWCType"],
    )


def get_branch_bias(line: pd.Series) -> pd.Series:
    return pd.Series(
        [int(line["Taken"], 16) / (int(line["Taken"], 16) + int(line["NotTaken"], 16))],
        index=["Bias"],
    )


def cramers_corrected_stat(confusion_matrix):
    """calculate Cramers V statistic for categorial-categorial association.
    uses correction from Bergsma and Wicher,
    Journal of the Korean Statistical Society 42 (2013): 323-328
    """
    chi2 = ss.chi2_contingency(confusion_matrix)[0]
    n = confusion_matrix.sum().sum()
    phi2 = chi2 / n
    r, k = confusion_matrix.shape
    phi2corr = max(0, phi2 - ((k - 1) * (r - 1)) / (n - 1))
    rcorr = r - ((r - 1) ** 2) / (n - 1)
    kcorr = k - ((k - 1) ** 2) / (n - 1)
    return np.sqrt(phi2corr / min((kcorr - 1), (rcorr - 1)))


def correlation_ratio(categories, values):
    cat = np.unique(categories, return_inverse=True)[1]
    values = np.array(values)

    ssw = 0
    ssb = 0
    for i in np.unique(cat):
        subgroup = values[np.argwhere(cat == i).flatten()]
        ssw += np.sum((subgroup - np.mean(subgroup)) ** 2)
        ssb += len(subgroup) * (np.mean(subgroup) - np.mean(values)) ** 2

    return (ssb / (ssb + ssw)) ** 0.5


def one_file_corr(hwc_path: Path, bias_path: Path, reuse_path: Path, columns: list):
    hwc_df = read_one_file(hwc_path, get_corr_raw_data)

    bias_df = read_one_csv(bias_path, get_branch_bias)

    reuse_df = read_one_file(reuse_path, cal_average_variance)
    reuse_df.fillna(reuse_df["ReuseMean"].max() + 1, inplace=True)
    #
    # df = pd.concat([hwc_df, bias_df, reuse_df], axis=1)
    # print(df)

    df = hwc_df.merge(bias_df, left_index=True, right_index=True)
    df = df.merge(reuse_df, left_index=True, right_index=True)
    # df.to_csv("/home/shixinsong/Desktop/UM/SURE/plots/tmp.csv")

    # Calculate Cramer's V between HWC and branch type
    confusion_matrix = pd.crosstab(df["HWCType"], df["BranchType"])
    branch_type_hwc_corr = cramers_corrected_stat(confusion_matrix)

    # Calculate correlation ratio between HWC and distance
    distance_hwc_corr = correlation_ratio(df["HWCType"], df["Distance"])

    # Calculate correlation ratio between HWC and bias
    bias_hwc_corr = correlation_ratio(df["HWCType"], df["Bias"])

    # Calculate correlation ratio between HWC and avg reuse distance
    reuse_hwc_corr = correlation_ratio(df["HWCType"], df["ReuseMean"])

    return pd.DataFrame(
        [[branch_type_hwc_corr, distance_hwc_corr, bias_hwc_corr, reuse_hwc_corr]],
        columns=columns,
        index=[hwc_path.stem],
    )


def all_file_corr(hwc_path: Path, bias_path: Path, reuse_path: Path, output_dir: Path, local: bool):
    columns = [
        "Branch type v.s. temperature",
        "Distance v.s. temperature",
        "Branch bias v.s. temperature",
        "Avg reuse distance v.s. temperature"
    ]
    if not local:
        df = pd.DataFrame(columns=columns)
        for trace in pt_traces:
            df = df.append(
                one_file_corr(
                    hwc_path / (trace + ".csv"), bias_path / (trace + ".csv"), reuse_path / (trace + ".csv"), columns
                )
            )

        print(df)
        df.to_csv(output_dir / "hwc_bias_corr.csv")
    else:
        df = pd.read_csv(output_dir / "hwc_bias_corr.csv", index_col=0, header=0)

    rename_map = {"pgbench": "postgresql", "mysqllarge": "mysql"}
    df.rename(index=rename_map, inplace=True)
    # df = df.drop(["verilator"])
    plot_functions.plot_bar_chart(
        output_path=output_dir / "hwc_bias_corr.pdf",
        df=df,
        ytitle=r"Correlation",
        two_level=False,
        add_average=False,
        figsize=(0.35, 0.9),
        legend_col_num=2,
        cut_upper_bound=1.3,
        legend_size=18,
    )
    # plt.figure(figsize=(10, 6))
    # ax = plt.gca()
    # df.plot(y=columns, ylabel="Average reuse distance variance", kind="bar", ax=ax)
    # ax.set_axisbelow(True)
    # plt.grid(axis="y")
    # ax.spines["bottom"].set_position("zero")
    # for pos in ["top", "right", "left"]:
    #     ax.spines[pos].set_visible(False)
    # plt.xticks()
    # plt.savefig(output_dir / "hwc_bias_corr.pdf")


@click.command()
@click.option(
    "--local",
    default=False,
    type=bool,
)
def main(local: bool):
    output_dir = plots_dir / "paper_results/hwc_bias_corr"
    output_dir.mkdir(exist_ok=True)
    # TODO: Check data directory
    all_file_corr(
        plots_dir / Path("opt_access_record/way4"),
        plots_dir / Path("branch_bias_record/way4"),
        plots_dir / Path("reuse_distance_taken"),
        output_dir,
        local
    )


if __name__ == "__main__":
    main()
