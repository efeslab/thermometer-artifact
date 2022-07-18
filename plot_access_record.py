import enum
import math
import statistics
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib import colors
from matplotlib.ticker import PercentFormatter

cdf_list = [
    10,
    20,
    30,
    40,
    50,
    60,
    70,
    80,
    90,
    95,
    99,
    99.1,
    99.2,
    99.3,
    99.4,
    99.5,
    99.6,
    99.7,
    99.8,
    99.9,
    99.95,
]


class RecordType(enum.Enum):
    hit = 0
    miss_only = 1
    miss_insert = 2
    evict = 3


class HWCType(enum.Enum):
    cold = 0
    warm = 1
    hot = 2


def count_hit_miss(data: list):
    total_hit = 0
    total_miss = 0
    for a in data:
        num = RecordType(int(a))
        if num == RecordType.hit:
            total_hit += 1
        elif num == RecordType.miss_only or num == RecordType.miss_insert:
            total_miss += 1
    return [total_hit, total_miss]


def count_all_kinds(data: list):
    result = [0] * 4
    for a in data:
        num = int(a)
        result[num] += 1
    return result


def judge_hwc_type(
    hit_miss_ratio: float, hot_lower_bound: float, cold_upper_bound: float
):
    if hit_miss_ratio <= cold_upper_bound:
        return HWCType.cold
    if hit_miss_ratio <= hot_lower_bound:
        return HWCType.warm
    return HWCType.hot


def cal_hit_access_misclassify(
    data: list, hot_lower_bound: float, cold_upper_bound: float
):
    total_hit = 0
    total_miss = 0
    all_hit_miss = []
    curr_hit_count = 0
    for a in data:
        num = RecordType(int(a))
        if num == RecordType.miss_only or num == RecordType.miss_insert:
            # miss
            total_miss += 1
            all_hit_miss.append(curr_hit_count)
            curr_hit_count = 0
        elif num == RecordType.hit:
            # hit
            total_hit += 1
            curr_hit_count += 1
    total_hit_miss_ratio = float(total_hit) / float(total_hit + total_miss)
    total_hwc_type = judge_hwc_type(
        total_hit_miss_ratio, hot_lower_bound, cold_upper_bound
    )
    all_hwc_type = []
    for a in all_hit_miss:
        all_hwc_type.append(
            judge_hwc_type(float(a) / float(a + 1), hot_lower_bound, cold_upper_bound)
        )
    all_hwc_type_count = [
        all_hwc_type.count(HWCType.cold),
        all_hwc_type.count(HWCType.warm),
        all_hwc_type.count(HWCType.hot),
    ]
    return total_hwc_type, all_hwc_type_count


def plot_hit_access_misclassify(
    input_path: Path, output_dir: Path, hot_lower_bound: float, cold_upper_bound: float
):
    filename = input_path.name
    trace = filename.split(".")[0]
    print(trace)
    all_warm_misclassify_hot = []
    all_hot_misclassify_warm = []
    all_warm_misclassify_cold = []
    with input_path.open(mode="r") as input_file:
        if not input_file:
            print("cannot open")
            return
        input_file.readline()
        while True:
            line = input_file.readline().rstrip()
            if not line:
                break
            all_words = line.split(",")
            total_hwc_type, all_hwc_type_count = cal_hit_access_misclassify(
                all_words[3:], hot_lower_bound, cold_upper_bound
            )
            if total_hwc_type == HWCType.warm:
                all_warm_misclassify_hot.append(
                    float(all_hwc_type_count[2]) / float(sum(all_hwc_type_count))
                )
                all_warm_misclassify_cold.append(
                    float(all_hwc_type_count[0]) / float(sum(all_hwc_type_count))
                )
            if total_hwc_type == HWCType.hot:
                all_hot_misclassify_warm.append(
                    float(all_hwc_type_count[1]) / float(sum(all_hwc_type_count))
                )
    plt.figure()
    plt.hist(x=all_warm_misclassify_hot, bins=100)
    output_path = output_dir / (trace + "_warm_misclassify_hot.pdf")
    plt.savefig(str(output_path))
    plt.close()
    plt.figure()
    plt.hist(x=all_warm_misclassify_cold, bins=100)
    output_path = output_dir / (trace + "_warm_misclassify_cold.pdf")
    plt.savefig(str(output_path))
    plt.close()
    plt.figure()
    plt.hist(x=all_hot_misclassify_warm, bins=100)
    output_path = output_dir / (trace + "_hot_misclassify_warm.pdf")
    plt.savefig(str(output_path))
    plt.close()


