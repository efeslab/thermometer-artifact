from pathlib import Path
import pandas as pd
import re
import click
import numpy as np
from typing import Callable

from matplotlib.figure import figaspect

import plot_functions
from matplotlib import pyplot as plt


def plot_side_by_side(df1: pd.DataFrame, df2: pd.DataFrame):
    pass


def parse_old_csv(input_path: Path, app_list: list):
    original_df = pd.read_csv(input_path, header=0, index_col=0)
    if "SRRIP" in original_df.columns:
        original_df.drop(columns=["SRRIP"], inplace=True)
    assert len(original_df.columns) == 1
    df_list = []
    for app in app_list:
        new_df = original_df[original_df.index.str.contains(app)]
        rename_map = {}
        for x in new_df.index:
            rename_map[x] = x.split(";")[1]
        new_df = new_df.rename(rename_map)
        new_df = new_df.rename({"Thermometer": app}, axis=1)
        df_list.append(new_df)
    result = pd.concat(df_list, axis=1)
    return result


# def parse_old_csv(input_path: Path, app_list: list):
#     original_df = pd.read_csv(input_path, header=0, index_col=0)



def main():
    output_dir = Path("/home/shixinsong/Desktop/UM/SURE/plots/paper_results/sensitivity")
    output_dir.mkdir(exist_ok=True)
    way_speedup_df = parse_old_csv(
        Path("/home/shixinsong/Desktop/UM/SURE/plots/paper_results/btb_way/way_speedup.csv"),
        ["cassandra", "drupal", "tomcat"]
    )
    size_speedup_df = parse_old_csv(
        Path("/home/shixinsong/Desktop/UM/SURE/plots/paper_results/btb_size/percentage_opt_ipc_speedup_size.csv"),
        ["cassandra", "drupal", "tomcat"]
    )
    print(way_speedup_df)
    print(size_speedup_df)
    figsize = figaspect(3.5 / 9)
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=figsize)
    params = {
        "style": ["o-", "s-", "^-", "o--", "s--", "^--"],
        "color": ["#5BB8D7", "#57A86B", "#A8A857", "#5BB8D7", "#57A86B", "#A8A857"],
        "grid": True,
        "linewidth": 2.5,
        "markersize": 8,
    }
    way_speedup_df.plot(
        ax=ax1,
        legend=False,
        xlabel="\# of BTB ways",
        ylabel="\% of optimal policy speedup",
        xticks=range(len(list(way_speedup_df.index))),
        # xtickslabel=list(way_speedup_df.index),
        **params
    )
    # ax1.xticks(range(len(list(way_speedup_df.index))), list(way_speedup_df.index))
    size_speedup_df.plot(
        ax=ax2,
        legend=False,
        xlabel="\# of BTB Entries",
        # ylabel="\% of optimal policy speedup",
        xticks=range(len(list(size_speedup_df.index))),
        **params
    )
    plt.legend(
        ncol=1,
        columnspacing=0.5,
        fontsize="small",
        prop={"size": 20},
        # loc="center right",
        # bbox_to_anchor=(1.5, 0.5),
    )
    plt.tight_layout()
    # plt.show()
    plt.savefig(output_dir / "btb_sensitivity.pdf")


if __name__ == '__main__':
    main()
