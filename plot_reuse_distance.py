import math
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib import colors
from matplotlib.ticker import PercentFormatter

reuse_distance_dirname = "/mnt/storage/shixins/champsim_pt/reuse_distance_taken/"
result_dir = "/mnt/storage/shixins/champsim_pt/reuse_distance_friendly_plot"

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

branch_name = [
    "BRANCH_NOT_BRANCH",
    "BRANCH_DIRECT_JUMP",
    "BRANCH_INDIRECT",
    "BRANCH_CONDITIONAL",
    "BRANCH_DIRECT_CALL",
    "BRANCH_INDIRECT_CALL",
    "BRANCH_RETURN",
    "BRANCH_OTHER",
]


def get_range_name(index: int, total: int):
    if index == 0:
        return "0"
    if index + 1 < total:
        return "[%s, %s)" % (4 ** (index - 1), 4 ** index)
    else:
        return "[%s, inf)" % (4 ** (index - 1))


def plot_friendly_unfriendly(data: list, title: str, output_dir: str):
    plt.figure()
    style = [".", "*", "."]
    for i, y_data in enumerate(data):
        x_data = range(len(y_data))
        # if i == 0:
        #     continue
        plt.plot(
            x_data,
            y_data,
            label=get_range_name(i, len(data)),
            marker=style[i],
            markersize=1,
            linestyle="",
        )
    plt.xlabel("Branches")
    plt.ylabel("Reuse Distance Count")
    plt.title(title)
    plt.legend()
    output_path = Path(result_dir) / output_dir
    output_path.mkdir(exist_ok=True)
    plt.savefig(str(output_path / ("%s.pdf" % title)))
    plt.close()


def count(vec: list, bin_num: int):
    result = [0] * (bin_num + 1)
    for s in vec:
        if int(s, 16) == 0:
            result[0] += 1
            continue
        length = int(math.log(int(s, 16), 4))
        if length >= bin_num:
            result[bin_num] += 1
        else:
            result[length + 1] += 1
    return result


def branch_type_classify(trace: str, num_bins: int):
    filename = Path(reuse_distance_dirname) / ("%s.champsimtrace.xz.csv" % trace)
    counts = {
        "1": [[] for _ in range(num_bins + 1)],
        "3": [[] for _ in range(num_bins + 1)],
        "4": [[] for _ in range(num_bins + 1)],
    }
    with filename.open(mode="r") as file:
        if not file:
            print("cannot open")
            return
        file.readline()
        while True:
            line = file.readline().rstrip()
            if not line:
                break
            all_words = line.split(",")
            # TODO: Add count according to branch_type
            line_count = count(all_words[4:], num_bins)
            for i, num in enumerate(line_count):
                counts[all_words[2]][i].append(num)
    for key, value in counts.items():
        plot_friendly_unfriendly(value, "%s_%s" % (trace, branch_name[int(key)]), trace)


def branch_target_histogram(trace: str, num_bins: int):
    filename = Path(reuse_distance_dirname) / ("%s.champsimtrace.xz.csv" % trace)
    data_set = []
    with filename.open(mode="r") as file:
        if not file:
            print("cannot open")
            return
        file.readline()
        small = 0
        large = 1
        while True:
            line = file.readline().rstrip()
            if not line:
                break
            all_words = line.split(",")
            ip = int(all_words[0], 16)
            target = int(all_words[1], 16)
            distance = ip - target if ip >= target else target - ip
            if distance < 4096:
                data_set.append(distance)
                small += 1
            else:
                large += 1
    print(small, large)
    plt.figure()
    plt.hist(x=data_set, bins=200)
    output_path = Path(result_dir) / trace
    output_path.mkdir(exist_ok=True)
    plt.savefig(str(output_path / ("%s_target_distance.pdf" % trace)))
    plt.close()


