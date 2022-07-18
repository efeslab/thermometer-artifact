
/*
 * This file implements a multi-level opt Branch Target Buffer (BTB) structure.
 * It uses a set-associative BTB to predict the targets of non-return branches,
 * and it uses a small Return Address Stack (RAS) to predict the target of
 * returns.
 */

#include "ooo_cpu.h"
#include "../accuracy.h"
#include "../prefetch_stream_buffer.h"
#include "../branch_bias.h"
#include "../multi_level_btb.h"
#include "../access_record.h"
#include <set>

using std::vector;
using std::set;

extern uint8_t total_btb_ways;
extern uint64_t total_btb_entries; // 1K, 2K...

//#define BASIC_BTB_SETS (total_btb_entries / total_btb_ways)
//#define BASIC_BTB_WAYS total_btb_ways
#define BASIC_BTB_INDIRECT_SIZE 4096
#define BASIC_BTB_RAS_SIZE 32
#define BASIC_BTB_CALL_INSTR_SIZE_TRACKERS 1024

//CoverageAccuracy coverage_accuracy;
StreamBuffer stream_buffer(32);
//BranchBias branch_bias;

vector<vector<uint64_t>> btb_level_info = {
        // Latency, total_sets, total_ways
        {0, 16, 1, (uint64_t) BTBType::NANO}, // nano BTB
        {1, 64, 1, (uint64_t) BTBType::MILI}, // mili BTB
        {2, 64, 2, (uint64_t) BTBType::L1}, // L1 BTB
        {3, 2048, 4, (uint64_t) BTBType::L2} // L2 BTB
};

class OPTBTB: public BTB<BTBEntry> {
    vector<vector<unordered_map<uint64_t, set<uint64_t>>>> future_accesses;
    vector<vector<unordered_map<uint64_t, set<uint64_t>>>> future_prefetches;
public:
    uint64_t last_timestamp = 0;
    uint64_t timestamp = 0;
    AccessRecord access_record;

    OPTBTB(uint64_t latency, uint64_t sets, uint64_t ways, BTBType btb_type) : BTB<BTBEntry>(latency, sets, ways),
                                                                               access_record(btb_type) {
        future_accesses.resize(NUM_CPUS, vector<unordered_map<uint64_t, set<uint64_t>>>(total_sets));
        future_prefetches.resize(NUM_CPUS, vector<unordered_map<uint64_t, set<uint64_t>>>(total_sets));
    }

    void read_record(uint64_t ip, uint64_t counter, uint8_t cpu) {
        auto set_index = find_set_index(ip);
        auto it = future_accesses[cpu][set_index].find(ip);
        if (it == future_accesses[cpu][set_index].end()) {
            future_accesses[cpu][set_index].emplace_hint(it, ip, set<uint64_t>());
        }
        future_accesses[cpu][set_index][ip].insert(counter);
    }

