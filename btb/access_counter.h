//
// Created by Shixin Song on 2021/6/5.
//

#ifndef CHAMPSIM_PT_ACCESS_COUNTER_H
#define CHAMPSIM_PT_ACCESS_COUNTER_H

#include "ooo_cpu.h"
#include <vector>
#include <map>
#include <string>
#include <iostream>
#include <utility>

using std::vector;
using std::map;
using std::string;
using std::endl;
using std::cout;
using std::pair;

struct AccessEntry {
    uint64_t ip = 0;
    uint64_t counter = 0;
    uint64_t branch_target = 0;
    uint8_t branch_type = 0;

    AccessEntry() = default;

    AccessEntry(uint64_t ip, uint64_t branch_target, uint8_t branch_type) :
            ip(ip),
            counter(0),
            branch_target(branch_target),
            branch_type(branch_type) {}

    uint64_t distance() {
        return ip > branch_target ? ip - branch_target : branch_target - ip;
    }
};

struct CountEntry {
    uint64_t distance_boundary = 1 << 12;
    map<uint8_t, uint64_t> type_counter; // Access counter of all types
    vector<uint64_t> distance_counter;

    CountEntry() {
        for (auto i : {BRANCH_DIRECT_JUMP, BRANCH_CONDITIONAL, BRANCH_DIRECT_CALL}) {
            type_counter.emplace(i, 0);
        }
        distance_counter.resize(2, 0);
    }

    ostream &print(ostream &os) const {
        uint64_t total = 0;
        for (auto &it : type_counter) {
            total += it.second;
        }
        os << total;
        for (auto &it : type_counter) {
            os << "," << it.second;
        }
        for (auto a : distance_counter) {
            os << "," << a;
        }
        return os;
    }

    void increment(AccessEntry &entry) {
        type_counter[entry.branch_type]++;
        if (entry.distance() < distance_boundary) {
            distance_counter.front()++;
        } else {
            distance_counter.back()++;
        }
    }
};

ostream &operator<<(ostream &os, const CountEntry &count_entry) {
    return count_entry.print(os);
}

class AccessCounter {
private:
    uint64_t total_sets;
    uint64_t total_ways;
    vector<map<uint64_t, CountEntry>> all_count;
    vector<vector<map<uint64_t, AccessEntry>>> btb_access_counter; // first is type, second is counter

public:
    AccessCounter(uint64_t sets, uint64_t ways) : total_sets(sets), total_ways(ways) {
        all_count.resize(NUM_CPUS);
        btb_access_counter.resize(
                NUM_CPUS,
                vector<map<uint64_t, AccessEntry>>(
                        sets,
                        map<uint64_t, AccessEntry>()));
    }

    uint64_t get_set_index(uint64_t ip) {
        return ((ip >> 2) & (total_sets - 1));
    }

    void insert(uint64_t ip, uint64_t cpu, uint64_t branch_target, uint8_t branch_type) {
        auto set = get_set_index(ip);
        auto it = btb_access_counter[cpu][set].find(ip);
        if (it != btb_access_counter[cpu][set].end()) cout << "IP " << ip << endl;
        assert(it == btb_access_counter[cpu][set].end());
        btb_access_counter[cpu][set].emplace_hint(it, ip, AccessEntry(ip, branch_target, branch_type));
    }

    void access(uint64_t ip, uint64_t cpu) {
        auto set = get_set_index(ip);
        auto it = btb_access_counter[cpu][set].find(ip);
        assert(it != btb_access_counter[cpu][set].end());
        it->second.counter++;
    }

    void evict(uint64_t ip, uint64_t cpu) {
        auto set = get_set_index(ip);
        auto it = btb_access_counter[cpu][set].find(ip); // first: ip, second: branch type + access count
        assert(it != btb_access_counter[cpu][set].end());
        // it->second.first: branch type; it->second.second: access count
        auto ct = all_count[cpu].find(it->second.counter); // first: access count second: current count count
        if (ct == all_count[cpu].end()) {
            all_count[cpu].emplace_hint(ct, it->second.counter, CountEntry());
            ct = all_count[cpu].find(it->second.counter);
        }
//        ct->second++;
        ct->second.increment(it->second);
        btb_access_counter[cpu][set].erase(it);
    }

    void print_final_stats(uint64_t cpu) {
        cout << "BTB Access Count Histogram" << endl;
        cout << "Access count,Num of Branches,BRANCH_DIRECT_JUMP,BRANCH_CONDITIONAL,BRANCH_DIRECT_CALL,Small,Large" << endl;
        for (auto &it : all_count[cpu]) {
            cout << it.first << "," << it.second << endl;
        }
        cout << "End BTB Access Count Histogram" << endl;
    }
};


#endif //CHAMPSIM_PT_ACCESS_COUNTER_H
