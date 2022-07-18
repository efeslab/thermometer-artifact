import asyncio
import functools
import subprocess
from ast import literal_eval
from pathlib import Path

import click

project_root = Path.cwd()
file_root = Path("/mnt/storage/shixins/champsim_pt")
# run_list = ["srrip_shotgun",
#             "ghrp_predecode", "ghrp_confluence",
#             "hawkeye_predecode", "hawkeye_confluence", "hawkeye_shotgun"]
# run_list = [
#     "ChampSim_fdip_opt",
# ]
# data_root = (file_root / "ipc1_public") if not pt else (file_root / "pt_traces")
# data_root = file_root / "intel_pt"

workers = 67
count = 0

run_partial_benchmarks = False
chosen_benchmarks0 = [
    "client_001",
    "client_002",
    "client_003",
    "server_003",
    "server_004",
    "server_016",
    "server_017",
    "spec_gcc_001",
    "spec_gcc_002",
    "spec_x264_001",
]
chosen_benchmarks1 = [
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
]
# bench = {"opt_shotgun": list(set(chosen_benchmarks1) - set(chosen_benchmarks0)),
#          "prob": list(set(chosen_benchmarks0) | set(chosen_benchmarks1)),
#          "prob_predecode": list(set(chosen_benchmarks0) | set(chosen_benchmarks1)),
#          "prob_confluence": list(set(chosen_benchmarks0) | set(chosen_benchmarks1)),
#          "prob_shotgun": list(set(chosen_benchmarks0) | set(chosen_benchmarks1))}
# bench = {"count": list(set(chosen_benchmarks1) | set(chosen_benchmarks0)),
#          "srrip_count": list(set(chosen_benchmarks1) | set(chosen_benchmarks0)),
#          "count_srrip": list(set(chosen_benchmarks0) | set(chosen_benchmarks1))}
bench = list(set(chosen_benchmarks0) | set(chosen_benchmarks1))

TIMEOUT = 36000

warmup_instructions = "50000000"
simulation_instructions = "50000000"


def get_output_filename(
        short_name: str,
        use_default_btb_record: bool,
        train_total_btb_ways,
        train_total_btb_entries,
        # use_twig_prefetch: bool
):
    default_btb_record_str = f"_use_default_btb_record_{train_total_btb_ways}_{train_total_btb_entries}" if use_default_btb_record else ""
    # use_twig_prefetch_str = f"_use_twig" if use_twig_prefetch else ""
    return f"{short_name}{default_btb_record_str}_result.txt"


def get_result_dirname(
        prefix: str,
        result_dir_template: str,
        program_name: str,
        total_btb_ways,
        use_twig_prefetch: bool
):
    if use_twig_prefetch:
        program_name = f"{program_name}_twig"
    return prefix + (result_dir_template % (program_name, total_btb_ways))


