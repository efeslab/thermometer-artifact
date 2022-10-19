<h1 align="center"> Thermometer </h1>

Thermometer is a profile-guided BTB replacement technique.

Please cite the following [paper](https://web.eecs.umich.edu/~takh/papers/song-thermometer-isca-2022.pdf) if you use this artifact:
```
@inproceedings{song-thermometer-isca-2022,
  author = {Song, Shixin and Khan, Tanvir Ahmed and Mahdizadeh Shahri, Sara and Sriraman, Akshitha and Soundararajan, Niranjan K and Subramoney, Sreenivas and Jim{\'e}nez, Daniel A and Litz, Heiner and Kasikci, Baris},
  title = {Thermometer: Profile-Guided BTB Replacement for Data Center Applications},
  booktitle = {Proceedings of the 49th International Symposium on Computer Architecture (ISCA)},
  series = {ISCA 2022},
  year = {2022},
  video = {https://www.youtube.com/watch?v=YMqFlP0PtWw},
  code = {https://github.com/efeslab/thermometer-artifact},
  month = jun,
}
```

# Simulator and trace
Thermometer is implemented in a trace-based simulator, [ChampSim](https://github.com/ChampSim/ChampSim).
Clone the Thermometer repo with
```
git clone git@github.com:efeslab/thermometer-artifact.git
```
We use Intel Processor Trace to generate traces for widely-used data center applications. The traces can be downloaded from [here](https://drive.google.com/file/d/1tN8Jw1TcZ9CrDzDWK0HFUD-nVLhZDW9e/view).

**Update: We also released traces of four more apps [here](https://drive.google.com/file/d/1RJYNbMR4G3m40ZiaflJ2Ox_DFB8vsgWv/view?usp=sharing)**

[//]: # (TODO: there are only 9 traces on the google drive)

# Compile

## Dependency

First, you should make sure that a C++ compiler (with C++17 support), `cmake`, and `boost-filesystem` are installed. 
Python 3 is required to install the dependency below, as well as to run the scripts. 

The project also requires `intelxed` as a dependency. You can install it by

```bash
$ git clone https://github.com/intelxed/xed.git xed
$ git clone https://github.com/intelxed/mbuild.git mbuild
$ cd xed
$ python3 ./mfile.py --shared install
```

This will create a `kits` directory containing the compiled xed (both static and shared) libraries.

**Add the absolute path to `kits/xed-install-base-xxx` to your env variable `CMAKE_PREFIX_PATH` so that CMake can find this package.**

[//]: # (ChampSim takes a JSON configuration script. Examine `champsim_config.json` for a fully-specified example. All options described in this file are optional and will be replaced with defaults if not specified. The configuration scrip can also be run without input, in which case an empty file is assumed.)

## Build (Debug)

CMake will compile in debug mode by default. You can use this build to debug your code.

```bash
$ mkdir cmake-build-debug
$ cd cmake-build-debug
$ cmake ..
$ make -j
```

## Build (Release)

You can also add a flag to compile in the release mode. This is useful in the real experiments since the code is optimized for speed.

```bash
$ mkdir cmake-build-release
$ cd cmake-build-release
$ cmake -DCMAKE_BUILD_TYPE=Release ..
$ make -j
```

# Experiments
## Compiled executables
After successfully build all executables, you can find them in the directory `cmake-build-debug` or `cmake-build-release`. 
Different executables are corresponding to different experimental setup mentioned in the paper. 
We list corresponding names of major experiment setups in the following tables. 
For some experimental setups, the corresponding executable names can be found according to the experiment running scripts.

| Executable                                                              | BTB Replacement     | BTB Prefetch | Other Setup                            |
|-------------------------------------------------------------------------|---------------------|--------------|----------------------------------------|
| ChampSim_fdip_lru                                                       | LRU                 |              |                                        |
| ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru                | Thermometer         |              |                                        |
| ChampSim_fdip_opt                                                       | Optimal Replacement |              |                                        |
| ChampSim_fdip_srrip                                                     | SRRIP               |              |                                        |
| ChampSim_fdip_ghrp                                                      | GHRP                |              |                                        |
| ChampSim_fdip_hawkeye                                                   | Hawkeye             |              |                                        |
| ChampSim_fdip_predecoder_btb_lru                                        | LRU                 | Confluence   |                                        |
| ChampSim_fdip_shotgun_lru                                               | LRU                 | Shotgun      |                                        |
| ChampSim_fdip_predecoder_btb_hot_warm_cold_80_50_f_keep_curr_hotter_lru | Thermometer         | Confluence   |                                        |
| ChampSim_fdip_shotgun_hot_warm_cold_80_50_f_keep_curr_hotter_lru        | Thermometer         | Shotgun      |                                        |
| ChampSim_fdip_predecoder_btb_opt                                        | Optimal Replacement | Confluence   |                                        |
| ChampSim_fdip_shotgun_opt                                               | Optimal Replacement | Shotgun      |                                        |
| ChampSim_fdip_perfect_btb                                               |                     |              | No BTB Misses                          |
| ChampSim_fdip_perfect_bp                                                | LRU                 |              | Correct branch direction               |
| ChampSim_icache_lru                                                     |                     |              | No I-Cache Misses (very large I-Cache) |


## Run Experiments
Use the following script to run most experiments
```bash
run_test.py
```
Use the following script to generate result summary from raw data
```bash
python3 generate_result.py --trace-type=pt --total-btb-ways=4 --dir-list=ChampSim_fdip_lru,ChampSim_fdip_srrip,ChampSim_fdip_ghrp,ChampSim_fdip_hawkeye,ChampSim_fdip_opt,ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru,ChampSim_fdip_perfect_btb,ChampSim_fdip_perfect_bp,ChampSim_icache_lru,ChampSim_fdip_predecoder_btb_lru,ChampSim_fdip_shotgun_lru,ChampSim_fdip_predecoder_btb_hot_warm_cold_80_50_f_keep_curr_hotter_lru,ChampSim_fdip_shotgun_hot_warm_cold_80_50_f_keep_curr_hotter_lru,ChampSim_fdip_predecoder_btb_opt,ChampSim_fdip_shotgun_opt,7979ChampSim_fdip_opt,7979ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru
```

## Run Experiments and Generate Results
Since some Figures use the data from the same experiment, so please run the following commands in order to get required raw data to generate the final results.
### IPC Speedup and BTB Miss Reduction
First, run experiments using the script `run_test.py`.
```bash
# LRU, SRRIP, GHRP, Hawkeye
# Perfect BTB, Perfect Branch Prediction, Perfect I-Cache
# LRU + Confluence, LRU + Shotgun
python3 run_test.py --trace-type=pt --total-btb-ways=4 --run-list=ChampSim_fdip_lru,ChampSim_fdip_srrip,ChampSim_fdip_ghrp,ChampSim_fdip_hawkeye,ChampSim_fdip_perfect_btb,ChampSim_fdip_perfect_bp,ChampSim_icache_lru,ChampSim_fdip_predecoder_btb_lru,ChampSim_fdip_shotgun_lru
# Optimal Replacement,
# Optimal Replacement + Confluence, Optimal Replacement + Shotgun
python3 run_test.py --trace-type=pt --generate=True --total-btb-ways=4 --run-list=ChampSim_fdip_opt,ChampSim_fdip_predecoder_btb_opt,ChampSim_fdip_shotgun_opt
# Thermometer, Thermometer (holistic information only)
# Thermometer + Confluence, Thermometer + Shotgun
python3 run_test.py --trace-type=pt --total-btb-ways=4 --run-list=ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru,ChampSim_fdip_hwc_50_80_f_keep_curr_hotter,ChampSim_fdip_predecoder_btb_hot_warm_cold_80_50_f_keep_curr_hotter_lru,ChampSim_fdip_shotgun_hot_warm_cold_80_50_f_keep_curr_hotter_lru
# Optimal Replacement and Thermometer on BTB with 7979 entry
python3 run_test.py --trace-type=pt --generate=True --total-btb-ways=4 --total-btb-entries=7979 --run-list=ChampSim_fdip_opt
python3 run_test.py --trace-type=pt --total-btb-ways=4 --total-btb-entries=7979 --run-list=ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru
# Twig
#python3 run_test.py --trace-type=pt --total-btb-ways=4 --total-btb-entries=8 --run-list=ChampSim_fdip_lru --twig-profile=True
## TODO: One step of generating Twig result is missing
#python3 run_test.py --trace-type=pt --total-btb-ways=4 --total-btb-entries=8 --run-list=ChampSim_fdip_lru,ChampSim_fdip_srrip --use-twig-prefetch=True
#python3 run_test.py --trace-type=pt --total-btb-ways=4 --total-btb-entries=8 --run-list=ChampSim_fdip_opt --use-twig-prefetch=True
#python3 run_test.py --trace-type=pt --total-btb-ways=4 --total-btb-entries=8 --run-list=ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru --use-twig-prefetch=True
```
Second, generate result summary using the script `generate_result.py`.
```bash
python3 generate_result.py --trace-type=pt --total-btb-ways=4 --dir-list=ChampSim_fdip_lru,ChampSim_fdip_srrip,ChampSim_fdip_ghrp,ChampSim_fdip_hawkeye,ChampSim_fdip_opt,ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru,ChampSim_fdip_hwc_50_80_f_keep_curr_hotter,ChampSim_fdip_perfect_btb,ChampSim_fdip_perfect_bp,ChampSim_icache_lru,ChampSim_fdip_predecoder_btb_lru,ChampSim_fdip_shotgun_lru,ChampSim_fdip_predecoder_btb_opt,ChampSim_fdip_shotgun_opt,ChampSim_fdip_predecoder_btb_hot_warm_cold_80_50_f_keep_curr_hotter_lru,ChampSim_fdip_shotgun_hot_warm_cold_80_50_f_keep_curr_hotter_lru,7979ChampSim_fdip_opt,7979ChampSim_fdip_hot_warm_cold_80_50_f_keep_curr_hotter_lru
```
Then, calculate corresponding IPC speedup and generate plots. You can check comments in the script to find out map from figure number to file name and plot code.
```bash
# Fig. 1 3 4 11
python3 plots/scripts/plot_ipc_mpki.py
# Fig. 2
python3 plots/scripts/plot_functions.py
```
Finally, calculate and plot BTB miss reduction.
```bash
# Fig. 12
python3 plots/scripts/plot_btb_miss.py
```

### Reuse Distance Variance
```bash
# Fig. 5
python3 plots/scripts/plot_reuse_distance_variance.py
```

### Hit-to-taken percentage
```bash
# Fig. 6 7
python3 plots/scripts/plot_hit_access_count.py
```

### Correlation
```bash
# Fig. 8
python3 plots/scripts/plot_hwc_bias_corr.py
```

### Bypass
```bash
# Fig. 9
python3 plots/scripts/plot_opt_eviction.py
```

### Coverage
```bash
# Fig. 9
python3 plots/scripts/plot_coverage.py
```

### Accuracy
```bash
# Fig. 9
python3 plots/scripts/plot_accuracy.py
```

# Note
Some paths are hard-coded in the implementation and scripts, feel free to replace them with what you needed.
Most result data and plots are already included in this repo.
If you have any problems (e.g., scripts do not work. some results cannot be generated), feel free to contact us.

