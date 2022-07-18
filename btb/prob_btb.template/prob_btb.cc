
/*
 * This file implements a basic Branch Target Buffer (BTB) structure.
 * It uses a set-associative BTB to predict the targets of non-return branches,
 * and it uses a small Return Address Stack (RAS) to predict the target of
 * returns.
 */

#include "ooo_cpu.h"
#include <vector>
#include <unordered_map>
#include <queue>
#include <limits>
#include "../reuse_distance.h"
#include "../access_counter.h"
#include "../prefetch_stream_buffer.h"

using std::vector;
using std::unordered_map;
using std::priority_queue;

#define BASIC_BTB_SETS 2048
#define BASIC_BTB_WAYS 4
#define BASIC_BTB_INDIRECT_SIZE 4096
#define BASIC_BTB_RAS_SIZE 32
#define BASIC_BTB_CALL_INSTR_SIZE_TRACKERS 1024

#define SRRIP_COUNTER_SIZE 2
#define SRRIP_DISTANT_INTERVAL (1 << SRRIP_COUNTER_SIZE) - 1
#define SRRIP_LONG_INTERVAL (1 << SRRIP_COUNTER_SIZE) - 2

// The order represents the priority
enum ReplaceType {
    PROB,
    PROB_SRRIP,
    SRRIP_PROB
};

#define REPLACE_TYPE @REPLACE_TYPE@
#define CALCULATE_PROBABILITY @CALCULATE_PROBABILITY@
#define CLEAR_WHEN_EVICT @CLEAR_WHEN_EVICT@
#define COUNTER_UPPER_BOUND @COUNTER_UPPER_BOUND@
#define UPDATE_WHEN_EVICT @UPDATE_WHEN_EVICT@

ReplaceType replace_type = REPLACE_TYPE;
bool calculate_probability = CALCULATE_PROBABILITY;
bool clear_when_evict = CLEAR_WHEN_EVICT;
uint64_t counter_upper_bound = COUNTER_UPPER_BOUND;
bool update_when_evict = UPDATE_WHEN_EVICT;

uint64_t timestamp = 0;

ReuseDistance reuse_distance(BASIC_BTB_SETS, false);
AccessCounter access_counter(BASIC_BTB_SETS, BASIC_BTB_WAYS);
StreamBuffer stream_buffer(32);

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
        }
        else not_taken++;
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

public:
    Prob(uint64_t sets, uint64_t ways) : total_sets(sets), total_ways(ways) {
        btb.resize(NUM_CPUS, vector<unordered_map<uint64_t, T>>(sets));
        branch_record.resize(NUM_CPUS);
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
                std::greater<pair<double, uint64_t>>> evict_pq;
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
        if (btb[cpu][set].size() >= total_ways) {
            // Evict one instr
            uint64_t evict_ip;
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
            auto p = taken_probability(evict_ip, cpu);
            auto it = branch_record[cpu].find(evict_ip);
            if (it != branch_record[cpu].end() && clear_when_evict)
                it->second.clear();
            btb[cpu][set].erase(evict_ip);
            // Update access counter
            access_counter.evict(evict_ip, cpu);

            if (update_when_evict && counter_upper_bound < std::numeric_limits<uint64_t>::max() && clear_when_evict) {
                for (auto &a : btb[cpu][set]) {
                    auto at = branch_record[cpu].find(a.first);
                    if (at != branch_record[cpu].end())
                        at->second.decrease(p);
                }
            }

        }
        assert(btb[cpu][set].size() < total_ways);
        btb[cpu][set].emplace(ip, T(ip, branch_target, branch_type, 1));
        // Update access counter
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
    std::cout << "Prob BTB sets: " << BASIC_BTB_SETS
              << " ways: " << BASIC_BTB_WAYS
              << " indirect buffer size: " << BASIC_BTB_INDIRECT_SIZE
              << " RAS size: " << BASIC_BTB_RAS_SIZE << std::endl;

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
                // no prediction for this entry so far, so allocate one
                prob_btb.insert_to_btb(ip, branch_target, branch_type, cpu);
                prob_btb.update_probability(ip, branch_type, taken != 0, cpu);
                reuse_distance.access(ip, branch_target, branch_type, cpu);
            }
        } else {
            // update an existing entry
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
            }
            btb_entry->branch_type = branch_type;
            prob_btb.update_probability(ip, branch_type, taken != 0, cpu);
            reuse_distance.access(ip, branch_target, branch_type, cpu);
            access_counter.access(ip, cpu);
        }
    }
}

void O3_CPU::prefetch_btb(uint64_t ip, uint64_t branch_target, uint8_t branch_type, bool taken, bool to_stream_buffer) {
    // TODO: Here we only prefetch for direct btb, since the hash for indirect branch may change later
    if (branch_type != BRANCH_RETURN &&
        branch_type != BRANCH_INDIRECT &&
        branch_type != BRANCH_INDIRECT_CALL) {
        // use BTB
        auto btb_entry = prob_btb.find_btb_entry(ip, cpu);

        if (btb_entry == nullptr) {
            if ((branch_target != 0) && taken) {
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
            prob_btb.update_probability(ip, branch_type, taken != 0, cpu);
            access_counter.access(ip, cpu);
        }
    }
}

void O3_CPU::btb_final_stats() {
    reuse_distance.print_final_stats(trace_name, cpu);
    access_counter.print_final_stats(cpu);
}
