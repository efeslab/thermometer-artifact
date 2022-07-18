import re
from pathlib import Path

project_root = Path.cwd()

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


def main(dir_name):
    result_dir = project_root / str(
        "correct_new_btb_fdip_result/" + dir_name + "_result"
    )
    table_dir = project_root / "ipc1_correct_new_btb_branch_analysis"
    table_dir.mkdir(exist_ok=True)
    output_filename = str(dir_name + ".csv")
    output = (table_dir / output_filename).open(mode="w")
    output.write(
        "BRANCH_DIRECT_JUMP,BRANCH_INDIRECT,BRANCH_CONDITIONAL,BRANCH_DIRECT_CALL,BRANCH_INDIRECT_CALL,BRANCH_RETURN\n"
    )
    for file in sorted(result_dir.iterdir()):
        filename = file.name
        trace = filename.split(".")[0]
        if not partial_generate or trace not in chosen_benchmarks:
            continue
        # print(str(file))
        s = file.open().read()
        direct_jump = (
            re.search("BRANCH_DIRECT_JUMP:.*\n", s).group(0).split(" ")[1].rstrip()
        )
        direct_indirect = (
            re.search("BRANCH_INDIRECT:.*\n", s).group(0).split(" ")[1].rstrip()
        )
        direct_conditional = (
            re.search("BRANCH_CONDITIONAL:.*\n", s).group(0).split(" ")[1].rstrip()
        )
        direct_direct_call = (
            re.search("BRANCH_DIRECT_CALL:.*\n", s).group(0).split(" ")[1].rstrip()
        )
        direct_indirect_call = (
            re.search("BRANCH_INDIRECT_CALL:.*\n", s).group(0).split(" ")[1].rstrip()
        )
        direct_return = (
            re.search("BRANCH_RETURN:.*\n", s).group(0).split(" ")[1].rstrip()
        )

        output.write(
            "%s,%s,%s,%s,%s,%s\n"
            % (
                direct_jump,
                direct_indirect,
                direct_conditional,
                direct_direct_call,
                direct_indirect_call,
                direct_return,
            )
        )


if __name__ == "__main__":
    # list_of_dir_names = ["ipc1_fdip", "ipc1_no_l1i",
    #                      "ipc1_fdip_perfect_btb", "ipc1_fdip_perfect_bpu",
    #                      "ipc1_fdip_srrip", "ipc1_fdip_ghrp", "ipc1_fdip_hawkeye", "ipc1_fdip_opt",
    #                      "ipc1_fdip_srrip_predecode", "ipc1_fdip_srrip_confluence", "ipc1_fdip_srrip_shotgun",
    #                      "ipc1_fdip_ghrp_predecode", "ipc1_fdip_ghrp_confluence", "ipc1_fdip_ghrp_shotgun",
    #                      "ipc1_fdip_hawkeye_predecode", "ipc1_fdip_hawkeye_confluence", "ipc1_fdip_hawkeye_shotgun",
    #                      "ipc1_fdip_opt_predecode", "ipc1_fdip_opt_confluence", "ipc1_fdip_opt_shotgun"]
    # list_of_dir_names = ["ipc1_fdip_srrip", "ipc1_fdip_opt",
    #                      "ipc1_fdip_srrip_predecode", "ipc1_fdip_srrip_confluence", "ipc1_fdip_srrip_shotgun",
    #                      "ipc1_fdip_opt_predecode", "ipc1_fdip_opt_confluence", "ipc1_fdip_opt_shotgun",
    #                      "ipc1_fdip_srrip_confluence_large_footprint", "ipc1_fdip_opt_confluence_large_footprint"]
    list_of_dir_names = [
        "ipc1_fdip_lru",
        "ipc1_fdip_srrip",
        "ipc1_fdip_opt",
        "ipc1_fdip_count",
        "ipc1_fdip_ghrp",
        "ipc1_fdip_hawkeye",
        "ipc1_fdip_prob",
        "ipc1_fdip_srrip_taken",
        "ipc1_fdip_srrip_taken0",
        "ipc1_fdip_count_clear",
    ]
    for dir_name in list_of_dir_names:
        main(dir_name)
