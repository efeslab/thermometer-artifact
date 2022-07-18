import enum
import math
import pathlib
from functools import wraps
from pathlib import Path

import click
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib import colors
from matplotlib.ticker import PercentFormatter
from tqdm import tqdm

total_ways = 4

pt_traces = [
    "cassandra",
    "clang",
    "drupal",
    "finagle-chirper",
    "finagle-http",
    "kafka",
    "mediawiki",
    "mysqllarge",
    "pgbench",
    "python",
    "tomcat",
    "verilator",
    "wordpress",
]


class HWCType(enum.Enum):
    hot = 0
    warm = 1
    cold = 2


# Just a practice for decorator with parameter
def decorator_example(input_path, output_csv):
    def wrapper(fn):
        @wraps(fn)
        def wrapped(x):
            print("Start", input_path, output_csv)
            fn(x)
            print("End")

        return wrapped

    return wrapper


@decorator_example("in", "out")
def fd(x):
    print("Hello", x)


def type_match_classify(row: list):
    hwc_choice = int(row[2 * (total_ways + 1)])
    opt_choice = int(row[2 * (total_ways + 1) + 1])
    hwc_type = int(row[hwc_choice * 2])
    opt_type = int(row[opt_choice * 2])
    if opt_choice == total_ways and hwc_choice != total_ways:
        # opt not evict
        return 3
    if hwc_type == opt_type:
        return 1
    return 2


def type_match_detailed_classify(row: list):
    hwc_choice = int(row[2 * (total_ways + 1)])
    opt_choice = int(row[2 * (total_ways + 1) + 1])
    hwc_type = int(row[hwc_choice * 2])
    opt_type = int(row[opt_choice * 2])
    if opt_choice == total_ways and hwc_choice != total_ways:
        # opt not evict
        return 3
    if hwc_choice == total_ways and opt_choice != total_ways:
        return 4
    if hwc_type == opt_type:
        return 1
    return 2


def opt_taken_classify(row: list):
    # Hot not taken, hot taken, warm not taken, warm taken, cold not taken, cold taken
    opt_choice = int(row[2 * (total_ways + 1) + 1])
    opt_type = int(row[opt_choice * 2])
    opt_taken = int(row[opt_choice * 2 + 1])
    assert opt_taken == 0 or opt_taken == 1
    return opt_type * 2 + 1 + opt_taken


def hwc_opt_type_classify(row: list):
    # hwc hot & opt hot, hwc warm & opt hot, hwc warm & opt warm,
    # hwc cold & opt hot, hwc cold & opt warm, hwc cold & opt cold
    hwc_choice = int(row[2 * (total_ways + 1)])
    hwc_type = int(row[hwc_choice * 2])
    opt_choice = int(row[2 * (total_ways + 1) + 1])
    opt_type = int(row[opt_choice * 2])
    index_dict = {
        (HWCType.hot, HWCType.hot): 1,
        (HWCType.warm, HWCType.hot): 2,
        (HWCType.warm, HWCType.warm): 3,
        (HWCType.cold, HWCType.hot): 4,
        (HWCType.cold, HWCType.warm): 5,
        (HWCType.cold, HWCType.cold): 6,
    }
    return index_dict[(HWCType(hwc_type), HWCType(opt_type))]


def hwc_evict_opt_not_type_classify(row: list):
    # hwc hot & opt hot, hwc warm & opt hot, hwc warm & opt warm,
    # hwc cold & opt hot, hwc cold & opt warm, hwc cold & opt cold
    # else (both evict or opt evict but hwc not evict)
    hwc_choice = int(row[2 * (total_ways + 1)])
    hwc_type = int(row[hwc_choice * 2])
    opt_choice = int(row[2 * (total_ways + 1) + 1])
    opt_type = int(row[opt_choice * 2])
    index_dict = {
        (HWCType.hot, HWCType.hot): 1,
        (HWCType.warm, HWCType.hot): 2,
        (HWCType.warm, HWCType.warm): 3,
        (HWCType.cold, HWCType.hot): 4,
        (HWCType.cold, HWCType.warm): 5,
        (HWCType.cold, HWCType.cold): 6,
    }
    if hwc_choice == total_ways and opt_choice != total_ways:
        return index_dict[(HWCType(hwc_type), HWCType(opt_type))]
    else:
        return 7


