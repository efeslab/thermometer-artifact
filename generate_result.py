import re
from pathlib import Path

import click

project_root = Path.cwd()
# file_root = Path("/mnt/storage/shixins/champsim_pt")
# summary_root = Path("/mnt/storage/shixins/champsim_pt/simulation_result")
file_root = Path("/home/shixinsong/Desktop/UM/SURE/plots")
summary_root = Path("/home/shixinsong/Desktop/UM/SURE/plots/simulation_result")

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

partial_generate = False

pt_traces = [
    "cassandra",
    "clang",
    "drupal",
    "finagle-chirper",
    "finagle-http",
    "kafka",
    "mediawiki",
    "pgbench",
    "python",
    "tomcat",
    "verilator",
    "wordpress",
]

def generate_one_result(dir_name, pt: bool, sql: bool = False):
    partial_result_dir = "pt_result" if pt else "correct_new_btb_fdip_result"
    template_dir_name = "%s_result" if pt else "ipc1_fdip_%s_result"
    if sql:
        partial_result_dir = "sql_result"
        template_dir_name = "%s_result"
    result_dir = file_root / partial_result_dir / (template_dir_name % dir_name)
    print(template_dir_name % dir_name)
    # table_dir = project_root / (
    #     "pt_result_analysis" if pt else "ipc1_correct_new_btb_result_analysis"
    # )
    table_dir = summary_root / (
        "pt_result_analysis" if pt else "ipc1_correct_new_btb_result_analysis"
    )
    if sql:
        table_dir = summary_root / "sql_result_analysis"
    table_dir.mkdir(exist_ok=True)
    output_filename = str(dir_name + ".csv") if pt else str(dir_name + ".csv")
    output = (table_dir / output_filename).open(mode="w")
    output.write(
        "Trace,IPC,L1I Access,L1I Hit,L1I Miss,L1I MPKI,Branch MPKI,L1D Access,L1D Hit,L1D Miss,L1D MPKI,L2C MPKI,LLC MPKI,L1I Load Access,L1I Load Hit,L1I Load Miss,L1I Load MPKI"
    )
    output.write(",L1I Latency,L1D Latency,L2C Latency,LLC Latency")
    output.write("\n")
    print(result_dir)
    # for short_filename in pt_traces:
    #     filename = f"{short_filename}_result.txt"
    #     file = result_dir / filename
    for file in sorted(result_dir.iterdir()):
        filename = file.name
        # if pt and filename.split("_")[0] not in pt_traces:
        #     continue
        trace = filename.split(".")[0]
        if partial_generate and trace not in chosen_benchmarks:
            continue
        # print(str(file))
        s = file.open().read()
        try:
            ipc_str = re.search("CPU 0 cumulative IPC.*\n", s).group(0)
        except:
            continue
        ipc_arr = ipc_str.split(" ")
        ipc = ipc_arr[4]
        instructions = int(ipc_arr[6])
        l1i = re.search("L1I TOTAL.*\n", s).group(0)
        l1i = l1i.split()
        l1i_access = int(l1i[3])
        l1i_hit = int(l1i[5])
        l1i_miss = int(l1i[7])
        l1i_mpki = round((1000.0 * l1i_miss) / instructions, 4)
        load_l1i = re.search("L1I LOAD.*\n", s).group(0)
        load_l1i = load_l1i.split()
        load_l1i_access = int(load_l1i[3])
        load_l1i_hit = int(load_l1i[5])
        load_l1i_miss = int(load_l1i[7])
        load_l1i_mpki = round((1000.0 * load_l1i_miss) / instructions, 4)
        l1d = re.search("L1D TOTAL.*\n", s).group(0)
        l1d = l1d.split()
        l1d_access = int(l1d[3])
        l1d_hit = int(l1d[5])
        l1d_miss = int(l1d[7])
        l1d_mpki = round((1000.0 * l1d_miss) / instructions, 4)
        branch = re.search("CPU 0 Branch Prediction Accuracy.*\n", s).group(0)
        branch_mpki = (branch.split())[7]
        l2c = re.search("L2C TOTAL.*\n", s).group(0)
        l2c = l2c.split()
        l2c_miss = int(l2c[7])
        l2c_mpki = round((1000.0 * l2c_miss) / instructions, 4)
        llc = re.search("LLC TOTAL.*\n", s).group(0)
        llc = llc.split()
        llc_miss = int(llc[7])
        llc_mpki = round((1000.0 * llc_miss) / instructions, 4)
        l1i_latency = re.search("L1I AVERAGE MISS LATENCY:.*\n", s).group(0).split()[4]
        l1d_latency = re.search("L1D AVERAGE MISS LATENCY:.*\n", s).group(0).split()[4]
        l2c_latency = re.search("L2C AVERAGE MISS LATENCY:.*\n", s).group(0).split()[4]
        llc_latency = re.search("LLC AVERAGE MISS LATENCY:.*\n", s).group(0).split()[4]
        output.write(
            "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n"
            % (
                trace,
                ipc,
                l1i_access,
                l1i_hit,
                l1i_miss,
                l1i_mpki,
                branch_mpki,
                l1d_access,
                l1d_hit,
                l1d_miss,
                l1d_mpki,
                l2c_mpki,
                llc_mpki,
                load_l1i_access,
                load_l1i_hit,
                load_l1i_miss,
                load_l1i_mpki,
                l1i_latency,
                l1d_latency,
                l2c_latency,
                llc_latency,
            )
        )


@click.command()
@click.option(
    "--trace-type", type=click.Choice(["ipc1", "pt", "both", "sql"], case_sensitive=False)
)
@click.option("--dir-list", type=click.STRING)
@click.option("--total-btb-ways", default="4", type=click.STRING)
def main(trace_type: str, dir_list: str, total_btb_ways: str):
    list_of_dir_names = dir_list.split(",")
    for dir_name in list_of_dir_names:
        for i in total_btb_ways.split(","):
            if trace_type == "sql":
                generate_one_result(dir_name + i, False, True)
            if trace_type == "ipc1" or trace_type == "both":
                generate_one_result(dir_name + i, False)
            if trace_type == "pt" or trace_type == "both":
                generate_one_result(dir_name + i, True)


if __name__ == "__main__":
    main()
