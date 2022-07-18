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
import scipy.stats as ss
from matplotlib.patches import Ellipse

import plot_functions

from .utils import scripts_dir, plots_dir, project_dir


text_color = "blue"

rc("font", **{"size": "23", "family": "serif", "serif": ["Palatino"]})
rc("text", usetex=True, **{'color': text_color})
rc("axes", **{"labelcolor": text_color})
rc("lines", **{"color": text_color})
rc("xtick", **{"color": text_color})
rc("ytick", **{"color": text_color})

hatches = ["/", ".", "x", "\\", "+"]
line_styles = ["-", "--", "-.", ":"]
bar_colors = ["#5BB8D7", "#57A86B", "#A8A857", "#6E4587", "#ADEBCC", "#EBDCAD"]


def output_filename(output_path: Path):
    if text_color != "blue":
        return output_path
    parent_dir = output_path.parent
    filename_stem = output_path.stem
    filename_suffix = output_path.suffix
    return parent_dir / f"blue_{filename_stem}{filename_suffix}"


def cal_percentage_branch(line: pd.Series) -> pd.Series:
    # keys = ["SRRIP", "GHRP", "Hawkeye", "Thermometer-random", "Thermometer-LRU", "OPT"]
    result = []
    new_index = list(line.index)
    # new_index.remove("100")
    for key in new_index:
        result.append(100 * line[key] / line["100"])
    return pd.Series(result, index=new_index)


def old_plots():
    chosen_list = [
        # "cassandra",
        # "clang",
        "drupal",
        # "finagle-chirper",
        # "finagle-http",
        "kafka",
        # "mediawiki",
        # "mysqllarge",
        # "pgbench",
        # "python",
        # "tomcat",
        "verilator",
        # "wordpress",
    ]
    data_dir = plots_dir / "paper_results/CDF"
    hit_access_df = pd.read_csv(
        data_dir / "CDF_hit_access_pt.csv", header=0, index_col=0
    ).transpose()
    hit_access_df = hit_access_df * 100

    branch_count_df = pd.read_csv(
        data_dir / "dynamic_execution_CDF_pt.csv", header=0, index_col=0
    )
    branch_count_df = branch_count_df.apply(cal_percentage_branch, axis=1)
    branch_count_df = branch_count_df.transpose()
    plt.figure(figsize=figaspect(0.4 / 0.9))
    for i, column in enumerate(chosen_list):
        plt.plot(
            branch_count_df[column],
            list(map(lambda x: float(x), branch_count_df.index)),
            label=column,
            color=bar_colors[i % len(bar_colors)],
            linestyle=line_styles[i % len(line_styles)],
        )
    ax = plt.gca()
    plt.ylabel(r"Dynamic execution CDF (\%)")
    plt.xlabel(r"\% of all unique taken branches")
    plt.legend()

    hot_ellipse = Ellipse((25, 60), 55, 90, angle=0, alpha=0.1)
    ax.add_artist(hot_ellipse)
    hot_ellipse.set_facecolor("red")
    warm_ellipse = Ellipse((62.5, 85), 24, 40, angle=0, alpha=0.1)
    ax.add_artist(warm_ellipse)
    warm_ellipse.set_facecolor("orange")
    cold_ellipse = Ellipse((87.5, 95), 25, 20, angle=0, alpha=0.1)
    ax.add_artist(cold_ellipse)

    words = ["hot", "warm", "cold"]
    pos = [25, 62.5, 87.5]
    height = [60, 85, 95]
    for i in range(len(words)):
        plt.text(pos[i], height[i], words[i], ha="center")

    plt.tight_layout()
    plt.savefig(output_filename(data_dir / "cdf_count.pdf"), bbox_inches='tight', pad_inches=0)

    df = branch_count_df.merge(hit_access_df, left_index=True, right_index=True)
    # print(df)
    columns = hit_access_df.columns
    plt.figure(figsize=figaspect(0.4 / 0.9))
    for i, column in enumerate(chosen_list):
        plt.plot(
            df[column + "_x"],
            df[column + "_y"],
            label=column,
            color=bar_colors[i % len(bar_colors)],
            linestyle=line_styles[i % len(line_styles)],
        )
    ax = plt.gca()
    plt.ylabel(r"Hit-to-taken (\%)")
    plt.xlabel(r"\% of all unique taken branches")
    plt.legend()

    hot_ellipse = Ellipse((25, 90), 50, 25, angle=0, alpha=0.1)
    ax.add_artist(hot_ellipse)
    hot_ellipse.set_facecolor("red")
    warm_ellipse = Ellipse((62.5, 70), 45, 25, angle=-70, alpha=0.1)
    ax.add_artist(warm_ellipse)
    warm_ellipse.set_facecolor("orange")
    cold_ellipse = Ellipse((85, 25), 35, 55, angle=0, alpha=0.1)
    ax.add_artist(cold_ellipse)
    # cold_ellipse.set_facecolor("cold")

    offsets = [0, 50, 75, 100]
    words = ["hot", "warm", "cold"]
    pos = [25, 62.5, 85]
    height = [90, 70, 25]
    for i in range(len(words)):
        plt.text(pos[i], height[i], words[i], ha="center")

    plt.tight_layout()
    # plt.xscale("log")

    # print(hit_access_df)
    # print(branch_count_df)
    plt.savefig(output_filename(data_dir / "hit_taken_count.pdf"), bbox_inches='tight', pad_inches=0)
    plt.close()


def cal_hit_access(line: pd.Series) -> pd.Series:
    row = line[0].split(",")
    pc = row[0]
    data = np.array(row[3:], dtype=int)
    unique, counts = np.unique(data, return_counts=True)
    counter = dict(zip(unique, counts))
    aaa = [counter.get(i, 0) for i in range(3)]
    return pd.Series(
        [pc, aaa[0] / sum(aaa), sum(aaa)], index=["PC", "HitAccess", "Count"]
    )


def main():
    old_plots()


if __name__ == "__main__":
    main()
