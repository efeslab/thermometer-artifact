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


def cal_overhead(line: pd.Series) -> pd.Series:
    instruction_set_size = line["0.99"] * 64 / 1024
    additional_bit_size = line["num branches"] * 2 / (8 * 1024)
    return pd.Series(
        [100 * additional_bit_size / instruction_set_size], index=["Overhead"]
    )


def main():
    overhead_dir = Path("/Users/shixinsong/Desktop/plots/paper_results/overhead")
    input_file = overhead_dir / "overhead_raw_data.csv"
    # output_csv = overhead_dir / "overhead.csv"
    output_pdf = overhead_dir / "overhead.pdf"

    df = pd.read_csv(input_file, index_col=0, header=0)
    df = df.apply(cal_overhead, axis=1)
    df = df.sort_index()
    print(df)

    plot_functions.plot_bar_chart(
        output_path=output_pdf,
        df=df,
        ytitle=r"Overhead (\%)",
        two_level=False,
        add_average=True,
        show_legend=False,
        figsize=(0.3, 0.9),
    )


if __name__ == "__main__":
    main()
