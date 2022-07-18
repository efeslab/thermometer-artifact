import math
import re
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib import colors
from matplotlib.ticker import PercentFormatter

big_data_dir = "/mnt/storage/shixins/champsim_pt/correct_new_btb_fdip_result"
big_plot_dir = "/mnt/storage/shixins/champsim_pt/access_counter_plot"

chosen_benchmarks = [
    "spec_gobmk_002",
    "server_011",
    "spec_gobmk_001",
    "server_012",
    "server_013",
    "client_004",
    "server_003",
    "server_027",
    "server_026",
    "server_023",
    "server_025",
    "server_024",
    "server_034",
    "server_004",
    "server_010",
    "server_016",
    "server_033",
    "client_008",
    "client_002",
    "spec_gcc_001",
]

partial_generate = True
calculate_percent = True
plot_type_or_distance = False
only_plot = True


def plot(content: str, filename: str):
    lines = (content.split("!"))[2:-1]
    all_set = []
    for line in lines:
        words = line.split(",")
        num = words[0]
        counts = words[1]
        # num, counts = line.split(',')
        num = int(num)
        if num >= 200:
            break
        counts = int(counts)
        for _ in range(counts):
            all_set.append(num)

    plt.figure()
    plt.hist(x=all_set, bins=200)
    plt.savefig(filename)
    plt.close()


def plot_branch_type(content: str, filename: str):
    num_bars = 100
    lines = (content.split("!"))[2:-1]
    bar_data = [([0.0] * num_bars) for _ in range(3)]
    for line in lines:
        data_line = line.split(",")
        num = int(data_line[0])
        if num >= num_bars:
            break
        for i in range(len(bar_data)):
            if calculate_percent:
                bar_data[i][num] = float(data_line[i + 2]) / float(data_line[1])
            else:
                bar_data[i][num] = float(data_line[i + 2])
    plt.figure()
    # print(bar_data[0])
    # print(bar_data[1])
    plt.bar(
        x=range(num_bars),
        height=bar_data[1],
        width=1,
        align="edge",
        label="BRANCH_CONDITIONAL",
    )
    plt.bar(
        x=range(num_bars),
        height=bar_data[0],
        width=1,
        bottom=bar_data[1],
        align="edge",
        label="BRANCH_DIRECT_JUMP",
    )
    partial_sum = []
    for i in range(len(bar_data[0])):
        partial_sum.append(bar_data[0][i] + bar_data[1][i])
    plt.bar(
        x=range(num_bars),
        height=bar_data[2],
        width=1,
        bottom=partial_sum,
        align="edge",
        label="BRANCH_DIRECT_CALL",
    )
    plt.legend()
    plt.savefig(filename)
    plt.close()


def plot_distance_type(content: str, filename: str):
    num_bars = 100
    lines = (content.split("!"))[2:-1]
    bar_data = [([0.0] * num_bars) for _ in range(2)]
    for line in lines:
        data_line = line.split(",")
        num = int(data_line[0])
        if num >= num_bars:
            break
        for i in range(len(bar_data)):
            if calculate_percent:
                bar_data[i][num] = float(data_line[i + 5]) / float(data_line[1])
            else:
                bar_data[i][num] = float(data_line[i + 5])
    plt.figure()
    # print(bar_data[0])
    # print(bar_data[1])
    plt.bar(x=range(num_bars), height=bar_data[0], width=1, align="edge", label="Small")
    plt.bar(
        x=range(num_bars),
        height=bar_data[1],
        width=1,
        bottom=bar_data[0],
        align="edge",
        label="Large",
    )
    plt.legend()
    plt.savefig(filename)
    plt.close()


def main(dir_name):
    print(dir_name)
    partial_name = "_percent" if calculate_percent else "_count"
    type_or_distance = "_type" if plot_type_or_distance else "_distance"
    data_dir = Path(big_data_dir) / (dir_name + "_result")
    plot_dir = Path(big_plot_dir) / (
        dir_name + partial_name + type_or_distance + "_plot"
    )
    plot_dir.mkdir(exist_ok=True, parents=True)
    for file in sorted(data_dir.iterdir()):
        filename = file.name
        trace = filename.split(".")[0]
        if not partial_generate or trace not in chosen_benchmarks:
            continue
        lines = file.open().read().split("\n")
        s = "!".join(lines)
        content = re.search(
            "BTB Access Count Histogram.*End BTB Access Count Histogram", s
        ).group(0)
        if only_plot:
            plot(content, str(plot_dir / ("%s.pdf" % trace)))
            continue
        if plot_type_or_distance:
            plot_branch_type(content, str(plot_dir / ("%s.pdf" % trace)))
        else:
            plot_distance_type(content, str(plot_dir / ("%s.pdf" % trace)))


if __name__ == "__main__":
    # list_of_dir_names = ["ipc1_fdip_srrip", "ipc1_fdip_srrip_friendly", "ipc1_fdip_ghrp", "ipc1_fdip_opt",
    #                      "ipc1_fdip_count_clear", "ipc1_fdip_count_clear_upper", "ipc1_fdip_count_clear_update"]
    list_of_dir_names = ["ipc1_fdip_srrip_second_small"]
    for dir_name in list_of_dir_names:
        main(dir_name)
