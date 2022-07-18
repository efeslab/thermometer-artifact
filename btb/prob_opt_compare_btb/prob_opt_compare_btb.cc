
/*
 * This file implements a basic Branch Target Buffer (BTB) structure.
 * It uses a set-associative BTB to predict the targets of non-return branches,
 * and it uses a small Return Address Stack (RAS) to predict the target of
 * returns.
 */

#include "ooo_cpu.h"
#include <vector>
#include <unordered_map>
#include <map>
#include <queue>
#include <limits>
#include "../access_counter.h"
#include "../reuse_distance.h"
#include "../prefetch_stream_buffer.h"

using std::vector;
using std::unordered_map;
using std::priority_queue;
using std::set;
using std::map;
using std::string;

#define BASIC_BTB_SETS 2048
#define BASIC_BTB_WAYS 4
#define BASIC_BTB_INDIRECT_SIZE 4096
#define BASIC_BTB_RAS_SIZE 32
#define BASIC_BTB_CALL_INSTR_SIZE_TRACKERS 1024

#define SRRIP_COUNTER_SIZE 2
#define SRRIP_DISTANT_INTERVAL (1 << SRRIP_COUNTER_SIZE) - 1
#define SRRIP_LONG_INTERVAL (1 << SRRIP_COUNTER_SIZE) - 2

string branch_type_name[8] = {
        "BRANCH_NOT_BRANCH",
        "BRANCH_DIRECT_JUMP",
        "BRANCH_INDIRECT",
        "BRANCH_CONDITIONAL",
        "BRANCH_DIRECT_CALL",
        "BRANCH_INDIRECT_CALL",
        "BRANCH_RETURN",
        "BRANCH_OTHER"
};

// The order represents the priority
enum ReplaceType {
    PROB,
    PROB_SRRIP,
    SRRIP_PROB
};

ReplaceType replace_type = PROB;
bool calculate_probability = false;
bool clear_when_evict = true;
uint64_t counter_upper_bound = 7;
bool update_when_evict = true;

AccessCounter access_counter(BASIC_BTB_SETS, BASIC_BTB_WAYS);
StreamBuffer stream_buffer(32);

// From opt
bool generate_record = false;

struct BASIC_BTB_ENTRY {
    uint64_t ip_tag = 0;
    uint64_t target = 0;
    uint8_t branch_type = BRANCH_CONDITIONAL;
    uint8_t always_taken = 1;
    uint64_t rrpv = SRRIP_LONG_INTERVAL;

    BASIC_BTB_ENTRY() = default;

    BASIC_BTB_ENTRY(uint64_t ip, uint64_t target, uint8_t branch_type, bool always_taken) :
            ip_tag(ip),
            target(target),
            branch_type(branch_type),
            always_taken(always_taken ? 1 : 0) {}
};

template<class T>
class Opt {
private:
    vector<vector<unordered_map<uint64_t, set<uint64_t>>>> future_accesses;
    vector<vector<unordered_map<uint64_t, set<uint64_t>>>> future_prefetches;
    vector<vector<unordered_map<uint64_t, T>>> current_btb;
    uint64_t total_sets;
    uint64_t total_ways;
public:
    uint64_t timestamp = 0;

    Opt(uint64_t total_sets, uint64_t total_ways) : total_sets(total_sets), total_ways(total_ways) {
        future_accesses.resize(NUM_CPUS, vector<unordered_map<uint64_t, set<uint64_t>>>(total_sets));
        future_prefetches.resize(NUM_CPUS, vector<unordered_map<uint64_t, set<uint64_t>>>(total_sets));
        current_btb.resize(NUM_CPUS, vector<unordered_map<uint64_t, T>>(total_sets));
    }