def cal_hit_access_variance(data: list):
    all_hit_access = []
    curr_hit = 0
    curr_miss = 0
    for a in data:
        num = RecordType(int(a))
        if num == RecordType.miss_only or num == RecordType.miss_insert:
            # miss
            if curr_hit != 0:
                # end of last period
                all_hit_access.append(float(curr_hit) / float(curr_hit + curr_miss))
                assert curr_miss != 0
                curr_hit = 0
                curr_miss = 0
            curr_miss += 1
        elif num == RecordType.hit:
            # hit
            assert curr_miss != 0
            curr_hit += 1
    all_hit_access.append(float(curr_hit) / float(curr_hit + curr_miss))
    if len(all_hit_access) == 1:
        return 0
    return statistics.variance(all_hit_access)


def plot_hit_access_variance(input_path: Path, output_dir: Path):
    filename = input_path.name
    trace = filename.split(".")[0]
    print(trace)
    all_variance = []
    zero_count = 0
    with input_path.open(mode="r") as input_file:
        if not input_file:
            print("cannot open")
            return
        input_file.readline()
        while True:
            line = input_file.readline().rstrip()
            if not line:
                break
            all_words = line.split(",")
            variance = cal_hit_access_variance(all_words[3:])
            if variance != 0:
                all_variance.append(variance)
            else:
                zero_count += 1
    print(zero_count, len(all_variance))
    plt.figure()
    plt.hist(x=all_variance, bins=200)
    output_path = output_dir / (trace + ".pdf")
    plt.savefig(str(output_path))
    plt.close()


def average_consecutive_zero(data: list):
    total_count = 0
    total_sec = 0
    tmp_count = 0
    for a in data:
        num = RecordType(int(a))
        if num == RecordType.hit:
            tmp_count += 1
        elif num == RecordType.evict:
            total_count += tmp_count
            total_sec += 1
            tmp_count = 0
        else:
            assert tmp_count == 0
    total_count += tmp_count
    total_sec += 1
    return float(total_count) / float(total_sec)


def dynamic_execution_cdf(input_path: Path, special_cdf: list, csv_output):
    filename = input_path.name
    trace = filename.split(".")[0]
    print(trace)
    total_access_count = 0
    all_set = []
    with input_path.open(mode="r") as input_file:
        if not input_file:
            print("cannot open")
            return
        input_file.readline()
        while True:
            line = input_file.readline().rstrip()
            if not line:
                break
            all_words = line.split(",")
            ip = int(all_words[0], 16)
            target = int(all_words[1], 16)
            distance = ip - target if ip >= target else target - ip
            hit, miss = count_hit_miss(all_words[3:])
            total_access_count += hit + miss
            all_set.append((float(hit) / float(miss + hit), hit + miss))
    all_set.sort(reverse=True)
    cdf_index = 0
    result = []
    partial_sum = 0
    for i, (hit_miss_ratio, access_count) in enumerate(all_set):
        partial_sum += access_count
        while (
            cdf_index < len(special_cdf)
            and partial_sum * 100 / total_access_count >= special_cdf[cdf_index]
        ):
            before = (partial_sum - access_count) * 100 / total_access_count
            after = partial_sum * 100 / total_access_count
            if (
                i > 0
                and special_cdf[cdf_index] - before < after - special_cdf[cdf_index]
            ):
                print(before, after, "before")
                result.append((i - 1, all_set[i - 1][0]))
            else:
                print(before, after, "after")
                result.append((i, hit_miss_ratio))
            cdf_index += 1
    assert len(result) == 21
    csv_output.write(
        trace
        + ","
        + ",".join([str(x[0]) for x in result])
        + ","
        + str(len(all_set))
        + ","
    )
    csv_output.write(",".join([str(x[1]) for x in result]) + "\n")


def all_cdf(input_dir: Path):
    output_path = Path(
        "/mnt/storage/shixins/champsim_pt/dynamic_execution_CDF_hit_all_pt.csv"
    )
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
    with output_path.open(mode="w") as output_file:
        for input_path in sorted(input_dir.iterdir()):
            if input_path.name.split(".")[0] not in pt_traces:
                continue
            dynamic_execution_cdf(input_path, cdf_list, output_file)


