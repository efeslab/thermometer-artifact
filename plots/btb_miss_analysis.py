import multiprocessing

import click
import re
import subprocess
from pathlib import Path


# echo "BTB-Lookup: 1 0 CF_CALL 140299761704680 140299761717344 1\nBTB-Update: 1 0 CF_CALL 140299761704681 140299761717344 1" | awk '/BTB-Lookup:/{printf("r %d 1\n",$5);}'


def parse_one_file(input_path: Path, output_path: Path, binary_path: Path, btb_size: int):
    awk_command = """awk '/BTB-Lookup:/{printf("r %d 1\\n",$5);}'"""
    dineroIV_command = f"{binary_path} -l1-dbsize 1 -l1-dsize {btb_size} -l1-dassoc 4 -l1-drepl l -l1-dccc -informat D"
    cmd = f"""zcat {input_path} | {awk_command} | {dineroIV_command}"""
    # print(cmd)
    p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = p.communicate()
    stdout = stdout.decode()
    miss_type_list = ["Compulsory", "Capacity", "Conflict"]
    miss_type_fraction = {}
    for miss_type in miss_type_list:
        miss_type_fraction[miss_type] = re.search(f"{miss_type} fraction.*\n", stdout).group(0).strip().split()[2]
    print(f"{input_path.stem}\t{miss_type_fraction['Compulsory']}\t{miss_type_fraction['Capacity']}\t{miss_type_fraction['Conflict']}")
    # print("Finished", str(output_path))


def main():
    btb_size_list = [8192, 4096]
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
    total_way = 4
    input_dir = Path("/mnt/storage/shixins/champsim_pt/twig_record")
    input_way_dir = input_dir / f"way{total_way}"
    output_dir = Path("/mnt/storage/shixins/champsim_pt/btb_miss_dineroIV")
    output_dir.mkdir(exist_ok=True)
    output_way_dir = output_dir / f"way{total_way}"
    output_way_dir.mkdir(exist_ok=True)
    binary_path = Path("/mnt/storage/takh/git-repos/btb-stream-detection/dinero/dineroIV")
    for btb_size in btb_size_list:
        task_args = []
        print(f"{btb_size}\tCompulsory\tCapacity\tConflict")
        for app in app_list:
            input_path = input_way_dir / f"{app}.gz"
            output_path = output_way_dir / f"{app}_{btb_size}.gz"
            task_args.append([
                input_path,
                output_path,
                binary_path,
                btb_size
            ])
        with multiprocessing.Pool(26) as p:
            p.starmap(parse_one_file, task_args)


if __name__ == '__main__':
    main()