    void read_record(FILE *demand_record, uint64_t cpu) {
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
            auto set_index = find_set_index(ip);
            auto it = (*future)[cpu][set_index].find(ip);
            if (it == (*future)[cpu][set_index].end()) {
                (*future)[cpu][set_index].emplace_hint(it, ip, set<uint64_t>());
            }
            (*future)[cpu][set_index][ip].insert(counter);
        }
    }

    uint64_t find_set_index(uint64_t ip) {
        return ((ip >> 2) & (total_sets - 1));
    }

    T *find_btb_entry(uint64_t ip, uint64_t cpu) {
        auto set = find_set_index(ip);
        auto it = current_btb[cpu][set].find(ip);
        return it == current_btb[cpu][set].end() ? nullptr : &(it->second);
    }

    uint64_t make_evict_decision(uint64_t insert_ip, uint64_t cpu) {
        // Get optimal victim for current btb, return 0 if cannot evict or don't need to evict
        auto set = find_set_index(insert_ip);
        auto time = timestamp - 1; // Get the current timestamp (we've +1 before the prediction)
        if (current_btb[cpu][set].size() >= total_ways) {
            // Evict a btb entry if it is full
            auto current_it = future_accesses[cpu][set][insert_ip].upper_bound(time);
            if (current_it == future_accesses[cpu][set][insert_ip].end()) {
                // There is no point caching the key
                // cout << "Error: cannot find the corresponding timestamp!" << endl;
                return 0;
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
                return prefetch_candidate.second;
            } else {
                // Find victim in future demand access
                pair<uint64_t, uint64_t> candidate;
                candidate.first = *current_it;
                candidate.second = insert_ip;
                for (const auto &entry : current_btb[cpu][set]) {
                    auto it = future_accesses[cpu][set][entry.first].upper_bound(time);
                    if (it == future_accesses[cpu][set][entry.first].end()) {
                        candidate.first = time + 1;
                        candidate.second = entry.first;
                    } else if (candidate.first < *it) {
                        candidate.first = *it;
                        candidate.second = entry.first;
                    }
                }
                if (candidate.second == insert_ip) {
                    return 0;
                } else {
                    return candidate.second;
                }
            }
        }
        return 0;
    }

    void evict_and_insert(uint64_t victim_ip, uint64_t insert_ip, uint64_t cpu) {
        // victim_ip = 0 if there is no need to evict
        auto set = find_set_index(insert_ip);
        if (victim_ip != 0) {
            assert(set == find_set_index(victim_ip));
            current_btb[cpu][set].erase(victim_ip);
        }
        assert(current_btb[cpu][set].size() < total_ways);
        current_btb[cpu][set].emplace(insert_ip, BASIC_BTB_ENTRY());
    }
};

Opt<BASIC_BTB_ENTRY> basic_opt(BASIC_BTB_SETS, BASIC_BTB_WAYS);

struct Taken {
    uint64_t taken = 0;
    uint64_t not_taken = 0;
    uint8_t branch_type = BRANCH_CONDITIONAL;

    Taken() = default;

    explicit Taken(uint8_t branch_type) : branch_type(branch_type) {}

    [[nodiscard]] double taken_probability() const {
        if (!calculate_probability) return (double) taken;
        if (branch_type != BRANCH_CONDITIONAL || taken + not_taken == 0) return 1;
        return ((double) taken) / ((double) taken + (double) not_taken);
    }

    void update(uint8_t type, bool taken_or_not) {
        if (type != branch_type) {
            branch_type = type;
            taken = 0;
            not_taken = 0;
        }
        if (taken_or_not) {
            if (calculate_probability)
                taken++;
            else {
                if (taken < counter_upper_bound)
                    taken++;
            }
        } else not_taken++;
    }

    void clear() {
        taken = 0;
        not_taken = 0;
    }

    void decrease(double victim_counter) {
        taken -= (uint64_t) victim_counter;
    }
};

template<class T>
class Prob {
    uint64_t total_sets;
    uint64_t total_ways;
    vector<vector<unordered_map<uint64_t, T>>> btb;
    vector<unordered_map<uint64_t, Taken>> branch_record;

