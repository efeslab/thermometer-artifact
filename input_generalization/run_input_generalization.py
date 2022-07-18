import asyncio
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

workers = 75
count = 0
TIMEOUT = 36000


def cli_async(f):
    @wraps(f)
    def wrapper(*args, **kwargs):
        return asyncio.run(f(*args, **kwargs))

    return wrapper


async def run(
    program_path: Path,
    run_program: str,
    trace_file: Path,
    big_output_dir: Path,
    opt_generate: bool,
    pt: bool,
    input_generalization: str,
    twig_profile=False,
    use_twig_prefetch=False,
    **kwargs
):
    programs = []
    if opt_generate:
        programs.append(program_path / (run_program + "_generate"))
    programs.append(program_path / run_program)

    for program in programs:
        args = [str(program)]
        for key, value in kwargs.items():
            args.extend(["-" + key, value])
        if pt:
            args.append("-pt")
        args.extend(["-input_generalization", input_generalization])
        args.extend(["-traces", str(trace_file)])

        if twig_profile:
            args.extend(["-twig"])

        if use_twig_prefetch:
            args.extend(["-twig_prefetch"])

        print(" ".join(args))

        global workers, count
        while workers <= 0:
            await asyncio.sleep(1)

        workers -= 1
        output_dir = big_output_dir / program.name / trace_file.parent.parent.name
        output_dir.mkdir(exist_ok=True, parents=True)
        output_path = output_dir / (
            trace_file.stem + "_" + input_generalization + ".txt"
        )
        print(str(output_path))
        with output_path.open(mode="w") as output_file:
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
            print(
                "Finish program %s No. %s with input file %s"
                % (program, count, str(trace_file))
            )


def choose_largest_traces(trace_dir: Path, choose_num: int):
    return sorted(trace_dir.iterdir(), key=lambda x: x.stat().st_size, reverse=True)[
        0:choose_num
    ]


async def twig_profile_round(
        app_list: list,
        program_path: Path,
        all_app_dir: Path,
        big_output_dir: Path,
        opt_generate: bool,
        pt: bool,
        **kwargs
):
    tasks = []
    for app_name in app_list:
        application = all_app_dir / app_name
        chosen_traces = choose_largest_traces(application / "traces", 10)
        tasks.extend(
            [
                run(
                    program_path=program_path,
                    run_program="ChampSim_fdip_lru",
                    trace_file=trace,
                    big_output_dir=big_output_dir,
                    opt_generate=opt_generate,
                    pt=pt,
                    input_generalization=trace.stem,
                    twig_profile=True,
                    **kwargs
                )
                for trace in chosen_traces
            ]
        )
    await asyncio.gather(*tasks)


async def first_round(
    app_list: list,
    program_path: Path,
    all_app_dir: Path,
    big_output_dir: Path,
    opt_generate: bool,
    pt: bool,
    **kwargs
):
    tasks = []
    for app_name in app_list:
        application = all_app_dir / app_name
        chosen_traces = choose_largest_traces(application / "traces", 10)
        tasks.extend(
            [
                run(
                    program_path=program_path,
                    run_program="ChampSim_fdip_opt",
                    trace_file=trace,
                    big_output_dir=big_output_dir,
                    opt_generate=opt_generate,
                    pt=pt,
                    input_generalization=trace.stem,
                    **kwargs
                )
                for trace in chosen_traces
            ]
        )
        tasks.extend(
            [
                run(
                    program_path=program_path,
                    run_program="ChampSim_fdip_srrip",
                    trace_file=trace,
                    big_output_dir=big_output_dir,
                    opt_generate=False,
                    pt=pt,
                    input_generalization=trace.stem,
                    **kwargs
                )
                for trace in chosen_traces
            ]
        )
    await asyncio.gather(*tasks)


async def second_round(
    app_list: list,
    program_path: Path, all_app_dir: Path, big_output_dir: Path, pt: bool, **kwargs
):
    tasks = []
    for app_name in app_list:
        application = all_app_dir / app_name
        chosen_traces = choose_largest_traces(application / "traces", 10)
        for sim_trace in chosen_traces:
            for train_trace in chosen_traces:
                tasks.append(
                    run(
                        program_path=program_path,
                        run_program="ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru",
                        trace_file=sim_trace,
                        big_output_dir=big_output_dir,
                        opt_generate=False,
                        pt=pt,
                        input_generalization=train_trace.stem,
                        **kwargs
                    )
                )
    await asyncio.gather(*tasks)


@click.command()
@click.option(
    "--program-path",
    default=Path("/home/shixins/champsim-pt/cmake-build-release"),
    type=pathlib.Path,
)
@click.option(
    "--all-app-dir", default=Path("/mnt/storage/takh/pgp/workloads"), type=pathlib.Path
)
@click.option(
    "--big-output-dir",
    default=Path(
        "/mnt/storage/shixins/champsim_pt/input_generalization/simulation_output"
    ),
    type=pathlib.Path,
)
@click.option("--warmup-instructions", default="50000000", type=click.STRING)
@click.option("--simulation-instructions", default="50000000", type=click.STRING)
@click.option("--total-btb-ways", default="4", type=click.STRING)
@click.option("--opt-generate", default=False, type=bool)
@click.option(
    "--simulation-round",
    type=click.Choice(["first", "second", "both", "twig"], case_sensitive=False),
    default="both",
)
@click.option("--app-list", type=click.STRING)
@cli_async
async def main(
    program_path: Path,
    all_app_dir: Path,
    big_output_dir: Path,
    warmup_instructions: str,
    simulation_instructions: str,
    total_btb_ways: str,
    opt_generate: bool,
    simulation_round: str,
    app_list: str,
):
    big_output_dir.mkdir(exist_ok=True, parents=True)
    app_list_arr = app_list.split(",")
    if simulation_round == "twig":
        await twig_profile_round(
            app_list=app_list_arr,
            program_path=program_path,
            all_app_dir=all_app_dir,
            big_output_dir=big_output_dir,
            opt_generate=opt_generate,
            pt=True,
            warmup_instructions=warmup_instructions,
            simulation_instructions=simulation_instructions,
            total_btb_ways=total_btb_ways,
        )
    if simulation_round == "first" or simulation_round == "both":
        await first_round(
            app_list=app_list_arr,
            program_path=program_path,
            all_app_dir=all_app_dir,
            big_output_dir=big_output_dir,
            opt_generate=opt_generate,
            pt=True,
            warmup_instructions=warmup_instructions,
            simulation_instructions=simulation_instructions,
            total_btb_ways=total_btb_ways,
        )

    if simulation_round == "second" or simulation_round == "both":
        await second_round(
            app_list=app_list_arr,
            program_path=program_path,
            all_app_dir=all_app_dir,
            big_output_dir=big_output_dir,
            pt=True,
            warmup_instructions=warmup_instructions,
            simulation_instructions=simulation_instructions,
            total_btb_ways=total_btb_ways,
        )


if __name__ == "__main__":
    main()
