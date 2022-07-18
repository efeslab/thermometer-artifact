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
from tqdm import tqdm
from typing import Callable, Tuple, Optional
import scipy.stats as ss

import plot_functions


def generate_plot(input_path: Path, output_path: Path, ytitle: str):
    df = pd.read_csv(input_path, header=0, index_col=0)
    df = df.transpose()
    print(df)
    plot_functions.plot_bar_chart(
        output_path,
        df,
        xtitle=r"Dynamic execution CDF (\%)",
        ytitle=ytitle,
        xlabel_rotation=45,
        add_average=False,
        figsize=(0.4, 0.9),
        plot_type="line",
    )


def main():
    input_dir = Path("/Users/shixinsong/Desktop/plots/paper_results/CDF")
    output_dir = input_dir
    generate_plot(
        input_dir / "dynamic_execution_CDF_pt.csv",
        output_dir / "branch_index.pdf",
        "Unique taken branches",
    )
    generate_plot(
        input_dir / "CDF_hit_access_pt.csv",
        output_dir / "hit_access.pdf",
        "Hit / access ratio",
    )


if __name__ == "__main__":
    main()