def branch_target_classify(trace: str, num_bins: int):
    filename = Path(reuse_distance_dirname) / ("%s.champsimtrace.xz.csv" % trace)
    counts = [[[] for _ in range(num_bins + 1)], [[] for _ in range(num_bins + 1)]]
    with filename.open(mode="r") as file:
        if not file:
            print("cannot open")
            return
        file.readline()
        while True:
            line = file.readline().rstrip()
            if not line:
                break
            all_words = line.split(",")
            ip = int(all_words[0], 16)
            target = int(all_words[1], 16)
            distance = ip - target if ip >= target else target - ip
            line_count = count(all_words[4:], num_bins)
            for i, num in enumerate(line_count):
                if distance < 2 ** 14:
                    counts[0][i].append(num)
                else:
                    counts[1][i].append(num)
    plot_friendly_unfriendly(counts[0], "%s_%s" % (trace, "small"), trace)
    plot_friendly_unfriendly(counts[1], "%s_%s" % (trace, "large"), trace)


def no_classify(trace: str, num_bins: int):
    filename = Path(reuse_distance_dirname) / ("%s.champsimtrace.xz.csv" % trace)
    counts = [[] for _ in range(num_bins + 1)]
    with filename.open(mode="r") as file:
        if not file:
            print("cannot open")
            return
        file.readline()
        while True:
            line = file.readline().rstrip()
            if not line:
                break
            words = line.split(",")[4:]
            line_count = count(words, num_bins)
            for i, num in enumerate(line_count):
                counts[i].append(num)
    print("Before plot")
    plot_friendly_unfriendly(counts, trace, trace)


def friendly_range_percentage(distances: list, num_ways: int):
    last_is_zero = False
    friendly_num = 0
    unfriendly_num = 0
    for distance in distances:
        dist = int(distance, 16)
        if dist == 0:
            friendly_num += 0 if last_is_zero else 1
            last_is_zero = True
        elif dist < num_ways:
            friendly_num += 1
            last_is_zero = False
        else:
            unfriendly_num += 1
            last_is_zero = False
    return float(friendly_num) / (float(friendly_num) + float(unfriendly_num))


def friendly_single_percentage(distances: list, num_ways: int):
    friendly_num = 0
    unfriendly_num = 0
    for distance in distances:
        dist = int(distance, 16)
        if dist < num_ways:
            friendly_num += 1
        else:
            unfriendly_num += 1
    return float(friendly_num) / (float(friendly_num) + float(unfriendly_num))


def judge_consider_or_not(which_type: str, size: int, info: list):
    if which_type == "all":
        return True
    ip = int(info[0], 16)
    target = int(info[1], 16)
    distance = ip - target if ip >= target else target - ip
    if which_type == "large":
        return distance >= size
    elif which_type == "small":
        return distance < size
    return which_type == branch_name[int(info[2], 16)]