async def run(
        trace_file,
        run_program,
        pt,
        total_btb_ways,
        total_btb_entries,
        champsim_pt,
        generate,
        pgo,
        use_default_btb_record,
        train_total_btb_ways,
        train_total_btb_entries,
        twig_profile,
        use_twig_prefetch,
):
    if total_btb_entries == "8":
        prefix = ""
    elif int(total_btb_entries) < 1024:
        prefix = str(total_btb_entries) + "K_"
    else:
        prefix = total_btb_entries

    # Mode judge
    if pgo:
        name = Path(trace_file).name.split(".")[0]
        raw_data_dir = "pgo_result"
        result_dir_template = "%s%s_result"
    elif pt:
        name = Path(trace_file).name
        trace_file = trace_file + "/trace.gz"
        raw_data_dir = "pt_result"
        result_dir_template = "%s%s_result"
    elif not champsim_pt:
        name = Path(trace_file).name.split(".")[0]
        raw_data_dir = "correct_new_btb_fdip_result"
        result_dir_template = "ipc1_fdip_%s%s_result"
    else:
        name = Path(trace_file).name.split(".")[0]
        raw_data_dir = "champsim_pt_result"
        result_dir_template = "%s%s_result"
    # TODO: Modify this later
    if run_partial_benchmarks and name in bench:
        return
    programs = []
    partial = run_program.split("_")
    if partial[-1] == "opt" and generate:
        print("Generate btb record and then run %s" % run_program)
        programs.append(
            str(project_root / "cmake-build-release" / (run_program + "_generate"))
        )
    programs.append(str(project_root / "cmake-build-release" / run_program))
    for program in programs:
        # program = str(project_root / "bin" / run_program)
        result_dirname = get_result_dirname(
            prefix=prefix,
            result_dir_template=result_dir_template,
            program_name=Path(program).name,
            total_btb_ways=total_btb_ways,
            use_twig_prefetch=use_twig_prefetch
        )
        result_dir = (
                file_root
                / raw_data_dir
                / result_dirname
        )
        result_dir.mkdir(exist_ok=True, parents=True)
        if pt or champsim_pt or pgo:
            output_filename = get_output_filename(
                short_name=name,
                use_default_btb_record=use_default_btb_record,
                train_total_btb_ways=train_total_btb_ways,
                train_total_btb_entries=train_total_btb_entries,
                # use_twig_prefetch=use_twig_prefetch
            )
        else:
            output_filename = get_output_filename(
                short_name=Path(trace_file).name,
                use_default_btb_record=use_default_btb_record,
                train_total_btb_ways=train_total_btb_ways,
                train_total_btb_entries=train_total_btb_entries,
                # use_twig_prefetch=use_twig_prefetch
            )
        output_path = result_dir / output_filename
        output_file = output_path.open(mode="w")
        args = [
            program,
            "-warmup_instructions",
            warmup_instructions,
            "-simulation_instructions",
            simulation_instructions,
            # "-perfect", "bpu",
        ]
        if pt:
            args.extend(["-pt"])
        x = run_program.split("_")[-1]
        if x == "bpu" or x == "btb" or x == "bp":
            args.extend(["-perfect", x])

        args.extend(["-total_btb_ways", str(total_btb_ways)])
        args.extend(["-total_btb_entries", str(total_btb_entries)])
        if use_default_btb_record:
            args.extend([
                "-train_total_btb_ways", str(train_total_btb_ways),
                "-train_total_btb_entries", str(train_total_btb_entries),
            ])
        else:
            assert train_total_btb_ways == total_btb_ways and train_total_btb_entries == total_btb_entries
            args.extend([
                "-train_total_btb_ways", str(total_btb_ways),
                "-train_total_btb_entries", str(total_btb_entries),
            ])

        if twig_profile:
            args.extend(["-twig"])

        if use_twig_prefetch:
            args.extend(["-twig_prefetch"])

        args.extend(["-traces", str(trace_file)])
        print(" ".join(args))
        global workers, count
        while workers <= 0:
            await asyncio.sleep(1)

        workers -= 1
        p = None
        try:
            p = await asyncio.create_subprocess_exec(
                program, *args, stdout=output_file, stderr=output_file
            )
            await asyncio.wait_for(p.communicate(), timeout=TIMEOUT)
        except asyncio.TimeoutError:
            print("Error: Timeout!")
        try:
            p.kill()
        except:
            pass

        workers += 1
        count += 1
        print("Finish program %s No. %s" % (run_program, count))


def cli_async(f):
    @functools.wraps(f)
    def wrapper(*args, **kwargs):
        return asyncio.run(f(*args, **kwargs))

    return wrapper


def foo(
        trace_type, run_program,
        total_btb_ways, total_btb_entries,
        generate, app_list: list,
        use_default_btb_record: bool,
        train_total_btb_ways, train_total_btb_entries,
        twig_profile: bool,
        use_twig_prefetch: bool
):
    pgo = False
    if trace_type == "ipc1":
        data_root = file_root / "ipc1_public"
        pt = False
        champsim_pt = False
    elif trace_type == "pt":
        data_root = file_root / "pt_traces"
        pt = True
        champsim_pt = False
    elif trace_type == "champsim-pt":
        data_root = file_root / "champsim_pt_traces"
        pt = False
        champsim_pt = True
    elif trace_type == "pgo":
        data_root = file_root / "pgo_traces"
        pt = False
        champsim_pt = False
        pgo = True
    else:
        assert False

    for trace_name in app_list:
        trace_file = data_root / trace_name
        yield run(
            str(trace_file),
            run_program,
            pt,
            total_btb_ways,
            total_btb_entries,
            champsim_pt,
            generate,
            pgo=pgo,
            use_default_btb_record=use_default_btb_record,
            train_total_btb_ways=train_total_btb_ways,
            train_total_btb_entries=train_total_btb_entries,
            twig_profile=twig_profile,
            use_twig_prefetch=use_twig_prefetch,
        )


