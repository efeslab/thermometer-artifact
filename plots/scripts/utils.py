from pathlib import Path

scripts_dir = Path(__file__).parent
plots_dir = scripts_dir.parent
project_dir = plots_dir.parent

pt_traces = [
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
