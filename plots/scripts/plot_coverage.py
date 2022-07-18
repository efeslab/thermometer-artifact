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

import plot_functions
from .utils import scripts_dir, plots_dir, project_dir, pt_traces


def cal_coverage(line: pd.Series) -> pd.Series:
    coverage = 100 * line["not the same"] / sum(list(line))
    return pd.Series([coverage], index=["Coverage"])


def main():
    input_dir = plots_dir / "hwc_opt_compare"
    output_dir = plots_dir / "paper_results/coverage"
    output_dir.mkdir(exist_ok=True)
    input_path = (
        input_dir
        / "hwc_opt_ChampSim_fdip_hwc_50_80_f_keep_curr_hotter_lru4_all_same_type_pt.csv"
    )
    df = pd.read_csv(input_path, header=0, index_col=0)
    print(df)
    df = df.apply(cal_coverage, axis=1)
    print(df)
    print(df.mean())
    df.to_csv(output_dir / "coverage.csv")

    rename_map = {"pgbench": "postgresql", "mysqllarge": "mysql"}
    df.rename(index=rename_map, inplace=True)
    # df = df.drop(["verilator"])

    plot_functions.plot_bar_chart(
        output_path=output_dir / "coverage.pdf",
        df=df,
        ytitle=r"Coverage (\%)",
        two_level=False,
        add_average=True,
        show_legend=False,
        figsize=(0.32, 0.9),
    )


if __name__ == "__main__":
    main()