    BTBEntry insert(uint64_t ip, uint64_t branch_target, uint8_t branch_type, uint64_t cpu, O3_CPU *ooo_cpu) override {
//        cout << "Before insert single level" << endl;
        auto set_index = find_set_index(ip);
        assert(set_index < total_sets);
        BTBEntry victim;
        auto time = timestamp - 1;
        if (btb[cpu][set_index].size() >= total_ways) {
            // Evict a btb entry if it is full
            auto current_it = future_accesses[cpu][set_index][ip].upper_bound(time);
            if (current_it == future_accesses[cpu][set_index][ip].end()) {
                // There is no point caching the key
                return victim;
            }
            // First, find the one among the current set will be prefetched furthest
            bool prefetch_found = false;
            pair<uint64_t, uint64_t> prefetch_candidate;
            for (auto &entry : btb[cpu][set_index]) {
                auto it = future_prefetches[cpu][set_index][entry.first].upper_bound(time);
                if (it != future_prefetches[cpu][set_index][entry.first].end()) {
                    // Find the prefetch
                    // Check whether the prefetch is before the next load of this line
                    auto demand_it = future_accesses[cpu][set_index][entry.first].upper_bound(time);
                    if (demand_it != future_accesses[cpu][set_index][entry.first].end() && (*it) > (*demand_it)) {
                        // Demand load is present and prefetch comes after the first load, so ignore
                        continue;
                    }
                    if (prefetch_found) {
                        if (prefetch_candidate.first < *it) {
                            prefetch_candidate = make_pair(*it, entry.first);
                        }
                    } else {
                        prefetch_found = true;
                        prefetch_candidate = make_pair(*it, entry.first);
                    }
                }
            }
            if (prefetch_found) {
                victim = BTBEntry(prefetch_candidate.second, btb[cpu][set_index][prefetch_candidate.second].target, btb[cpu][set_index][prefetch_candidate.second].branch_type);
                btb[cpu][set_index].erase(prefetch_candidate.second);
                access_record.evict(prefetch_candidate.second, cpu);
            } else {
                // Find victim in future demand access
                pair<uint64_t, uint64_t> candidate;
                candidate.first = *current_it;
                candidate.second = ip;
                for (const auto &entry : btb[cpu][set_index]) {
                    auto it = future_accesses[cpu][set_index][entry.first].upper_bound(time);
                    if (it == future_accesses[cpu][set_index][entry.first].end()) {
                        candidate.first = last_timestamp + 1;
                        candidate.second = entry.first;
                    } else if (candidate.first < *it) {
                        candidate.first = *it;
                        candidate.second = entry.first;
                    }
                }
                if (candidate.second == ip) {
                    return victim;
                } else {
                    victim = BTBEntry(candidate.second, btb[cpu][set_index][candidate.second].target, btb[cpu][set_index][candidate.second].branch_type);
                    btb[cpu][set_index].erase(candidate.second);
                    access_record.evict(candidate.second, cpu);
                }
            }
        }
        btb[cpu][set_index].emplace(ip, BTBEntry(ip, branch_target, branch_type));
        return victim;
    }
};

class MultiLevelOPTBTB : public MultiLevelBTB<BTBEntry, OPTBTB> {
    uint64_t timestamp = 0;
public:
    explicit MultiLevelOPTBTB(vector<vector<uint64_t>> &level_info) : MultiLevelBTB() {
        for (auto &level : level_info) {
            btbs.emplace_back(level[0], level[1], level[2], (BTBType) level[3]);
        }
    }

    void read_record(FILE *demand_record, uint64_t cpu) {
        uint64_t ip, counter;
//        std::fseek(demand_record, 0, SEEK_END);
        while (true) {
            if (fscanf(demand_record, "%lu %lu", &ip, &counter) == EOF) break;
            for (auto &level_btb : btbs) {
                level_btb.read_record(ip, counter, cpu);
            }
        }
        for (auto &level_btb : btbs) {
            level_btb.last_timestamp = counter;
        }
        cout << "The last timestamp: " << counter << endl;
    }

    void find_btb_entry_for_record(uint64_t ip, uint64_t target, uint8_t branch_type, uint64_t cpu) {
//        cout << "Before find entry for record" << endl;
        // Note in multilevel, I don't detect whether it is inserted or not!!!
        for (auto &level_btb : btbs) {
//            cout << "Before find one" << endl;
            auto btb_entry = level_btb.find_btb_entry(ip, cpu);
//            cout << "After found one" << endl;
            if (btb_entry != nullptr) {
                level_btb.access_record.access(btb_entry->ip_tag, btb_entry->target, btb_entry->branch_type, cpu, true, false);
//                cout << "After find entry for record" << endl;
                return;
            }
//            cout << "null" << endl;
            level_btb.access_record.access(ip, target, branch_type, cpu, false, false);
        }
//        cout << "After find entry for record" << endl;
    }

    void update_timestamp() {
        timestamp++;
        for (auto &level_btb : btbs) {
            level_btb.timestamp = timestamp;
        }
    }

    void print_access_record(string &trace_name, uint64_t cpu) {
        for (auto &level_btb : btbs) {
            level_btb.access_record.print_final_stats(trace_name, cpu);
        }
    }
};

MultiLevelOPTBTB multi_level_btb(btb_level_info);

uint64_t basic_btb_indirect[NUM_CPUS][BASIC_BTB_INDIRECT_SIZE];
uint64_t basic_btb_conditional_history[NUM_CPUS];

uint64_t basic_btb_ras[NUM_CPUS][BASIC_BTB_RAS_SIZE];
int basic_btb_ras_index[NUM_CPUS];
/*
 * The following two variables are used to automatically identify the
 * size of call instructions, in bytes, which tells us the appropriate
 * target for a call's corresponding return.
 * They exist because ChampSim does not model a specific ISA, and
 * different ISAs could use different sizes for call instructions,
 * and even within the same ISA, calls can have different sizes.
 */
