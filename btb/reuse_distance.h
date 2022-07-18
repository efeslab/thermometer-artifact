//
// Created by Shixin Song on 2021/6/5.
//

#ifndef CHAMPSIM_PT_REUSE_DISTANCE_H
#define CHAMPSIM_PT_REUSE_DISTANCE_H

#include "ooo_cpu.h"
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

using std::vector;
using std::map;
using std::unordered_map;
using std::unordered_set;
using std::endl;

struct ReuseEntry {
    uint64_t ip = 0;
    uint64_t target = 0;
    uint8_t branch_type = BRANCH_CONDITIONAL; // Remember to convert to int when print!
    unordered_set<uint64_t> distance_set;
    vector<uint64_t> reuse_distance_record;

    ReuseEntry() = default;

    ReuseEntry(const ReuseEntry &other) {
        ip = other.ip;
        target = other.target;
        branch_type = other.branch_type;
    }

    ReuseEntry(uint64_t ip, uint64_t target, uint8_t branch_type) :
            ip(ip), target(target), branch_type(branch_type) {}

    void access(uint64_t access_ip) {
        if (access_ip == ip) {
            // Clear distance_set and add size to reuse_distance_record
            reuse_distance_record.push_back(distance_set.size());
            distance_set.clear();
        } else {
            distance_set.insert(access_ip);
        }
    }
};

class ReuseDistance {
    uint64_t total_sets;
    vector<map<uint64_t, ReuseEntry>> sorted_branch_record;
    vector<vector<unordered_set<uint64_t>>> btb_view_branch_record;

    bool record_reuse_distance = false;

public:
    ReuseDistance(uint64_t sets, bool record_reuse_distance) : total_sets(sets), record_reuse_distance(record_reuse_distance) {
        // TODO: Init btb_view_branch_record and sorted_branch_record
        sorted_branch_record.resize(NUM_CPUS);
        btb_view_branch_record.resize(NUM_CPUS, vector<unordered_set<uint64_t>>(sets));
    }

    uint64_t get_set_index(uint64_t ip) {
        return ((ip >> 2) & (total_sets - 1));
    }

    void access(uint64_t access_ip, uint64_t target, uint8_t branch_type, uint64_t cpu) {
        if (!record_reuse_distance) {
            return;
        }
        // TODO: Get set index of access_ip and update all branches mapped onto this set
        auto set = get_set_index(access_ip);
        // Add to branch_record if needed
        btb_view_branch_record[cpu][set].insert(access_ip);
        auto it = sorted_branch_record[cpu].find(access_ip);
        if (it == sorted_branch_record[cpu].end()) {
            sorted_branch_record[cpu].emplace_hint(it, access_ip, ReuseEntry(access_ip, target, branch_type));
        }
        for (auto ip : btb_view_branch_record[cpu][set]) {
            sorted_branch_record[cpu][ip].access(access_ip);
        }
    }

    void print_final_stats(string &trace_name, uint64_t cpu) {
        if (!record_reuse_distance) {
            return;
        }
        auto short_name = O3_CPU::find_trace_short_name(trace_name, O3_CPU::NameKind::TRACE);
        fs::create_directory("/mnt/storage/shixins/champsim_pt/reuse_distance_taken");
        string filename = "/mnt/storage/shixins/champsim_pt/reuse_distance_taken/" + short_name + ".csv";
        cout << "Open reuse distance file " << filename << endl;
        ofstream out(filename.c_str());
        assert(out.is_open());
        out << "PC,Target,Type,Num of Access,Reuse Distance" << endl;
        for (auto &it : sorted_branch_record[cpu]) {
            out << std::hex << it.first;
            out << ",";
            out << std::hex << it.second.target;
            out << "," << (int) it.second.branch_type << "," << it.second.reuse_distance_record.size();
            for (auto a : it.second.reuse_distance_record) {
                out << "," << a;
            }
            out << endl;
        }
        out.close();
    }
};

#endif //CHAMPSIM_PT_REUSE_DISTANCE_H
