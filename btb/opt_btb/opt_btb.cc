
/*
 * This file implements a basic Branch Target Buffer (BTB) structure.
 * It uses a set-associative BTB to predict the targets of non-return branches,
 * and it uses a small Return Address Stack (RAS) to predict the target of
 * returns.
 */

#include "ooo_cpu.h"
#include <unordered_map>
#include <set>
#include <vector>
#include "../access_counter.h"
#include "../access_record.h"
#include "../accuracy.h"
#include "../prefetch_stream_buffer.h"

extern uint8_t total_btb_ways;
extern uint64_t total_btb_entries; // 1K, 2K...

#define BASIC_BTB_SETS (total_btb_entries / total_btb_ways)
#define BASIC_BTB_WAYS total_btb_ways
#define BASIC_BTB_INDIRECT_SIZE 4096
#define BASIC_BTB_RAS_SIZE 32
#define BASIC_BTB_CALL_INSTR_SIZE_TRACKERS 1024

CoverageAccuracy coverage_accuracy;
StreamBuffer stream_buffer(32);

struct BASIC_BTB_ENTRY {
    uint64_t ip_tag;
    uint64_t target;
    uint8_t always_taken;
    uint64_t lru;
};

using std::unordered_map;
using std::set;
using std::vector;

bool generate_record = false;

//AccessCounter access_counter(BASIC_BTB_SETS, BASIC_BTB_WAYS);
AccessRecord access_record;

template<class T>
class Opt {
private:
    vector<vector<unordered_map<uint64_t, set<uint64_t>>>> future_accesses;
    vector<vector<unordered_map<uint64_t, set<uint64_t>>>> future_prefetches;
    vector<vector<unordered_map<uint64_t, T>>> current_btb;
    uint64_t total_sets;
    uint64_t total_ways;

    uint64_t last_timestamp = 0;
public:
    uint64_t timestamp = 0;

    Opt(uint64_t total_sets, uint64_t total_ways) : total_sets(total_sets), total_ways(total_ways) {}

    void init(uint64_t sets, uint64_t ways) {
        total_sets = sets;
        total_ways = ways;
        future_accesses.resize(NUM_CPUS, vector<unordered_map<uint64_t, set<uint64_t>>>(total_sets));
        future_prefetches.resize(NUM_CPUS, vector<unordered_map<uint64_t, set<uint64_t>>>(total_sets));
        current_btb.resize(NUM_CPUS, vector<unordered_map<uint64_t, T>>(total_sets));
    }

    void add_to_future(uint64_t cpu, uint64_t ip, uint64_t counter, vector<vector<unordered_map<uint64_t, set<uint64_t>>>> *future) {
        auto set_index = find_set_index(ip);
        auto it = (*future)[cpu][set_index].find(ip);
        if (it == (*future)[cpu][set_index].end()) {
            (*future)[cpu][set_index].emplace_hint(it, ip, set<uint64_t>());
        }
        (*future)[cpu][set_index][ip].insert(counter);
    }

    void read_record(FILE *demand_record, uint64_t cpu, unordered_map<uint64_t, vector<pair<uint64_t, uint64_t>>> *twig_prefetch_match) {
        uint64_t ip, counter;
        int type = 0;
        while (true) {
            if (generate_record) {
                if (fscanf(demand_record, "%llu %llu", &ip, &counter) == EOF) break;
                type = 0;
            } else {
//                if (fscanf(demand_record, "%llu %llu %d", &ip, &counter, &type) == EOF) break;
                if (fscanf(demand_record, "%llu %llu", &ip, &counter) == EOF) break;
                type = 0;
            }
            vector<vector<unordered_map<uint64_t, set<uint64_t>>>> *future;
            if (type == 0) {
                future = &future_accesses;
            } else {
                future = &future_prefetches;
            }
            // NOTE: We only consider prefetch record for Twig!
            add_to_future(cpu, ip, counter,&future_accesses);
            // Add twig prefetch record if needed
            if (twig_prefetch_match != nullptr) {
                auto it = twig_prefetch_match->find(ip);
                if (it != twig_prefetch_match->end()) {
                    for (auto &a : it->second) {
                        add_to_future(cpu, a.first, counter, &future_prefetches);
                    }
                }
            }
        }
        last_timestamp = counter;
        cout << "The last timestamp: " << last_timestamp << endl;
        rewind(demand_record);
    }