uint64_t basic_btb_call_instr_sizes[NUM_CPUS][BASIC_BTB_CALL_INSTR_SIZE_TRACKERS];

uint64_t basic_btb_abs_addr_dist(uint64_t addr1, uint64_t addr2) {
    if(addr1 > addr2) {
        return addr1 - addr2;
    }

    return addr2 - addr1;
}

uint64_t basic_btb_indirect_hash(uint8_t cpu, uint64_t ip) {
    uint64_t hash = (ip >> 2) ^ (basic_btb_conditional_history[cpu]);
    return (hash & (BASIC_BTB_INDIRECT_SIZE-1));
}

void push_basic_btb_ras(uint8_t cpu, uint64_t ip) {
    basic_btb_ras_index[cpu]++;
    if (basic_btb_ras_index[cpu] == BASIC_BTB_RAS_SIZE) {
        basic_btb_ras_index[cpu] = 0;
    }

    basic_btb_ras[cpu][basic_btb_ras_index[cpu]] = ip;
}

uint64_t peek_basic_btb_ras(uint8_t cpu) {
    return basic_btb_ras[cpu][basic_btb_ras_index[cpu]];
}

uint64_t pop_basic_btb_ras(uint8_t cpu) {
    uint64_t target = basic_btb_ras[cpu][basic_btb_ras_index[cpu]];
    basic_btb_ras[cpu][basic_btb_ras_index[cpu]] = 0;

    basic_btb_ras_index[cpu]--;
    if (basic_btb_ras_index[cpu] == -1) {
        basic_btb_ras_index[cpu] += BASIC_BTB_RAS_SIZE;
    }

    return target;
}

uint64_t basic_btb_call_size_tracker_hash(uint64_t ip) {
    return (ip & (BASIC_BTB_CALL_INSTR_SIZE_TRACKERS-1));
}

uint64_t basic_btb_get_call_size(uint8_t cpu, uint64_t ip) {
    uint64_t size = basic_btb_call_instr_sizes[cpu][basic_btb_call_size_tracker_hash(ip)];

    return size;
}

void O3_CPU::initialize_btb() {
    std::cout << "Muli-level Basic BTB" << std::endl
              << " indirect buffer size: " << BASIC_BTB_INDIRECT_SIZE
              << " RAS size: " << BASIC_BTB_RAS_SIZE << std::endl;

    open_btb_record("r", false);
    multi_level_btb.read_record(btb_record, cpu);

//    coverage_accuracy.init(btb_record, BASIC_BTB_SETS, BASIC_BTB_WAYS);


//    branch_bias.init(total_btb_ways, total_btb_entries);

    for (uint32_t i = 0; i < BASIC_BTB_INDIRECT_SIZE; i++) {
        basic_btb_indirect[cpu][i] = 0;
    }
    basic_btb_conditional_history[cpu] = 0;

    for (uint32_t i = 0; i < BASIC_BTB_RAS_SIZE; i++) {
        basic_btb_ras[cpu][i] = 0;
    }
    basic_btb_ras_index[cpu] = 0;
    for (uint32_t i=0; i<BASIC_BTB_CALL_INSTR_SIZE_TRACKERS; i++) {
        basic_btb_call_instr_sizes[cpu][i] = 4;
    }
}

std::pair<uint64_t, uint8_t> O3_CPU::btb_prediction(uint64_t ip, uint8_t branch_type, uint64_t *latency) {
    uint8_t always_taken = false;
    if (branch_type != BRANCH_CONDITIONAL) {
        always_taken = true;
    }

    if ((branch_type == BRANCH_DIRECT_CALL) ||
        (branch_type == BRANCH_INDIRECT_CALL)) {
        // add something to the RAS
        push_basic_btb_ras(cpu, ip);
    }

    if (branch_type == BRANCH_RETURN) {
        // peek at the top of the RAS
        uint64_t target = peek_basic_btb_ras(cpu);
        // and adjust for the size of the call instr
        target += basic_btb_get_call_size(cpu, target);

        return std::make_pair(target, always_taken);
    } else if ((branch_type == BRANCH_INDIRECT) ||
               (branch_type == BRANCH_INDIRECT_CALL)) {
        return std::make_pair(basic_btb_indirect[cpu][basic_btb_indirect_hash(cpu, ip)], always_taken);
    } else {
        // use BTB for all other branches + direct calls
        multi_level_btb.update_timestamp();
        auto find_result = multi_level_btb.find_btb_entry(ip, cpu);
        if (latency != nullptr) {
            *latency = find_result.second;
        }

        if (find_result.first == nullptr) {
            // no prediction for this IP
            return std::make_pair(stream_buffer.stream_buffer_predict(ip), true);
        }

        return std::make_pair(find_result.first->target, find_result.first->always_taken);
    }

    return std::make_pair(0, always_taken);
}