    vector<uint64_t> opt_choices;
    vector<uint64_t> not_opt_choices;
    vector<uint64_t> should_not_evict;
    vector<map<int, uint64_t>> difference_counter;
    vector<vector<uint64_t>> rank_counter;

public:
    Prob(uint64_t sets, uint64_t ways) : total_sets(sets), total_ways(ways) {
        btb.resize(NUM_CPUS, vector<unordered_map<uint64_t, T>>(sets));
        branch_record.resize(NUM_CPUS);
        opt_choices.resize(NUM_CPUS, 0);
        not_opt_choices.resize(NUM_CPUS, 0);
        should_not_evict.resize(NUM_CPUS, 0);
        difference_counter.resize(NUM_CPUS);
        rank_counter.resize(NUM_CPUS, vector<uint64_t>(total_ways, 0));
    }

    uint64_t get_set_index(uint64_t ip) {
        return ((ip >> 2) & (total_sets - 1));
    }

    void update_rrpv(uint64_t cpu, uint64_t set) {
        for (auto &it : btb[cpu][set]) {
            if (it.second.rrpv < SRRIP_DISTANT_INTERVAL)
                it.second.rrpv++;
        }
    }

    T *find_btb_entry(uint64_t ip, uint64_t cpu) {
        auto set = get_set_index(ip);
        auto it = btb[cpu][set].find(ip);
        if (it == btb[cpu][set].end()) {
            return nullptr;
        } else {
            it->second.rrpv = 0;
            return &(it->second);
        }
    }

    uint64_t srrip_prob_evict(uint64_t cpu, uint64_t set) {
        priority_queue<pair<double, uint64_t>,
                std::vector<pair<double, uint64_t>>,
                std::greater<>> evict_pq;
        while (true) {
            for (auto &it : btb[cpu][set]) {
                if (it.second.rrpv == SRRIP_DISTANT_INTERVAL) {
                    evict_pq.emplace(taken_probability(it.first, cpu), it.first);
                }
            }
            if (!evict_pq.empty())
                break;
            else
                update_rrpv(cpu, set);
        }
        return evict_pq.top().second;
    }

    uint64_t prob_srrip_evict(uint64_t cpu, uint64_t set) {
        priority_queue<pair<uint64_t, uint64_t>> evict_pq;
        double current_min = std::numeric_limits<double>::max();
        for (auto &it : btb[cpu][set]) {
            auto p = taken_probability(it.first, cpu);
            if (p < current_min) {
                current_min = p;
                while (!evict_pq.empty()) evict_pq.pop();
            }
            if (p == current_min)
                evict_pq.emplace(it.second.rrpv, it.first);
        }
        auto result = evict_pq.top();
        assert(result.first <= SRRIP_DISTANT_INTERVAL);
        while (!evict_pq.empty()) {
            assert(btb[cpu][set].find(evict_pq.top().second) != btb[cpu][set].end());
            assert(btb[cpu][set][evict_pq.top().second].rrpv <= result.first);
            btb[cpu][set][evict_pq.top().second].rrpv += (SRRIP_DISTANT_INTERVAL - result.first);
            evict_pq.pop();
        }
        return result.second;
    }

    uint64_t prob_evict(uint64_t cpu, uint64_t set) {
        double current_min = std::numeric_limits<double>::max();
        uint64_t current_ip = 0;
        for (auto &it : btb[cpu][set]) {
            auto p = taken_probability(it.first, cpu);
            if (p <= current_min) {
                current_min = p;
                current_ip = it.first;
            }
        }
        return current_ip;
    }

