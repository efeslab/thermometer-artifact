import multiprocessing

import click
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from pathlib import Path


def get_set_index(ip: int, num_sets: int):
    return (ip >> 2) % num_sets


def count_unique_branches(line: pd.Series):
    return line.value_counts()


def parse_one_file(input_path: Path, output_path: Path, num_sets_list: list):
    distribution_array = np.zeros((len(num_sets_list), max(num_sets_list)))
    print(f"Start read {input_path}")
    with input_path.open(mode="r") as input_file:
        while True:
            line = input_file.readline().rstrip()
            if not line:
                break
            if "PC" in line:
                continue
            ip = int(line.split(",")[0], 16)
            for i, num_sets in enumerate(num_sets_list):
                distribution_array[i][get_set_index(ip, num_sets)] += 1

    for line in distribution_array:
        line.sort()
    df = pd.DataFrame(distribution_array, index=num_sets_list)
    df.to_csv(output_path)
    print(f"Finish write to {output_path}")

    count_df = df.apply(count_unique_branches, axis=1)
    print(count_df)
    count_df = count_df.transpose()
    count_df.to_csv(output_path.parent / f"{output_path.stem}_count.csv")
    count_df.plot.bar(legend=True)
    plt.savefig(output_path.parent / f"{output_path.stem}_count.pdf")
    plt.close()
    # df = df.transpose()
    # df.plot.line(legend=True)
    # output_fig_path = output_path.parent / f"{output_path.stem}.png"
    # plt.savefig(output_fig_path)
    # plt.close()


@click.command()
@click.option(
    "--local",
    default=False,
    type=bool,
)
def main(local: bool):
    app_list = [
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
    if not local:
        input_dir = Path("/mnt/storage/shixins/champsim_pt/opt_access_record/way4")
        output_dir = Path("/mnt/storage/shixins/champsim_pt/branch_distribution_set/way4")
        output_dir.mkdir(exist_ok=True, parents=True)
        num_sets_list = [7979, 8192]
        task_args = []
        for app in app_list:
            input_path = input_dir / f"{app}.csv"
            output_path = output_dir / f"{app}.csv"
            task_args.append([input_path, output_path, num_sets_list])

        with multiprocessing.Pool(1) as p:
            p.starmap(parse_one_file, task_args)
    else:
        input_dir = Path("/home/shixinsong/Desktop/UM/SURE/plots/paper_results/branch_distribution_set/way4")
        for app in app_list:
            df = pd.read_csv(input_dir / f"{app}_count.csv", index_col=0, header=0)
            df = df.drop([0.0, 1.0, 2.0, 3.0, 4.0])
            df.plot.bar(legend=True)
            plt.savefig(input_dir / f"{app}_count.pdf")
            plt.close()


if __name__ == '__main__':
    main()