def all_same_type_classify(row: list):
    # not the same, 0th, 1th, 2th, ...
    btb_branch_type = int(row[0])
    curr_type = int(row[8]) + 1 if int(row[8]) > 0 else int(row[8])
    hwc_choice = int(row[2 * (total_ways + 1)])
    hwc_type = int(row[hwc_choice * 2])
    for i in range(total_ways):
        if int(row[i * 2]) != btb_branch_type:
            # All entries are not the same one.
            return 1
    if curr_type >= btb_branch_type:
        return btb_branch_type + 2
    else:
        return 1


def read_one_csv(input_path: Path, size, classify):
    df = pd.read_csv(input_path, header=0)
    all_count = [0] * size
    new_df = df.apply(classify, axis=1).value_counts()
    for i, count in new_df.iteritems():
        all_count[i] = count
    # for _, row in df.iterrows():
    #     all_count[classify(row)] += 1
    trace = input_path.name.split(".")[0]
    all_count[0] = trace
    return all_count


def read_all_csv(
    input_dir: Path, output_path: Path, classify, headers, pt: bool = False
):
    # classify(False, None, output_csv)
    csv_list = []
    for file in sorted(input_dir.glob("*.csv")):
        if pt and file.name.split(".")[0] not in pt_traces:
            continue
        if not pt and file.name.split(".")[0].split("_")[0] in pt_traces:
            continue
        csv_list.append(file)
    if not csv_list:
        return
    # csv_list = sorted(input_dir.glob("*.csv"))
    total_size = 0
    for input_path in csv_list:
        total_size += input_path.stat().st_size
    output_df = pd.DataFrame(columns=headers, index=range(len(csv_list)))
    with tqdm(total=total_size) as pbar:
        for i, input_path in enumerate(csv_list):
            output_df.loc[i] = read_one_csv(input_path, len(headers), classify)
            pbar.update(input_path.stat().st_size)
    output_df.to_csv(output_path, index=False, header=True)


def read_one_file(input_path: Path, output_csv):
    output_csv.write(input_path.name.split(".")[0])
    all_count = np.zeros((4, 4))
    with input_path.open(mode="r") as input_file:
        if not input_file:
            print("cannot open")
            return
        input_file.readline()
        while True:
            line = input_file.readline().rstrip()
            if not line:
                break
            words = line.split(",")
            all_count[int(words[0])][int(words[2])] += 1
    for a in all_count:
        output_csv.write(",".join(map(lambda x: str(x), a)))
        output_csv.write(",")
    output_csv.write("\n")


def generate_csv(input_dir: Path, output_path: Path):
    with output_path.open(mode="w") as output_csv:
        output_csv.write("Trace")
        for i in range(4):
            for j in range(4):
                output_csv.write(",%s %s" % (i, j))
        output_csv.write("\n")
        for input_path in sorted(input_dir.iterdir()):
            read_one_file(input_path, output_csv)