def plot_friendly_histogram(
    trace: str,
    num_ways: int,
    judge_friendly: list,
    csv_output,
    out_sub_dir: str,
    range_count: bool,
):
    filename = Path(reuse_distance_dirname) / ("%s.champsimtrace.xz.csv" % trace)
    data_count = {
        "all": [[0, 0] for _ in range(len(judge_friendly))],
        "BRANCH_DIRECT_JUMP": [[0, 0] for _ in range(len(judge_friendly))],
        "BRANCH_CONDITIONAL": [[0, 0] for _ in range(len(judge_friendly))],
        "BRANCH_DIRECT_CALL": [[0, 0] for _ in range(len(judge_friendly))],
        "large": [[0, 0] for _ in range(len(judge_friendly))],
        "small": [[0, 0] for _ in range(len(judge_friendly))],
    }
    with filename.open(mode="r") as file:
        if not file:
            print("cannot open")
            return
        file.readline()
        while True:
            line = file.readline().rstrip()
            if not line:
                break
            words = line.split(",")
            # friendly_count = 0
            # unfriendly_count = 0
            # for dist in words[4:]:
            #     if int(dist, 16) < num_ways:
            #         friendly_count += 1
            #     else:
            #         unfriendly_count += 1
            # friendly_percentage = float(friendly_count) / (float(friendly_count) + float(unfriendly_count))
            if range_count:
                friendly_percentage = friendly_range_percentage(words[4:], num_ways)
            else:
                friendly_percentage = friendly_single_percentage(words[4:], num_ways)
            judge = []
            for standard in judge_friendly:
                if friendly_percentage > standard:
                    # Considered as friendly
                    judge.append((1, 0))
                else:
                    judge.append((0, 1))
            for which_type, bar_count in data_count.items():
                if judge_consider_or_not(which_type, 2 ** 12, words[0:4]):
                    for i in range(len(judge_friendly)):
                        bar_count[i][0] += judge[i][0]
                        bar_count[i][1] += judge[i][1]
    percentage_data = {
        "all": [0.0 for _ in range(len(judge_friendly))],
        "BRANCH_DIRECT_JUMP": [0.0 for _ in range(len(judge_friendly))],
        "BRANCH_CONDITIONAL": [0.0 for _ in range(len(judge_friendly))],
        "BRANCH_DIRECT_CALL": [0.0 for _ in range(len(judge_friendly))],
        "large": [0.0 for _ in range(len(judge_friendly))],
        "small": [0.0 for _ in range(len(judge_friendly))],
    }
    for key, value in data_count.items():
        # print(value)
        for i in range(len(judge_friendly)):
            percentage_data[key][i] = float(value[i][0]) / (
                float(value[i][0]) + float(value[i][1])
            )
    plt.figure()
    pos = -0.25
    all_pos = range(len(judge_friendly))
    for which_type, value in percentage_data.items():
        this_pos = []
        for i in all_pos:
            this_pos.append(i + pos)
        plt.bar(this_pos, value, 0.1, label=which_type)
        pos += 0.1
    output_path = Path(result_dir)
    output_path.mkdir(exist_ok=True)
    plt.xticks(all_pos, judge_friendly)
    plt.ylabel("Percentage of friendly branches")
    plt.xlabel("Judge friendly standard")
    plt.legend()
    directory = output_path / out_sub_dir
    directory.mkdir(exist_ok=True)
    plt.savefig(str(directory / ("%s_try_friendly_hist.pdf" % trace)))
    plt.close()


def judge_detailed_consider_or_not(which_type: str, size: int, info: list):
    if which_type == "all":
        return True
    branch_type, _, distance_type = which_type.split(" ")
    ip = int(info[0], 16)
    target = int(info[1], 16)
    distance = ip - target if ip >= target else target - ip
    if distance_type == "large":
        return distance >= size and branch_type == branch_name[int(info[2], 16)]
    elif distance_type == "small":
        return distance < size and branch_type == branch_name[int(info[2], 16)]
    assert 0