    uint64_t find_set_index(uint64_t ip) {
        return ((ip >> 2) % total_sets);
    }

    T *find_btb_entry(uint64_t ip, uint64_t cpu) {
        auto set = find_set_index(ip);
        auto it = current_btb[cpu][set].find(ip);
        return it == current_btb[cpu][set].end() ? nullptr : &(it->second);
    }

    bool insert_to_btb(uint64_t ip, uint64_t cpu, uint64_t branch_target, uint8_t branch_type) {
        auto set = find_set_index(ip);
        auto time = timestamp - 1; // Get the current timestamp (we've +1 before the prediction)
        if (current_btb[cpu][set].size() == total_ways) {
//            if (future_accesses[cpu][set][ip].find(time) == future_accesses[cpu][set][ip].end()) {
//                assert(time > last_timestamp);
//            }
//            assert(future_accesses[cpu][set][ip].find(time) != future_accesses[cpu][set][ip].end());
            // Evict a btb entry if it is full
            auto current_it = future_accesses[cpu][set][ip].upper_bound(time);
            if (current_it == future_accesses[cpu][set][ip].end()) {
                // There is no point caching the key
                // cout << "Error: cannot find the corresponding timestamp!" << endl;
                coverage_accuracy.get_reuse_distance(ip, time, true);
                return false;
            }
            // First, find the one among the current set will be prefetched furthest
            bool prefetch_found = false;
            pair<uint64_t, uint64_t> prefetch_candidate;
            for (auto &entry : current_btb[cpu][set]) {
                auto it = future_prefetches[cpu][set][entry.first].upper_bound(time);
                if (it != future_prefetches[cpu][set][entry.first].end()) {
                    // Find the prefetch
                    // Check whether the prefetch is before the next load of this line
                    auto demand_it = future_accesses[cpu][set][entry.first].upper_bound(time);
                    if (demand_it != future_accesses[cpu][set][entry.first].end() && (*it) > (*demand_it)) {
                        // Demand load is presend and prefetch comes after the first load, so ignore
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
                current_btb[cpu][set].erase(prefetch_candidate.second);
                access_record.evict(prefetch_candidate.second, cpu);
                coverage_accuracy.get_reuse_distance(prefetch_candidate.second, time, false);
            } else {
                // Find victim in future demand access
                pair<uint64_t, uint64_t> candidate;
                candidate.first = *current_it;
                candidate.second = ip;
                for (const auto &entry : current_btb[cpu][set]) {
                    auto it = future_accesses[cpu][set][entry.first].upper_bound(time);
                    if (it == future_accesses[cpu][set][entry.first].end()) {
                        candidate.first = last_timestamp + 1; // It should be total num of branch accesses!!!
                        candidate.second = entry.first;
                    } else if (candidate.first < *it) {
                        candidate.first = *it;
                        candidate.second = entry.first;
                    }
                }
                if (candidate.second == ip) {
                    coverage_accuracy.get_reuse_distance(ip, time, true);
                    return false;
                } else {
                    current_btb[cpu][set].erase(candidate.second);
                    access_record.evict(candidate.second, cpu);
                    coverage_accuracy.get_reuse_distance(candidate.second, time, false);
                }
            }
        }
        current_btb[cpu][set].emplace(ip, BASIC_BTB_ENTRY());
//        access_counter.insert(ip, cpu, branch_target, branch_type);
        return true;
    }

};

Opt<BASIC_BTB_ENTRY> basic_opt(BASIC_BTB_SETS, BASIC_BTB_WAYS);

//unordered_map<uint64_t, set<uint64_t>> future_accesses[NUM_CPUS][BASIC_BTB_SETS];
//unordered_map<uint64_t, BASIC_BTB_ENTRY> current_btb[NUM_CPUS][BASIC_BTB_SETS];
//uint64_t timestamp = 0;

//BASIC_BTB_ENTRY basic_btb[NUM_CPUS][BASIC_BTB_SETS][BASIC_BTB_WAYS];
//uint64_t basic_btb_lru_counter[NUM_CPUS];

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

uint64_t basic_btb_set_index(uint64_t ip) {
    return ((ip >> 2) % BASIC_BTB_SETS);
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
    std::cout << "OPT BTB sets: " << BASIC_BTB_SETS
              << " ways: " << (int) BASIC_BTB_WAYS
              << " indirect buffer size: " << BASIC_BTB_INDIRECT_SIZE
              << " RAS size: " << BASIC_BTB_RAS_SIZE << std::endl;

    open_btb_record("r", false);

    coverage_accuracy.init(btb_record, BASIC_BTB_SETS, BASIC_BTB_WAYS);

    basic_opt.init(BASIC_BTB_SETS, BASIC_BTB_WAYS);

    // Initialize future_accesses
    if (generate_record)
        basic_opt.read_record(btb_record, cpu, twig_prefetch_match);
    else
        basic_opt.read_record(btb_record, cpu, twig_prefetch_match);

//    for (uint32_t i = 0; i < BASIC_BTB_SETS; i++) {
//        for (uint32_t j = 0; j < BASIC_BTB_WAYS; j++) {
//            basic_btb[cpu][i][j].ip_tag = 0;
//            basic_btb[cpu][i][j].target = 0;
//            basic_btb[cpu][i][j].always_taken = 0;
//            basic_btb[cpu][i][j].lru = 0;
//        }
//    }
//    basic_btb_lru_counter[cpu] = 0;

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
        basic_opt.timestamp++;
        auto btb_entry = basic_opt.find_btb_entry(ip, cpu);
        if (btb_entry == nullptr) {
            // no prediction for this IP
            return std::make_pair(stream_buffer.stream_buffer_predict(ip), true);
        }
        return std::make_pair(btb_entry->target, btb_entry->always_taken);
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
        auto btb_entry = basic_opt.find_btb_entry(ip, cpu);
        if (btb_entry == nullptr) {
            stream_buffer.stream_buffer_update(ip);
            // no prediction for this entry so far, so allocate one
            if (branch_target != 0 && taken) {
                auto judge = basic_opt.insert_to_btb(ip, cpu, branch_target, branch_type);
                access_record.access(ip, branch_target, branch_type, cpu, false, judge);
                if (judge) {
                    auto repl_entry = basic_opt.find_btb_entry(ip, cpu);
                    repl_entry->ip_tag = ip;
                    repl_entry->target = branch_target;
                    repl_entry->always_taken = 1;
                }
            }
        } else {
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
                access_record.access(ip, branch_target, branch_type, cpu, true, false);
            }
//            access_counter.access(ip, cpu);
        }
    }
}

