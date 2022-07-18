//
// Created by Shixin Song on 2021/7/26.
//

#ifndef CHAMPSIM_PT_ACCURACY_H
#define CHAMPSIM_PT_ACCURACY_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <limits>
#include <fstream>
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

using std::vector;
using std::unordered_map;
using std::unordered_set;
using std::set;

class CoverageAccuracy {
    // TODO: Only for single core CPU!
    vector<unordered_map<uint64_t, uint64_t>> access_record; // key: timestamp, value: ip
    unordered_map<uint64_t, set<uint64_t>> future_accesses;
//    vector<uint64_t> victim_reuse_distance;
    uint64_t total_sets;
    uint64_t total_ways;

    uint64_t friendly_count = 0;
    uint64_t unfriendly_count = 0;

public:
    void init(FILE *demand_record, uint64_t num_sets, uint64_t num_ways) {
        assert(demand_record != nullptr);
        total_sets = num_sets;
        total_ways = num_ways;
        access_record.resize(num_sets);
        uint64_t ip, counter;
        while (true) {
            if (fscanf(demand_record, "%llu %llu", &ip, &counter) == EOF) break;
            auto it = future_accesses.find(ip);
            if (it == future_accesses.end()) {
                future_accesses.emplace_hint(it, ip, set<uint64_t>());
            }
            future_accesses[ip].insert(counter);
            access_record[find_set_index(ip)].emplace(counter, ip);
        }
        rewind(demand_record);
    }

    uint64_t find_set_index(uint64_t ip) {
        return ((ip >> 2) & (total_sets-1));
    }

    uint64_t get_reuse_distance(uint64_t ip, uint64_t curr_time, bool by_pass) {
        auto it = future_accesses[ip].upper_bound(curr_time);
        if (it == future_accesses[ip].end()) {
//            victim_reuse_distance.push_back(std::numeric_limits<uint64_t>::max());
            unfriendly_count++;
            return std::numeric_limits<uint64_t>::max();
        }
        // *it is the next time when ip is accessed.
        unordered_set<uint64_t> unique_cache_lines;
        auto start = curr_time + 1;
        for (uint64_t i = start; i < *it; i++) {
            auto taken_access_it = access_record[find_set_index(ip)].find(i);
            if (taken_access_it != access_record[find_set_index(ip)].end()) {
                unique_cache_lines.insert(taken_access_it->second);
            }
        }
//        victim_reuse_distance.push_back(unique_cache_lines.size());
        if (unique_cache_lines.size() < total_ways) {
            friendly_count++;
        } else {
            unfriendly_count++;
        }
        return unique_cache_lines.size();
    }

    void print_final_stats(string &trace_name, string &program_name, uint64_t total_btb_ways) {
//        auto short_name = O3_CPU::find_trace_short_name(trace_name, O3_CPU::NameKind::TRAIN);
//        fs::path coverage_accuracy_path = "/mnt/storage/shixins/champsim_pt/coverage_accuracy";
//        string sub_dir = program_name + std::to_string(total_btb_ways);
//        fs::create_directory(coverage_accuracy_path / sub_dir);
//        auto filename = coverage_accuracy_path / sub_dir / (short_name + ".csv");
//        cout << "Print coverage accuracy raw data into " << filename << endl;
//        ofstream out(filename.c_str());
//        assert(out.is_open());
//        for (auto a : victim_reuse_distance) {
//            out << a << endl; // Decimal form.
//        }
        cout << "Victim friendly count: " << friendly_count << endl;
        cout << "Victim unfriendly count: " << unfriendly_count << endl;
        cout << "Eviction accuracy: " << (double) unfriendly_count / (double) (friendly_count + unfriendly_count) << endl;
    }
};

#endif //CHAMPSIM_PT_ACCURACY_H
