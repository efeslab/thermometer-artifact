import math
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib import colors
from matplotlib.ticker import PercentFormatter


def print_one_set_branches(chosen_set: int, total_set: int, full_path_name: str):
    with Path(full_path_name).open(mode="r") as file:
        if not file:
            print("cannot open")
            return
        count = 0
        while True:
            line = file.readline().rstrip()
            if not line:
                break
            ip = int(line.split(" ")[0])
            curr_set = (ip >> 2) & (total_set - 1)
            if curr_set == chosen_set:
                if ip == int("446964", 16):
                    print(hex(ip), count)
                count += 1


def main():
    print_one_set_branches(
        (int("446964", 16) >> 2) & 2047,
        2048,
        "/mnt/storage/shixins/champsim_pt/btb_record_insert_taken/client_002.champsimtrace.xz.txt",
    )


if __name__ == "__main__":
    main()
