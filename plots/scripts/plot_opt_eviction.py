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
from typing import Callable, Tuple, Optional

import plot_functions
from .utils import scripts_dir, plots_dir, project_dir, pt_traces


def read_one_file(input_path: Path, func: Callable[[pd.Series], pd.Series]):
    df = pd.read_fwf(input_path, header=0, infer_nrows=100000)
    result = df.apply(func, axis=1)
    return result


def cal_hit_access(line: pd.Series) -> pd.Series:
    row = line[0].split(",")
    pc = row[0]
    data = np.array(row[3:], dtype=int)
    unique, counts = np.unique(data, return_counts=True)
    counter = dict(zip(unique, counts))
    aaa = [counter.get(i, 0) for i in range(3)]
    all_miss = counter.get(1, 0) + counter.get(2, 0)
    bypass_rate = counter.get(1, 0) / all_miss
    return pd.Series(
        [pc, aaa[0] / sum(aaa), bypass_rate], index=["PC", "HitAccess", "BypassRate"]
    )


def variance_mean(
    local: bool,
    access_record_dir: Path,
    output_path: Path,
    trace_type: str = "pt",
):
    columns = ["Cold", "Warm", "Hot"]
    if not local:
        result = pd.DataFrame(columns=columns)
        for opt_access_record_path in sorted(access_record_dir.iterdir()):
            if trace_type == "pt" and opt_access_record_path.stem not in pt_traces:
                continue
            if trace_type == "ipc1" and opt_access_record_path.stem in pt_traces:
                continue
            access_record = read_one_file(opt_access_record_path, cal_hit_access)
            cold_rate = access_record[access_record["HitAccess"] <= 0.5].mean(axis=0)[
                "BypassRate"
            ]
            warm_rate = access_record[(access_record["HitAccess"] <= 0.8) & (access_record["HitAccess"] > 0.5)].mean(axis=0)[
                "BypassRate"
            ]
            hot_rate = access_record[access_record["HitAccess"] > 0.8].mean(axis=0)[
                "BypassRate"
            ]
            df = pd.DataFrame(
                [[cold_rate, warm_rate, hot_rate]],
                columns=columns,
                index=[opt_access_record_path.stem],
            )
            result = result.append(df)

        print(result)

        result.to_csv(output_path.parent / "bypass.csv")
    else:
        result = pd.read_csv(output_path.parent / "bypass.csv", header=0, index_col=0)

    result = result * 100
    rename_map = {"pgbench": "postgresql", "mysqllarge": "mysql"}
    result.rename(index=rename_map, inplace=True)
    # result = result.drop(["verilator"])
    plot_functions.plot_bar_chart(
        output_path,
        result,
        ytitle=r"Bypass (\%)",
        figsize=(0.32, 0.9),
    )


@click.command()
@click.option(
    "--local",
    default=False,
    type=bool,
)
def main(local: bool):
    access_record_dir = plots_dir / Path("opt_access_record/way4")
    output_dir = plots_dir / Path("paper_results/bypass")
    output_dir.mkdir(exist_ok=True)
    variance_mean(local=local, access_record_dir=access_record_dir, output_path=output_dir / "bypass.pdf")


if __name__ == "__main__":
    main()