def plot_detailed_friendly_histogram(
    trace: str,
    num_ways: int,
    judge_friendly: list,
    csv_output,
    out_sub_dir: str,
    range_count: bool,
):
    filename = Path(reuse_distance_dirname) / ("%s.champsimtrace.xz.csv" % trace)
    data_count = {
        "all": [[0, 0] for _ in range(len(judge_friendly))],
        "BRANCH_DIRECT_JUMP and small": [[0, 0] for _ in range(len(judge_friendly))],
        "BRANCH_DIRECT_JUMP and large": [[0, 0] for _ in range(len(judge_friendly))],
        "BRANCH_CONDITIONAL and small": [[0, 0] for _ in range(len(judge_friendly))],
        "BRANCH_CONDITIONAL and large": [[0, 0] for _ in range(len(judge_friendly))],
        "BRANCH_DIRECT_CALL and small": [[0, 0] for _ in range(len(judge_friendly))],
        "BRANCH_DIRECT_CALL and large": [[0, 0] for _ in range(len(judge_friendly))],
    }
    with filename.open(mode="r") as file:
        if not file:
            print("cannot open")
            return
        file.readline()
        while True:
            line = file.readline().rstrip()
            if not line:
                break
            words = line.split(",")
            # friendly_count = 0
            # unfriendly_count = 0
            # for dist in words[4:]:
            #     if int(dist, 16) < num_ways:
            #         friendly_count += 1
            #     else:
            #         unfriendly_count += 1
            # friendly_percentage = float(friendly_count) / (float(friendly_count) + float(unfriendly_count))
            if range_count:
                friendly_percentage = friendly_range_percentage(words[4:], num_ways)
            else:
                friendly_percentage = friendly_single_percentage(words[4:], num_ways)
            judge = []
            for standard in judge_friendly:
                if friendly_percentage > standard:
                    # Considered as friendly
                    judge.append((1, 0))
                else:
                    judge.append((0, 1))
            for which_type, bar_count in data_count.items():
                if judge_detailed_consider_or_not(which_type, 2 ** 12, words[0:4]):
                    for i in range(len(judge_friendly)):
                        bar_count[i][0] += judge[i][0]
                        bar_count[i][1] += judge[i][1]
    percentage_data = {
        "all": [0.0 for _ in range(len(judge_friendly))],
        "BRANCH_DIRECT_JUMP and small": [0.0 for _ in range(len(judge_friendly))],
        "BRANCH_DIRECT_JUMP and large": [0.0 for _ in range(len(judge_friendly))],
        "BRANCH_CONDITIONAL and small": [0.0 for _ in range(len(judge_friendly))],
        "BRANCH_CONDITIONAL and large": [0.0 for _ in range(len(judge_friendly))],
        "BRANCH_DIRECT_CALL and small": [0.0 for _ in range(len(judge_friendly))],
        "BRANCH_DIRECT_CALL and large": [0.0 for _ in range(len(judge_friendly))],
    }
    for key, value in data_count.items():
        # print(value)
        for i in range(len(judge_friendly)):
            percentage_data[key][i] = (
                0.0
                if float(value[i][0]) + float(value[i][1]) == 0
                else float(value[i][0]) / (float(value[i][0]) + float(value[i][1]))
            )
    plt.figure()
    pos = -0.25
    all_pos = range(len(judge_friendly))
    corr_data = []
    for which_type, value in percentage_data.items():
        this_pos = []
        for i in all_pos:
            this_pos.append(i + pos)
        corr_data.extend(value)
        plt.bar(this_pos, value, 0.1, label=which_type)
        pos += 0.1
    # print(trace + ',' + ','.join([str(x) for x in corr_data]))
    csv_output.write(trace + "," + ",".join([str(x) for x in corr_data]) + "\n")
    output_path = Path(result_dir)
    output_path.mkdir(exist_ok=True)
    plt.xticks(all_pos, judge_friendly)
    plt.ylabel("Percentage of friendly branches")
    plt.xlabel("Judge friendly standard")
    plt.legend()
    directory = output_path / out_sub_dir
    directory.mkdir(exist_ok=True)
    plt.savefig(str(directory / ("%s_try_friendly_hist.pdf" % trace)))
    plt.close()


def main():
    # plot_friendly_histogram("server_003", 4, [0.9, 0.95, 0.99])
    output_path = Path(result_dir)
    output_path.mkdir(exist_ok=True)
    directory = output_path / "friendly_range_detailed_hist_taken"
    directory.mkdir(exist_ok=True)
    csv_file = directory / "data_summary.csv"
    with csv_file.open(mode="w") as csv_output:
        for file in Path(reuse_distance_dirname).iterdir():
            filename = file.name
            name = filename.split(".")[0]
            print(name)
            # for name in sorted(chosen_benchmarks):
            plot_detailed_friendly_histogram(
                name,
                4,
                [0.9, 0.95, 0.99],
                csv_output,
                out_sub_dir="friendly_range_detailed_hist_taken",
                range_count=True,
            )
    # num_bins = 2
    # for name in chosen_benchmarks:
    #     branch_type_classify(name, num_bins)
    #     # branch_target_histogram("server_003", num_bins)
    #     branch_target_classify(name, num_bins)
    #     no_classify(name, 2)


if __name__ == "__main__":
    main()
