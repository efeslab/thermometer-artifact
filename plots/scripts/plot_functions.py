import asyncio
import enum
import math
import pathlib
from functools import wraps
from pathlib import Path
import re
import statistics
import math

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

from .utils import scripts_dir, plots_dir, project_dir, pt_traces

text_color = "black"

rc("font", **{"size": "23", "family": "serif", "serif": ["Palatino"]})
rc("text", usetex=True, **{'color': text_color})
rc("axes", **{"labelcolor": text_color})
rc("lines", **{"color": text_color})
rc("xtick", **{"color": text_color})
rc("ytick", **{"color": text_color})

hatches = ["/", ".", "x", "\\", "+"]
line_styles = ["-", "--", "-.", ":"]
bar_colors = ["#5BB8D7", "#57A86B", "#A8A857", "#6E4587", "#ADEBCC", "#EBDCAD"]
# bar_colors = ["#5BB8D7", "#AF2002", "#AF7B02", "#21AF02", "#02A2AF", "#5102AF", "#AF0236"]


def output_filename(output_path: Path):
    if text_color != "blue":
        return output_path
    parent_dir = output_path.parent
    filename_stem = output_path.stem
    filename_suffix = output_path.suffix
    return parent_dir / f"blue_{filename_stem}{filename_suffix}"