@click.command()
@click.option(
    "--trace-type",
    type=click.Choice(["ipc1", "pt", "both", "champsim-pt", "pgo"], case_sensitive=False),
)
@click.option("--total-btb-ways", default="4", type=click.STRING)
@click.option("--total-btb-entries", default="8", type=click.STRING)
@click.option("--run-list", type=click.STRING)
@click.option("--generate", default=False, type=bool)
@click.option("--app-list", default="", type=click.STRING)
@click.option("--use-default-btb-record", type=bool)
@click.option("--train-total-btb-ways", default="4", type=click.STRING)
@click.option("--train-total-btb-entries", default="8", type=click.STRING)
@click.option("--twig-profile", default=False, type=bool)
@click.option("--use-twig-prefetch", default=False, type=bool)
@cli_async
async def main(
        trace_type: str,
        total_btb_ways: str,
        total_btb_entries: str,
        run_list: str,
        generate: bool,
        app_list: str,
        use_default_btb_record: bool,
        train_total_btb_ways: str,
        train_total_btb_entries: str,
        twig_profile: bool,
        use_twig_prefetch: bool
):
    tasks = []
    total_btb_ways_arr = total_btb_ways.split(",")
    total_btb_entries_arr = total_btb_entries.split(",")
    train_total_btb_ways_arr = train_total_btb_ways.split(",")
    train_total_btb_entries_arr = train_total_btb_entries.split(",")
    run_list_arr = run_list.split(",")
    if app_list == "":
        app_list = "cassandra,clang,drupal,finagle-chirper,finagle-http,kafka,mediawiki,mysqllarge,pgbench,python,tomcat,verilator,wordpress"
    app_list_arr = app_list.split(",")
    for _total_btb_ways in total_btb_ways_arr:
        _total_btb_ways = _total_btb_ways.strip()
        for _total_btb_entries in total_btb_entries_arr:
            _total_btb_entries = _total_btb_entries.strip()
            for _train_total_btb_ways in train_total_btb_ways_arr:
                for _train_total_btb_entries in train_total_btb_entries_arr:
                    if not use_default_btb_record:
                        if _total_btb_ways != _train_total_btb_ways or _total_btb_entries != _train_total_btb_entries:
                            print("Not default, train and size not match: ", _total_btb_ways, _train_total_btb_ways, _total_btb_entries, _train_total_btb_entries)
                            continue
                    for run_program in run_list_arr:
                        print(run_program)
                        if trace_type == "pgo":
                            tasks.extend(
                                foo(
                                    "pgo",
                                    run_program,
                                    _total_btb_ways,
                                    _total_btb_entries,
                                    generate,
                                    app_list=app_list_arr,
                                    use_default_btb_record=use_default_btb_record,
                                    train_total_btb_ways=_train_total_btb_ways,
                                    train_total_btb_entries=_train_total_btb_entries,
                                    twig_profile=twig_profile,
                                    use_twig_prefetch=use_twig_prefetch,
                                )
                            )
                        if trace_type == "ipc1" or trace_type == "both":
                            tasks.extend(
                                foo(
                                    "ipc1",
                                    run_program,
                                    _total_btb_ways,
                                    _total_btb_entries,
                                    generate,
                                    app_list=app_list_arr,
                                    use_default_btb_record=use_default_btb_record,
                                    train_total_btb_ways=_train_total_btb_ways,
                                    train_total_btb_entries=_train_total_btb_entries,
                                    use_twig_prefetch=use_twig_prefetch,
                                )
                            )
                        if trace_type == "pt" or trace_type == "both":
                            tasks.extend(
                                foo(
                                    "pt",
                                    run_program,
                                    _total_btb_ways,
                                    _total_btb_entries,
                                    generate,
                                    app_list=app_list_arr,
                                    use_default_btb_record=use_default_btb_record,
                                    train_total_btb_ways=_train_total_btb_ways,
                                    train_total_btb_entries=_train_total_btb_entries,
                                    twig_profile=twig_profile,
                                    use_twig_prefetch=use_twig_prefetch,
                                )
                            )
                        if trace_type == "champsim-pt":
                            tasks.extend(
                                foo(
                                    "champsim-pt",
                                    run_program,
                                    _total_btb_ways,
                                    _total_btb_entries,
                                    generate,
                                    app_list=app_list_arr,
                                    use_default_btb_record=use_default_btb_record,
                                    train_total_btb_ways=_train_total_btb_ways,
                                    train_total_btb_entries=_train_total_btb_entries,
                                    twig_profile=twig_profile,
                                    use_twig_prefetch=use_twig_prefetch,
                                )
                            )

    await asyncio.gather(*tasks)


if __name__ == "__main__":
    main()