void O3_CPU::update_btb(uint64_t ip, uint64_t branch_target, uint8_t taken,
                        uint8_t branch_type) {
    // updates for indirect branches
    if ((branch_type == BRANCH_INDIRECT) ||
        (branch_type == BRANCH_INDIRECT_CALL)) {
        basic_btb_indirect[cpu][basic_btb_indirect_hash(cpu, ip)] = branch_target;
    }
    if (branch_type == BRANCH_CONDITIONAL) {
        basic_btb_conditional_history[cpu] <<= 1;
        if (taken) {
            basic_btb_conditional_history[cpu] |= 1;
        }
    }

    if (branch_type == BRANCH_RETURN) {
        // recalibrate call-return offset
        // if our return prediction got us into the right ball park, but not the
        // exactly correct byte target, then adjust our call instr size tracker
        uint64_t call_ip = pop_basic_btb_ras(cpu);
        uint64_t estimated_call_instr_size = basic_btb_abs_addr_dist(call_ip, branch_target);
        if (estimated_call_instr_size <= 10) {
            basic_btb_call_instr_sizes[cpu][basic_btb_call_size_tracker_hash(call_ip)] = estimated_call_instr_size;
        }
    } else if ((branch_type != BRANCH_INDIRECT) &&
               (branch_type != BRANCH_INDIRECT_CALL)) {
        // use BTB
//        branch_bias.access(ip, cpu, taken != 0);
        auto find_result = multi_level_btb.find_btb_entry(ip, cpu);

        if (find_result.first == nullptr) {
            stream_buffer.stream_buffer_update(ip);
            if ((branch_target != 0) && taken) {
                // Update access record first
                multi_level_btb.find_btb_entry_for_record(ip, branch_target, branch_type, cpu);
                // no prediction for this entry so far, so allocate one
                multi_level_btb.insert(ip, branch_target, branch_type, cpu, this);
            }
        } else {
            // update an existing entry
            if (!taken) {
                find_result.first->always_taken = 0;
            } else {
                // Update access record first
                multi_level_btb.find_btb_entry_for_record(ip, branch_target, branch_type, cpu);
                // Only update target on taken!!!
                find_result.first->target = branch_target;
                multi_level_btb.update(ip, cpu, this);
            }
        }
    }
}

void O3_CPU::prefetch_btb(uint64_t ip, uint64_t branch_target, uint8_t branch_type, bool taken, bool to_stream_buffer) {
    // TODO: Here we only prefetch for direct btb, since the hash for indirect branch may change later
    if (branch_type != BRANCH_RETURN &&
        branch_type != BRANCH_INDIRECT &&
        branch_type != BRANCH_INDIRECT_CALL) {
        auto find_result = multi_level_btb.find_btb_entry(ip, cpu);

        if (find_result.first == nullptr) {
            if ((branch_target != 0) && taken) {
                // no prediction for this entry so far, so allocate one
                if (to_stream_buffer) {
                    stream_buffer.prefetch(ip, branch_target);
                } else {
                    multi_level_btb.insert(ip, branch_target, branch_type, cpu, this);
                }
            }
        } else {
            // update an existing entry
            if (!taken) {
                find_result.first->always_taken = 0;
            } else {
                // Only update target on taken!!!
                find_result.first->target = branch_target;
                multi_level_btb.update(ip, cpu, this);
            }
        }
    }
}


void O3_CPU::btb_final_stats() {
//    cout << "BTB taken num: " << predicted_taken_branch_count
//         << " BTB miss num: " << btb_miss_taken_branch_count
//         << " BTB miss rate: " << ((double) btb_miss_taken_branch_count) / ((double) predicted_taken_branch_count)
//         << endl;
//    branch_bias.print_final_stats(trace_name, cpu);
//    print_final_stats(trace_name, program_name, BASIC_BTB_WAYS);
    multi_level_btb.print_access_record(trace_name, cpu);
}