def plot_bar_chart(
        output_path: Path,
        df: pd.DataFrame,
        xtitle: str = "",
        ytitle: str = "",
        xlabel_rotation: int = 35,
        show_legend: bool = True,
        legend_col_num: int = 3,
        add_average: bool = True,
        cut_upper_bound: float = 0,
        cut_lower_bound: float = 0,
        figsize: Optional[Tuple[float, float]] = None,
        two_level: bool = False,
        second_level_y_pos: float = 0.15,
        second_level_rotation: int = 0,
        plot_type: str = "bar",
        two_column: float = 1,
        extra_label_size: int = 10,
        legend_size: int = 20,
        stacked: bool = False,
        cut_off_at_zero: bool = False,
        add_avg_without_verilator: bool = False,
        x_scale: str = "linear",
        y_scale: str = "linear",
):
    """Plot single level bar charts with the same style"""

    if two_level:
        apps = sorted(list(set(map(lambda x: x.split(";")[0], df.index))))
        app_pos = [0] * len(apps)
        num_per_app = len(df.index) / len(apps)
        total_bar_per_app = num_per_app * len(df.columns)
        df.index = pd.Index(list(map(lambda x: x.split(";")[1], df.index)))

    if add_avg_without_verilator:
        df = df.append(
            pd.DataFrame(
                [df.drop(["verilator"]).mean(axis=0), df.mean(axis=0)],
                columns=df.columns,
                index=[r"\textbf{Avg no verilator}", r"\textbf{Avg}"]
            )
        )
    elif add_average:
        df = df.append(
            pd.DataFrame([df.mean(axis=0)], columns=df.columns, index=[r"\textbf{Avg}"])
        )

    if figsize:
        plt.figure(figsize=two_column * figaspect(figsize[0] / figsize[1]))
    else:
        plt.figure(figsize=two_column * figaspect(0.4 / 0.9))
    ax = plt.gca()
    if plot_type == "bar":
        df.plot(
            y=df.columns,
            xlabel=xtitle,
            ylabel=ytitle,
            kind=plot_type,
            ax=ax,
            legend=False,
            fill=False,
            width=0.6,
            stacked=stacked
        )
        bars = ax.patches
        bar_width = 0
        for i, bar in enumerate(bars):
            bar_width = bar.get_width()
            bar.set_hatch(hatches[(i // len(df)) % len(hatches)] * 5)
            bar.set_edgecolor(bar_colors[(i // len(df)) % len(bar_colors)])
            if two_level:
                app_index = int((i % len(df)) // num_per_app)
                if app_index < len(app_pos):
                    app_pos[app_index] += bar.get_x()
            # print(bar)
            if 0 < cut_upper_bound < bar.get_height():
                plt.text(
                    bar.get_x() + bar.get_width() / 2,
                    cut_upper_bound * 1.05,
                    "{0:.0f}".format(bar.get_height()),
                    rotation="vertical",
                    ha="center",
                    fontsize=extra_label_size
                    # va="center",
                )
    else:
        df.plot(
            y=df.columns,
            xlabel=xtitle,
            ylabel=ytitle,
            kind=plot_type,
            ax=ax,
            legend=False,
            stacked=stacked
        )
        lines = ax.lines
        for i, line in enumerate(lines):
            line.set_linestyle(line_styles[i % len(line_styles)])
            line.set_color(bar_colors[i % len(bar_colors)])

    if two_level:
        app_pos = list(map(lambda x: x / total_bar_per_app, app_pos))
        assert len(app_pos) == len(apps)
        for i, app in enumerate(apps):
            plt.text(
                app_pos[i] + bar_width / 2,
                second_level_y_pos,
                app,
                ha="center",
                va="top",
                rotation=second_level_rotation,
            )
    if cut_upper_bound > 0:
        ax.set_ylim(ymax=cut_upper_bound)
    if cut_lower_bound < 0:
        ax.set_ylim(ymin=cut_lower_bound)
    if show_legend:
        plt.legend(
            ncol=legend_col_num, columnspacing=0.5, fontsize="small", prop={"size": legend_size}
        )
    plt.grid(linestyle="--", zorder=0)
    if plot_type == "bar":
        plt.xticks(rotation=xlabel_rotation, ha="right", rotation_mode='anchor')
    # else:
    #     plt.xticks(range(len(list(df.index))), list(df.index), rotation=xlabel_rotation)
    if cut_off_at_zero:
        ax.set_xlim(xmin=0)
        ax.set_ylim(ymin=0)
    plt.tight_layout()
    # plt.xscale(x_scale)
    if y_scale == "symlog":
        plt.yscale(y_scale, linthresh=0.015)
    plt.savefig(output_filename(output_path), bbox_inches='tight', pad_inches=0)
    plt.close()


def remove_index_suffix(df: pd.DataFrame, suffix: str):
    new_index = []
    for row in df.index:
        if row.endswith(suffix):
            row = row[: -len(suffix)]
        new_index.append(row)
    df.index = pd.Index(new_index)


def main():
    input_dir = plots_dir / "simulation_result/pt_result_analysis"
    output_dir = plots_dir / "paper_results"
    input_path = input_dir / "ChampSim_fdip_chubby_lru4.csv"
    df = pd.read_csv(input_path, header=0, index_col=0)
    print(df)
    rename_map = {"pgbench": "postgresql", "mysqllarge": "mysql"}
    app_list = pt_traces
    remove_index_suffix(df, "_result")

    def cal_log_of_data(line: pd.Series) -> pd.Series:
        result = [math.log2(line["L2C MPKI"])]
        return pd.Series(result, index=["L2C MPKI log"])

    df = df.loc[app_list]
    df.rename(index=rename_map, inplace=True)
    df = df[["L2C MPKI"]]
    print(df)
    # log_df = df.apply(cal_log_of_data, axis=1)
    # print(log_df)
    plot_bar_chart(
        output_path=output_dir / "l2_mpki_chubby_log.pdf",
        df=df,
        ytitle=r"L2iMPKI (log-10 scale)",
        two_level=False,
        add_average=False,
        figsize=(0.35, 0.9),
        # cut_upper_bound=10,
        show_legend=False,
        extra_label_size=15,
        y_scale="symlog",
        # plot_type="line",
    )
    # remove_index_suffix(df, "_result")
    # plot_bar_chart(
    #     Path("/Users/shixinsong/Desktop/plots/try.pdf"),
    #     df,
    #     r"\% Speedup over FDIP",
    #     cut_upper_bound=20,
    #     legend_col_num=4
    # )
    # print(df)


if __name__ == "__main__":
    main()
