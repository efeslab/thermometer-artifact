import math
import re
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib import colors
from matplotlib.ticker import PercentFormatter

pt = True
# big_data_dir = "/mnt/storage/shixins/champsim_pt/correct_new_btb_fdip_result"
big_data_dir = (
    "/mnt/storage/shixins/champsim_pt/pt_result"
    if pt
    else "/mnt/storage/shixins/champsim_pt/correct_new_btb_fdip_result"
)


def get_data_value(input_path: Path, output_csv, types):
    with input_path.open(mode="r") as input_file:
        filename = input_path.name
        trace = filename.split(".")[0]
        output_csv.write(trace)
        s = input_file.read()
        for t in types:
            line = re.search("Evict %s: .*\n" % t, s).group(0).rstrip()
            evict_part, average_candidate_part = line.split("\t")
            evict_num = evict_part.split(" ")[-1]
            average_candidate = average_candidate_part.split(" ")[-1]
            output_csv.write(",%s,%s" % (evict_num, average_candidate))
        output_csv.write("\n")


def generate_csv(program_name: str, output_dir: Path):
    input_dir = (
        Path(big_data_dir) / ("%s_result" % program_name)
        if pt
        else Path(big_data_dir) / ("ipc1_fdip_%s_result" % program_name)
    )
    output_path = (
        output_dir / ("%s_pt.csv" % program_name)
        if pt
        else output_dir / ("%s.csv" % program_name)
    )
    types = ["cold", "warm not taken", "warm taken", "hot"]
    with output_path.open(mode="w") as output_csv:
        output_csv.write("Trace," + ",,".join(types) + "\n")
        for input_path in sorted(input_dir.iterdir()):
            get_data_value(input_path, output_csv, types)


def main():
    program_names = [
        "ChampSim_fdip_hot_warm_cold_80_50_f_keep",
        "ChampSim_fdip_hot_warm_cold_80_25_f_keep",
    ]
    output_dir = Path("/mnt/storage/shixins/champsim_pt/hot_warm_cold_count")
    output_dir.mkdir(exist_ok=True)
    for program_name in program_names:
        generate_csv(program_name, output_dir)


if __name__ == "__main__":
    main()
