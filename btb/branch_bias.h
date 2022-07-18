//
// Created by Shixin Song on 2021/8/4.
//

#ifndef CHAMPSIM_PT_BRANCH_BIAS_H
#define CHAMPSIM_PT_BRANCH_BIAS_H

#include "ooo_cpu.h"
#include <vector>
#include <map>
#include <utility>

using std::vector;
using std::map;
using std::pair;

class BranchBias {
    vector<map<uint64_t, pair<uint64_t, uint64_t>>> branch_record;
    // Value: first is taken count, second is not taken count.
    uint64_t total_btb_ways;
    uint64_t total_btb_entries;

    bool record_branch_bias = false;
public:
    BranchBias(bool record_branch_bias) : record_branch_bias(record_branch_bias) {
        branch_record.resize(NUM_CPUS);
    }

    void init(uint64_t btb_ways, uint64_t btb_entries) {
        total_btb_ways = btb_ways;
        total_btb_entries = btb_entries;
    }

    void access(uint64_t ip, uint64_t cpu, bool taken) {
        if (!record_branch_bias) {
            return;
        }
        auto it = branch_record[cpu].find(ip);
        if (it == branch_record[cpu].end()) {
            branch_record[cpu].emplace_hint(it, ip, std::make_pair(0, 0));
            it = branch_record[cpu].find(ip);
        }
        if (taken) {
            it->second.first++;
        } else {
            it->second.second++;
        }
    }

    void print_final_stats(string &trace_name, uint64_t cpu) {
        if (!record_branch_bias) {
            return;
        }
        auto short_name = O3_CPU::find_trace_short_name(trace_name, O3_CPU::NameKind::TRACE);
        fs::path opt_access_record_path = "/mnt/storage/shixins/champsim_pt/branch_bias_record";
        fs::create_directory(opt_access_record_path);
        string sub_dir = "way" + std::to_string(total_btb_ways);
        if (total_btb_entries != 8 && total_btb_entries != 8192) {
            if (total_btb_entries % 1024 == 0) {
                sub_dir += ("_" + std::to_string(total_btb_entries / 1024) + "K");
            } else {
                sub_dir += ("_" + std::to_string(total_btb_entries) + "K");
            }
        }
        if (IFETCH_BUFFER_SIZE != 192) {
            sub_dir += ("_fdip" + std::to_string(IFETCH_BUFFER_SIZE));
        }
        fs::create_directory(opt_access_record_path / sub_dir);
        auto filename = opt_access_record_path / sub_dir / (short_name + ".csv");
        cout << "Open branch bias record " << filename << endl;
        ofstream out(filename.c_str());
        assert(out.is_open());
        out << "PC,Taken,NotTaken" << endl;
        for (auto it : branch_record[cpu]) {
            out << std::hex << it.first << "," << it.second.first << "," << it.second.second << endl;
        }
    }
};

#endif //CHAMPSIM_PT_BRANCH_BIAS_H