def cal_correlation(input_path: Path, csv_output):
    output_dir = Path("/mnt/storage/shixins/champsim_pt/access_record_corr.csv")
    filename = input_path.name
    trace = filename.split(".")[0]
    print(trace)
    collect_data = {
        "hit / miss": [],
        "miss with insertion / all miss": [],
        "num of consecutive hits": [],
    }
    with input_path.open(mode="r") as input_file:
        if not input_file:
            print("cannot open")
            return
        input_file.readline()
        while True:
            line = input_file.readline().rstrip()
            if not line:
                break
            all_words = line.split(",")
            ip = int(all_words[0], 16)
            target = int(all_words[1], 16)
            distance = ip - target if ip >= target else target - ip
            all_kinds = count_all_kinds(all_words[3:])
            consecutive_zero = average_consecutive_zero(all_words[3:])
            collect_data["hit / miss"].append(
                float(all_kinds[0]) / float(all_kinds[2] + all_kinds[1])
            )
            collect_data["miss with insertion / all miss"].append(
                float(all_kinds[2]) / float(all_kinds[2] + all_kinds[1])
            )
            collect_data["num of consecutive hits"].append(consecutive_zero)
    result = []
    for key1, data1 in collect_data.items():
        for key2, data2 in collect_data.items():
            if key1 < key2:
                result.append(np.corrcoef(data1, data2)[0][1])
    csv_output.write(trace + "," + ",".join([str(x) for x in result]) + "\n")


def all_corr(input_dir: Path):
    output_path = Path("/mnt/storage/shixins/champsim_pt/all_correlation.csv")
    with output_path.open(mode="w") as output_file:
        collect_data = {
            "hit / miss": [],
            "miss with insertion / all miss": [],
            "num of consecutive hits": [],
        }
        for key1 in collect_data.keys():
            for key2 in collect_data.keys():
                if key1 < key2:
                    output_file.write("," + key1 + " vs " + key2)
        output_file.write("\n")
        for input_path in sorted(input_dir.iterdir()):
            cal_correlation(input_path, output_file)


def plot_hit_miss_hist(input_path: Path, output_dir: Path):
    filename = input_path.name
    trace = filename.split(".")[0]
    print(trace)
    all_set = []
    with input_path.open(mode="r") as input_file:
        if not input_file:
            print("cannot open")
            return
        input_file.readline()
        while True:
            line = input_file.readline().rstrip()
            if not line:
                break
            all_words = line.split(",")
            ip = int(all_words[0], 16)
            target = int(all_words[1], 16)
            distance = ip - target if ip >= target else target - ip
            hit, miss = count_hit_miss(all_words[3:])
            for _ in range(hit + miss):
                all_set.append(float(hit) / float(hit + miss))
            # if float(hit) / float(miss) < 200.0:
            #     all_set.append(float(hit) / float(miss))
    plt.figure()
    plt.hist(x=all_set, bins=200)
    output_path = output_dir / (trace + ".pdf")
    plt.savefig(str(output_path))
    plt.close()


def main():
    input_dir = Path("/mnt/storage/shixins/champsim_pt/opt_access_record/way4")

    # Warm but should be hot
    output_dir = Path(
        "/mnt/storage/shixins/champsim_pt/opt_access_record_hwc_misclassify"
    )
    output_dir.mkdir(exist_ok=True)
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
    # for input_path in sorted(input_dir.iterdir()):
    #     if input_path.name.split(".")[0] not in pt_traces:
    #         continue
    #     plot_hit_access_misclassify(input_path, output_dir, 0.8, 0.25)

    # output_dir = Path("/mnt/storage/shixins/champsim_pt/opt_hit_access_variance_hist")
    # output_dir.mkdir(exist_ok=True)
    # for input_path in sorted(input_dir.iterdir()):
    #     plot_hit_access_variance(input_path, output_dir)

    # all_corr(input_dir)
    all_cdf(input_dir)
    # output_dir = Path("/mnt/storage/shixins/champsim_pt/opt_access_record_hit_access_dynamic_hist")
    # output_dir.mkdir(exist_ok=True)
    # for input_path in sorted(input_dir.iterdir()):
    #     plot_hit_miss_hist(input_path, output_dir)


if __name__ == "__main__":
    main()
