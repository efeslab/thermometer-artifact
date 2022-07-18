from pathlib import Path
import pandas as pd
import click
import plot_functions


def main():
    ipc1_result_analysis_dir = Path("/home/shixinsong/Desktop/UM/SURE/plots/simulation_result/ipc1_correct_new_btb_result_analysis")
    # hwc_df = pd.read_csv(
    #     ipc1_result_analysis_dir / "ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru4.csv",
    #     index_col=0,
    #     header=0
    # )
    # srrip_df = pd.read_csv(
    #     ipc1_result_analysis_dir / "ChampSim_fdip_srrip4.csv",
    #     index_col=0,
    #     header=0,
    # )
    # speedup = sorted((100 * (hwc_df["IPC"] - srrip_df["IPC"]) / srrip_df["IPC"]).array)
    # result_df = pd.DataFrame(
    #     [speedup], index=["IPC Speedup"]
    # ).transpose()
    # print(result_df)
    # output_dir = Path("/home/shixinsong/Desktop/UM/SURE/plots/paper_results/ipc1")
    # output_dir.mkdir(exist_ok=True)
    # result_df.to_csv(output_dir / "thermometer-ghrp-s-curve.csv")
    # plot_functions.plot_bar_chart(
    #     output_path=output_dir / "thermometer-ghrp-s-curve.pdf",
    #     df=result_df,
    #     ytitle="Speedup (\%)",
    #     xtitle="50 IPC-1 traces",
    #     show_legend=False,
    #     plot_type="line",
    #     add_average=False
    # )

    all_df = {}
    all_df["Thermometer"] = pd.read_csv(
        ipc1_result_analysis_dir / "ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru4.csv",
        index_col=0,
        header=0
    )
    for repl_policy in ["LRU", "SRRIP", "GHRP", "Hawkeye", "OPT"]:
        all_df[repl_policy] = pd.read_csv(
            ipc1_result_analysis_dir / f"ChampSim_fdip_{repl_policy.lower()}4.csv",
            index_col=0,
            header=0,
        )
    repl_list = ["SRRIP", "GHRP", "Hawkeye", "Thermometer", "OPT"]
    opt_sort_speedup_df = pd.DataFrame()
    for repl_policy in repl_list:
        opt_sort_speedup_df[repl_policy] = 100 * (all_df[repl_policy]["IPC"] - all_df["LRU"]["IPC"]) / all_df["LRU"]["IPC"]
    print(opt_sort_speedup_df)
    opt_sort_speedup_df.sort_values(by=["OPT"], inplace=True)
    print(opt_sort_speedup_df)
    opt_sort_speedup_df["Index"] = range(50)
    opt_sort_speedup_df.set_index("Index", inplace=True)
    print(opt_sort_speedup_df)
    output_dir = Path("/home/shixinsong/Desktop/UM/SURE/plots/paper_results/ipc1")
    output_dir.mkdir(exist_ok=True)
    opt_sort_speedup_df.to_csv(output_dir / "opt-sort-all-lru-s-curve.pdf")
    plot_functions.plot_bar_chart(
        output_path=output_dir / "opt-sort-all-lru-s-curve.pdf",
        df=opt_sort_speedup_df,
        ytitle="Speedup (\%)",
        xtitle="50 IPC-1 traces",
        show_legend=True,
        plot_type="line",
        add_average=False,
        cut_off_at_zero=True,
    )

    return

    speedup_data = []
    for repl_policy in repl_list:
        speedup_data.append(
            sorted((100 * (all_df[repl_policy]["IPC"] - all_df["LRU"]["IPC"]) / all_df["LRU"]["IPC"]).array)
        )
    all_speedup = pd.DataFrame(
        speedup_data,
        index=repl_list
    ).transpose()
    print(all_speedup)
    all_speedup.to_csv(output_dir / "all-lru-s-curve.csv")
    plot_functions.plot_bar_chart(
        output_path=output_dir / "all-lru-s-curve.pdf",
        df=all_speedup,
        ytitle="Speedup (\%)",
        xtitle="50 IPC-1 traces",
        show_legend=True,
        plot_type="line",
        add_average=False,
        cut_off_at_zero=True,
    )



if __name__ == '__main__':
    main()