    T *insert_to_btb(uint64_t ip, uint64_t branch_target, uint8_t branch_type, uint64_t cpu) {
        auto set = get_set_index(ip);
        uint64_t evict_ip = 0;
        if (btb[cpu][set].size() >= total_ways) {
            // Evict one instr
            switch (replace_type) {
                case PROB:
                    evict_ip = prob_evict(cpu, set);
                    break;
                case PROB_SRRIP:
                    evict_ip = prob_srrip_evict(cpu, set);
                    break;
                case SRRIP_PROB:
                    evict_ip = srrip_prob_evict(cpu, set);
                    break;
            }
            // Find opt choice under current condition and compare with counter choice
            auto p = taken_probability(evict_ip, cpu);
            auto opt_evict_ip = basic_opt.make_evict_decision(ip, cpu);
            if (opt_evict_ip == evict_ip) {
                opt_choices[cpu]++;
            } else if (opt_evict_ip == 0) {
                should_not_evict[cpu]++;
            } else {
                not_opt_choices[cpu]++;
                auto opt_p = taken_probability(opt_evict_ip, cpu);
                // Update difference_counter (counter difference between opt choice and actual choice)
                if (counter_upper_bound < std::numeric_limits<uint64_t>::max()) {
                    auto key = (int) (opt_p - p);
                    if (difference_counter[cpu].find(key) == difference_counter[cpu].end())
                        difference_counter[cpu][key] = 0;
                    difference_counter[cpu][key]++;
                }
                // Update rank_counter
                uint64_t counter = 0;
                for (auto &it : btb[cpu][set]) {
                    if (taken_probability(it.first, cpu) < opt_p)
                        counter++;
                }
                assert(counter < total_ways);
                rank_counter[cpu][counter]++;
            }
//            cout << "Evict\t" << evict_ip << endl;
            auto it = branch_record[cpu].find(evict_ip);
            if (it != branch_record[cpu].end() && clear_when_evict)
                it->second.clear();

            // Update access counter after evict
            access_counter.evict(evict_ip, cpu);

            btb[cpu][set].erase(evict_ip);
            if (update_when_evict && counter_upper_bound < std::numeric_limits<uint64_t>::max() && clear_when_evict) {
                for (auto &a : btb[cpu][set]) {
                    auto at = branch_record[cpu].find(a.first);
                    if (at != branch_record[cpu].end())
                        at->second.decrease(p);
                }
            }
        }
        basic_opt.evict_and_insert(evict_ip, ip, cpu);
        assert(btb[cpu][set].size() < total_ways);
        btb[cpu][set].emplace(ip, T(ip, branch_target, branch_type, 1));

        // Update access counter after insert
        access_counter.insert(ip, cpu, branch_target, branch_type);
        return &(btb[cpu][set][ip]);
    }

    double taken_probability(uint64_t ip, uint64_t cpu) {
        auto it = branch_record[cpu].find(ip);
        if (it == branch_record[cpu].end()) {
            return 0.0;
        }
        return it->second.taken_probability();
    }

    void update_probability(uint64_t ip, uint8_t branch_type, bool taken, uint64_t cpu) {
        auto it = branch_record[cpu].find(ip);
        if (it == branch_record[cpu].end()) {
            branch_record[cpu].emplace_hint(it, ip, Taken(branch_type));
            it = branch_record[cpu].find(ip);
        }
        it->second.update(branch_type, taken);
    }

    void print_final_stats(uint64_t cpu) {
        cout << "Opt choices: " << opt_choices[cpu]
             << " Not opt choices: " << not_opt_choices[cpu]
             << " Should not evict: " << should_not_evict[cpu] << endl;
        if (counter_upper_bound < std::numeric_limits<uint64_t>::max()) {
            cout << "Difference counter: " << endl;
            for (auto &it : difference_counter[cpu]) {
                cout << it.first << " " << it.second << endl;
            }
            cout << "End difference counter: " << endl;
        }
        cout << "Rank counter: " << endl;
        for (uint64_t i = 0; i < rank_counter[cpu].size(); i++) {
            cout << i << " " << rank_counter[cpu][i] << endl;
        }
        cout << "End rank counter" << endl;
        access_counter.print_final_stats(cpu);
    }
};

Prob<BASIC_BTB_ENTRY> prob_btb(BASIC_BTB_SETS, BASIC_BTB_WAYS);

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
    if (addr1 > addr2) {
        return addr1 - addr2;
    }

    return addr2 - addr1;
}

uint64_t basic_btb_indirect_hash(uint8_t cpu, uint64_t ip) {
    uint64_t hash = (ip >> 2) ^(basic_btb_conditional_history[cpu]);
    return (hash & (BASIC_BTB_INDIRECT_SIZE - 1));
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
    return (ip & (BASIC_BTB_CALL_INSTR_SIZE_TRACKERS - 1));
}

