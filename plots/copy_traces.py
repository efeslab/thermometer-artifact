import shutil
from pathlib import Path


def parse_trace_name(app: str, trace_path: Path):
    filename = trace_path.name
    print(filename)
    if app == "mysql" or app == "mysqllarge":
        return filename.split(".")[0].replace("largest_trace_mysql_1thread_20000tbSz_250tbNum_oltp_", "").replace("_", "-") + ".gz"
    elif app == "mysqlbolt":
        return filename.split(".")[0].replace("largest_trace_mysql_bolt_1thread_20000tbSz_250tbNum_oltp_", "").replace("_", "-") + ".gz"
    elif app == "nginx":
        return filename.replace("largest_trace_wrk_4threads_80_conn_60time_8000RPS_", "").replace("size_500fileCnt_1nginx_wkrs_1clients.gz", "") + ".gz"
    elif app == "postgres":
        return filename.replace("largest_trace_psql_query", "")
    elif app == "pgbench":
        return filename.replace("largest_trace_pt_pgbench_scale", "")
    elif app == "python":
        return filename.replace("_", "-")
    elif app == "clang":
        return filename
    else:
        assert 0


def choose_largest_traces(trace_dir: Path, choose_num: int):
    return sorted(trace_dir.iterdir(), key=lambda x: x.stat().st_size, reverse=True)[
           0:choose_num
           ]


def copy_files(input_dir: Path, output_dir: Path):
    (output_dir / "traces").mkdir(exist_ok=True, parents=True)
    app = output_dir.stem
    for trace_path in input_dir.glob("*.gz"):
        new_trace_name = parse_trace_name(app, trace_path)
        print(new_trace_name)
        new_trace_path = output_dir / "traces" / new_trace_name
        shutil.copy(trace_path, new_trace_path)
    chosen_traces = choose_largest_traces(output_dir / "traces", 1)
    shutil.copy(chosen_traces[0], output_dir / "trace.gz")


def main():
    pt_trace_big_dir = Path("/mnt/storage/shixins/champsim_pt/pt_traces")
    # copy_files(Path("/mnt/storage/sara/mysql_traces_ISCA22"), pt_trace_big_dir / "mysql")
    # copy_files(Path("/mnt/storage/sara/nginx_traces_ISCA22"), pt_trace_big_dir / "nginx")
    # copy_files(Path("/mnt/storage/sara/postgres_traces_ISCA22"), pt_trace_big_dir / "postgres")
    # copy_files(Path("/mnt/storage/sara/pgbench_traces_ISCA22"), pt_trace_big_dir / "pgbench")
    # copy_files(Path("/mnt/storage/sara/python_traces_ISCA22/original"), pt_trace_big_dir / "python")
    # copy_files(Path("/mnt/storage/sara/clang_traces_ISCA22"), pt_trace_big_dir / "clang")
    # copy_files(Path("/mnt/storage/sara/mysql_bolt_traces_ISCA22"), pt_trace_big_dir / "mysqlbolt")
    copy_files(Path("/mnt/storage/sara/mysql_large_traces_ISCA22"), pt_trace_big_dir / "mysqllarge")


if __name__ == '__main__':
    main()
