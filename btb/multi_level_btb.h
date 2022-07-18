//
// Created by shixinsong on 10/30/21.
//

#ifndef CHAMPSIM_PT_MULTI_LEVEL_BTB_H
#define CHAMPSIM_PT_MULTI_LEVEL_BTB_H

#include <vector>
#include <unordered_map>
#include <utility>
#include <limits>

using std::vector;
using std::unordered_map;
using std::pair;

struct BTBEntry {
    uint64_t ip_tag = 0;
    uint64_t target = 0;
    uint8_t branch_type = 0;
    uint8_t always_taken = 1;

    BTBEntry() = default;

    BTBEntry(uint64_t ip, uint64_t target, uint8_t branch_type) : ip_tag(ip), target(target), branch_type(branch_type) {}
};

template<class BTBEntryType>
class BTB {
protected:
    static_assert(std::is_base_of<BTBEntry, BTBEntryType>::value, "Template parameter BTBEntryType must be subclass of BTBEntry");
    uint64_t total_sets = 0;
    uint64_t total_ways = 0;
    vector<vector<unordered_map<uint64_t, BTBEntryType>>> btb;

public:
    uint64_t latency = 0;

    BTB(uint64_t latency, uint64_t sets, uint64_t ways) : latency(latency), total_sets(sets), total_ways(ways) {
        btb.resize(NUM_CPUS, vector<unordered_map<uint64_t, BTBEntryType>>(sets));
    }

    virtual void init(uint64_t sets, uint64_t ways) {
        // Maybe useless
        total_sets = sets;
        total_ways = ways;
        btb.resize(NUM_CPUS, vector<unordered_map<uint64_t, BTBEntryType>>(sets));
    }

    virtual void init_record(string &trace_name) {}

    uint64_t find_set_index(uint64_t ip) {
        return ((ip >> 2) % total_sets);
    }

    BTBEntryType *find_btb_entry(uint64_t ip, uint64_t cpu) {
        auto set_index = find_set_index(ip);
        auto it = btb[cpu][set_index].find(ip);
        return it == btb[cpu][set_index].end() ? nullptr : &(it->second);
    }

    // Return value is the evicted branch basic info. If nothing is evicted, return entry.ip = 0.
    virtual BTBEntry insert(uint64_t ip, uint64_t branch_target, uint8_t branch_type, uint64_t cpu, O3_CPU *ooo_cpu) {
        return {};
    }

    virtual void update(uint64_t ip, uint64_t cpu) {}

    void remove(uint64_t ip, uint64_t cpu) {
        auto set_index = find_set_index(ip);
        btb[cpu][set_index].erase(ip);
    }
};

template<class BTBEntryType, class BTBType = BTB<BTBEntryType>>
class MultiLevelBTB {
protected:
    static_assert(std::is_base_of<BTB<BTBEntryType>, BTBType>::value, "Template parameter BTBType must be subclass of BTB");
    vector<BTBType> btbs;

public:
    MultiLevelBTB() = default;

    explicit MultiLevelBTB(vector<vector<uint64_t>> &level_info) {
        for (auto &level : level_info) {
            btbs.emplace_back(level[0], level[1], level[2]);
        }
    }

    void init_record(string &trace_name) {
        for (auto &level_btb : btbs) {
            level_btb.init_record(trace_name);
        }
    }

    pair<BTBEntryType *, uint64_t> find_btb_entry(uint64_t ip, uint64_t cpu) {
//        cout << "Before multi find" << endl;
        for (auto &level_btb : btbs) {
            auto btb_entry = level_btb.find_btb_entry(ip, cpu);
            if (btb_entry != nullptr) {
                return std::make_pair(btb_entry, level_btb.latency);
            }
        }
        return std::make_pair(nullptr, std::numeric_limits<uint64_t>::max());
//        cout << "After multi find" << endl;
    }

    bool insert(uint64_t ip, uint64_t branch_target, uint8_t branch_type, uint64_t cpu, O3_CPU *ooo_cpu, uint64_t lower_bound = 0, uint64_t upper_bound = std::numeric_limits<uint64_t>::max()) {
//        cout << "Before multi insert" << endl;
        // Search from in level in the range [lower_level, upper_level) (the smallest level can be inserted).
        for (uint64_t i = lower_bound; i < std::min(btbs.size(), upper_bound); i++) {
            auto victim = btbs[i].insert(ip, branch_target, branch_type, cpu, ooo_cpu);
            if (victim.ip_tag != 0) {
                // Victim is evicted from level = i. Try to insert it into higher level.
                insert(victim.ip_tag, victim.target, victim.branch_type, cpu, ooo_cpu, i + 1, upper_bound);
//                cout << "After multi insert" << endl;
                return true;
            }
            // Check whether ip has been inserted into the cache
            if (find_btb_entry(ip, cpu).first != nullptr) {
                return true;
            }
            // ip has not been inserted into the cache, continue trying to insert into the next level.
        }
//        cout << "After multi insert" << endl;
        return false;
    }

    void update(uint64_t ip, uint64_t cpu, O3_CPU *ooo_cpu) {
//        cout << "Before multi update" << endl;
        uint64_t i;
        BTBEntryType *btb_entry;
        for (i = 0; i < btbs.size(); i++) {
            btb_entry = btbs[i].find_btb_entry(ip, cpu);
            if (btb_entry != nullptr) {
                break;
            }
        }
        assert(i < btbs.size() && btb_entry != nullptr);
        // ip is currently in level i btb.
        // Try to inert into smaller level (start from 0) !!!
        if (i == 0 || !insert(ip, btb_entry->target, btb_entry->branch_type, cpu, ooo_cpu, 0, i)) {
            // Entry is already in the lowest level, or cannot be inserted into the lower one
            btbs[i].update(ip, cpu);
        } else {
            // Entry has been inserted to the lower level, so remove it from the current level.
            btbs[i].remove(ip, cpu);
        }
//        cout << "After multi update" << endl;
    }
};

#endif //CHAMPSIM_PT_MULTI_LEVEL_BTB_H