uint64_t basic_btb_get_call_size(uint8_t cpu, uint64_t ip) {
    uint64_t size = basic_btb_call_instr_sizes[cpu][basic_btb_call_size_tracker_hash(ip)];

    return size;
}

void O3_CPU::initialize_btb() {
    std::cout << "Prob OPT Compare BTB sets: " << BASIC_BTB_SETS
              << " ways: " << BASIC_BTB_WAYS
              << " indirect buffer size: " << BASIC_BTB_INDIRECT_SIZE
              << " RAS size: " << BASIC_BTB_RAS_SIZE << std::endl;

    cout << "Comparison between count clear with upper bound and opt" << endl;

    open_btb_record("r", false);

    // Initialize future_accesses
    basic_opt.read_record(btb_record, cpu);

    for (uint32_t i = 0; i < BASIC_BTB_INDIRECT_SIZE; i++) {
        basic_btb_indirect[cpu][i] = 0;
    }
    basic_btb_conditional_history[cpu] = 0;

    for (uint32_t i = 0; i < BASIC_BTB_RAS_SIZE; i++) {
        basic_btb_ras[cpu][i] = 0;
    }
    basic_btb_ras_index[cpu] = 0;
    for (uint32_t i = 0; i < BASIC_BTB_CALL_INSTR_SIZE_TRACKERS; i++) {
        basic_btb_call_instr_sizes[cpu][i] = 4;
    }
}

std::pair<uint64_t, uint8_t> O3_CPU::btb_prediction(uint64_t ip, uint8_t branch_type) {
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

        auto btb_entry = prob_btb.find_btb_entry(ip, cpu);

        if (btb_entry == nullptr) {
            // no prediction for this IP
            return std::make_pair(stream_buffer.stream_buffer_predict(ip), true);
        }

        return std::make_pair(btb_entry->target, btb_entry->always_taken);
    }
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
        auto btb_entry = prob_btb.find_btb_entry(ip, cpu);

        if (btb_entry == nullptr) {
            stream_buffer.stream_buffer_update(ip);
            if ((branch_target != 0) && taken) {
                // TODO: Call function in opt to compare with prob decision.
                // no prediction for this entry so far, so allocate one
                prob_btb.insert_to_btb(ip, branch_target, branch_type, cpu);
                prob_btb.update_probability(ip, branch_type, taken != 0, cpu);
            }
        } else {
            // update an existing entry
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
            }
            btb_entry->branch_type = branch_type;
            // Update access counter after access
            access_counter.access(ip, cpu);
            prob_btb.update_probability(ip, branch_type, taken != 0, cpu);
        }
    }
}

void O3_CPU::prefetch_btb(uint64_t ip, uint64_t branch_target, uint8_t branch_type, bool taken) {
    // TODO: Here we only prefetch for direct btb, since the hash for indirect branch may change later
    if (branch_type != BRANCH_RETURN &&
        branch_type != BRANCH_INDIRECT &&
        branch_type != BRANCH_INDIRECT_CALL) {
        // use BTB
        auto btb_entry = prob_btb.find_btb_entry(ip, cpu);

        if (btb_entry == nullptr) {
            if ((branch_target != 0) && taken) {
                // TODO: Call function in opt to compare with prob decision.
                // no prediction for this entry so far, so allocate one
                if (to_stream_buffer) {
                    stream_buffer.prefetch(ip, branch_target);
                } else {
                    prob_btb.insert_to_btb(ip, branch_target, branch_type, cpu);
                    prob_btb.update_probability(ip, branch_type, taken != 0, cpu);
                }
            }
        } else {
            // update an existing entry
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
            }
            btb_entry->branch_type = branch_type;
            // Update access counter after access
            access_counter.access(ip, cpu);
            prob_btb.update_probability(ip, branch_type, taken != 0, cpu);
        }
    }
}

void O3_CPU::btb_final_stats() {
    prob_btb.print_final_stats(cpu);
}