void O3_CPU::prefetch_btb(uint64_t ip, uint64_t branch_target, uint8_t branch_type, bool taken, bool to_stream_buffer) {
    // TODO: Here we only prefetch for direct btb, since the hash for indirect branch may change later
    access_record.btb_type = BTBType::PREDECODER;
    if (branch_type != BRANCH_RETURN &&
        branch_type != BRANCH_INDIRECT &&
        branch_type != BRANCH_INDIRECT_CALL) {
//        basic_opt.timestamp++;
        auto btb_entry = basic_opt.find_btb_entry(ip, cpu);

        if (btb_entry == nullptr) {
            // no prediction for this entry so far, so allocate one
            if (branch_target != 0 && taken) {
                if (to_stream_buffer) {
                    stream_buffer.prefetch(ip, branch_target);
                } else {
                    auto judge = basic_opt.insert_to_btb(ip, cpu, branch_target, branch_type);
                    access_record.access(ip, branch_target, branch_type, cpu, false, judge);
                    if (judge) {
                        auto repl_entry = basic_opt.find_btb_entry(ip, cpu);
                        repl_entry->ip_tag = ip;
                        repl_entry->target = branch_target;
                        repl_entry->always_taken = 1;
                    }
                }
            }
        } else {
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
                access_record.access(ip, branch_target, branch_type, cpu, true, false);
            }
//            access_counter.access(ip, cpu);
        }
    }
}

void O3_CPU::btb_final_stats() {
    cout << "BTB taken num: " << predicted_taken_branch_count
         << " BTB miss num: " << btb_miss_taken_branch_count
         << " BTB miss rate: " << ((double) btb_miss_taken_branch_count) / ((double) predicted_taken_branch_count)
         << endl;
//    access_counter.print_final_stats(cpu);
//    assert(access_record.btb_type == BTBType::NORMAL);
    access_record.print_final_stats(trace_name, cpu, twig_prefetch_match != nullptr);
    coverage_accuracy.print_final_stats(trace_name, program_name, BASIC_BTB_WAYS);
}