@click.command()
@click.option(
    "--input-big-dir",
    default=Path("/mnt/storage/shixins/champsim_pt/hwc_opt_compare_raw_data"),
    type=pathlib.Path,
)
@click.option(
    "--output-dir",
    default=Path("/mnt/storage/shixins/champsim_pt/hwc_opt_compare"),
    type=pathlib.Path,
)
@click.option("--pt", default=False, type=bool)
@click.option("--input-dirs", type=click.STRING)
def main(input_big_dir: Path, output_dir: Path, pt: bool, input_dirs: str):
    # TODO: modify the way to get input_dir!!!
    output_dir.mkdir(exist_ok=True)
    input_dirs_arr = input_dirs.split(",")
    for input_dir in sorted(input_big_dir.iterdir()):
        input_dir: Path
        if (
            input_dir.is_dir()
            # and input_dir.name == "hwc_opt_ChampSim_fdip_hot_warm_cold_80_25_f_keep"
            and input_dir.name in input_dirs_arr
        ):
            # name = str(input_dir.name) + ".csv"
            # print(name)
            # output_path = output_dir / name
            # headers = ["Trace", "hwc opt choose same type", "hwc opt choose different type",
            #            "opt not evict (hwc evict)"]
            # read_all_csv(input_dir, output_path, type_match_classify, headers)

            # opt_taken_headers = ["Trace", "hot not taken", "hot taken", "warm not taken", "warm taken",
            #                      "cold not taken", "cold taken"]
            # opt_taken_name = str(input_dir.name) + "_opt_taken.csv"
            # print(opt_taken_name)
            # opt_taken_output_path = output_dir / opt_taken_name
            # read_all_csv(input_dir, opt_taken_output_path, opt_taken_classify, opt_taken_headers)

            # hwc_opt_type_headers = [
            #     "Trace",
            #     "hwc hot & opt hot", "hwc warm & opt hot", "hwc warm & opt warm",
            #     "hwc cold & opt hot", "hwc cold & opt warm", "hwc cold & opt cold"
            # ]
            # hwc_opt_type_name = str(input_dir.name) + "_hwc_opt_type_pt.csv" if pt else str(input_dir.name) + "_hwc_opt_type.csv"
            # print(hwc_opt_type_name)
            # hwc_opt_type_output_path = output_dir / hwc_opt_type_name
            # read_all_csv(input_dir, hwc_opt_type_output_path, hwc_opt_type_classify, hwc_opt_type_headers, pt)
            #
            # name = str(input_dir.name) + "_type_compare_pt.csv" if pt else str(input_dir.name) + "_type_compare.csv"
            # print(name)
            # output_path = output_dir / name
            # headers = ["Trace", "hwc opt choose same type", "hwc opt choose different type",
            #            "opt not evict hwc evict", "opt evict hwc not evict"]
            # read_all_csv(input_dir, output_path, type_match_detailed_classify, headers, pt)

            # hwc_evict_opt_not_type_headers = [
            #     "Trace",
            #     "hwc hot & opt hot",
            #     "hwc warm & opt hot",
            #     "hwc warm & opt warm",
            #     "hwc cold & opt hot",
            #     "hwc cold & opt warm",
            #     "hwc cold & opt cold",
            #     "others",
            # ]
            # hwc_evict_opt_not_type_name = (
            #     str(input_dir.name) + "_hwc_evict_opt_not_type_pt.csv"
            #     if pt
            #     else str(input_dir.name) + "_hwc_evict_opt_not_type.csv"
            # )
            # print(hwc_evict_opt_not_type_name)
            # hwc_evict_opt_not_type_output_path = (
            #     output_dir / hwc_evict_opt_not_type_name
            # )
            # read_all_csv(
            #     input_dir,
            #     hwc_evict_opt_not_type_output_path,
            #     hwc_evict_opt_not_type_classify,
            #     hwc_evict_opt_not_type_headers,
            #     pt,
            # )

            all_same_type_headers = ["Trace", "not the same", "0", "1", "2", "3"]
            all_same_type_name = (
                str(input_dir.name) + "_all_same_type_pt.csv"
                if pt
                else str(input_dir.name) + "_all_same_type.csv"
            )
            print(all_same_type_name)
            all_same_type_output_path = output_dir / all_same_type_name
            read_all_csv(
                input_dir,
                all_same_type_output_path,
                all_same_type_classify,
                all_same_type_headers,
                pt,
            )

    # output_path = output_dir / "hwc_80_25.csv"
    # generate_csv(input_dir, output_path)


if __name__ == "__main__":
    main()
