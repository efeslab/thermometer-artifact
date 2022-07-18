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

workers = 75
count = 0
TIMEOUT = 36000

pt_traces = [
    "cassandra",
    "drupal",
    "finagle-chirper",
    "finagle-http",
    "kafka",
    "mediawiki",
    "tomcat",
    "verilator",
    "wordpress",
]


async def run_stack_simulator(trace_dir: Path, output_dir: Path):
    for trace in pt_traces:
        trace_path = trace_dir / trace / "trace.gz"
        running_dir = output_dir / trace
        running_dir.mkdir(exist_ok=True)
        command_str = (
            "zcat "
            + str(trace_path)
            + "|/mnt/storage/shixins/icache_profiling/third_party/stack-distance/stack-distance"
        )
        global workers, count
        while workers <= 0:
            await asyncio.sleep(1)

        workers -= 1
        p = None
        try:
            p = await asyncio.create_subprocess_shell(command_str, cwd=running_dir)
            await asyncio.wait_for(p.communicate(), timeout=TIMEOUT)
        except asyncio.TimeoutError:
            print("Error: Timeout!")
        try:
            p.kill()
        except:
            pass

        workers += 1
        count += 1
        print("Finish program %s No. %s" % (trace, count))


async def main():
    output_dir = Path(
        "/mnt/storage/shixins/champsim_pt/pt_instruction_footprint_raw_data"
    )
    output_dir.mkdir(exist_ok=True)
    await run_stack_simulator(
        Path("/mnt/storage/shixins/champsim_pt/pt_traces"), output_dir
    )


if __name__ == "__main__":
    asyncio.run(main())
