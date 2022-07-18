
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
#include <deque>
#include <limits>
#include <tuple>
#include <cstdlib>
#include "../reuse_distance.h"
#include "../access_counter.h"
#include "../prefetch_stream_buffer.h"

using std::vector;
using std::unordered_map;
using std::priority_queue;
using std::deque;
using std::tuple;

#define BASIC_BTB_SETS 2048
#define BASIC_BTB_WAYS 4
#define BASIC_BTB_INDIRECT_SIZE 4096
#define BASIC_BTB_RAS_SIZE 32
#define BASIC_BTB_CALL_INSTR_SIZE_TRACKERS 1024

#define SRRIP_COUNTER_SIZE 2
#define SRRIP_DISTANT_INTERVAL (1 << SRRIP_COUNTER_SIZE) - 1
#define SRRIP_LONG_INTERVAL (1 << SRRIP_COUNTER_SIZE) - 2

#define REUSE_DISTANCE_LENGTH 20
#define REUSE_DISTANCE_UPPER_BOUND 8

enum class NotFoundReturnMethod {
    RANDOM,
    AVERAGE,
    RECENT
};

#define CHECK_PREDICT_ACCURACY @CHECK_PREDICT_ACCURACY@
#define PREDICT_CHECK_NUM @PREDICT_CHECK_NUM@
#define NOT_FOUND_RETURN_METHOD @NOT_FOUND_RETURN_METHOD@

NotFoundReturnMethod not_found_return_method = NOT_FOUND_RETURN_METHOD;

uint64_t timestamp = 0;

//ReuseDistance reuse_distance(BASIC_BTB_SETS);
AccessCounter access_counter(BASIC_BTB_SETS, BASIC_BTB_WAYS);
StreamBuffer stream_buffer(32);

struct BASIC_BTB_ENTRY {
    uint64_t ip_tag = 0;
    uint64_t target = 0;
    uint8_t branch_type = BRANCH_CONDITIONAL;
    uint8_t always_taken = 1;
    uint64_t rrpv = SRRIP_LONG_INTERVAL;
    // Used to count reuse_distance, can be replaced by a 4-bit bitmap and a 3-bit counter
    set<uint64_t> count_reuse_distance;
    // The newest is at the front of the deque for simplicity
    deque<uint8_t> reuse_distance; // We only store distance from 0 to 7
    uint8_t predict_record = REUSE_DISTANCE_LENGTH;

    BASIC_BTB_ENTRY() = default;

    BASIC_BTB_ENTRY(uint64_t ip, uint64_t target, uint8_t branch_type, bool always_taken) :
            ip_tag(ip),
            target(target),
            branch_type(branch_type),
            always_taken(always_taken ? 1 : 0) {
        reuse_distance.push_front(REUSE_DISTANCE_UPPER_BOUND - 1);
    }

    [[nodiscard]] bool srrip_can_evict() const {
        return rrpv == SRRIP_DISTANT_INTERVAL;
    }

    void srrip_insert_setting() {
        rrpv = SRRIP_LONG_INTERVAL;
    }

    void srrip_update() {
        rrpv = 0;
    }

    void srrip_increase() {
        rrpv++;
    }

    void access_self() {
        if (reuse_distance.size() >= REUSE_DISTANCE_LENGTH) {
            reuse_distance.pop_back();
            if (count_reuse_distance.size() >= REUSE_DISTANCE_UPPER_BOUND) {
                reuse_distance.push_front(REUSE_DISTANCE_UPPER_BOUND - 1);
            } else {
                reuse_distance.push_front(count_reuse_distance.size());
            }
            count_reuse_distance.clear();
        }
    }

    void increase(uint64_t access_ip) {
        if (access_ip == ip_tag)
            access_self();
        else
            count_reuse_distance.insert(access_ip);
    }

    uint8_t predict_reuse_distance() {
        // TODO: Verify this algorithm
        auto n = reuse_distance.size();
        vector<size_t> pi(n, 0);

        //kmp algo.
        for (size_t i = 1; i < n; i++) {
            size_t j = pi[i - 1];
            while (j > 0 && reuse_distance[i] != reuse_distance[j])
                j = pi[j - 1];
            if (reuse_distance[i] == reuse_distance[j])
                j++;
            pi[i] = j;
        }

        for (size_t i = 1; i < n; i++) {
            if (PREDICT_CHECK_NUM == 2) {
                if (i == 1 && pi[i])
                    return reuse_distance[i - 1];
                if (i > 1 && i < n - 1 && pi[i] && pi[i + 1])
                    return reuse_distance[i - 1];
            } else {
                if (pi[i]) {
                    return reuse_distance[i - 1];
                }
            }
        }
        if (not_found_return_method == NotFoundReturnMethod::RANDOM) {
            return std::rand() % REUSE_DISTANCE_UPPER_BOUND;
        } else if (not_found_return_method == NotFoundReturnMethod::AVERAGE) {
            uint64_t sum = 0;
            for (auto a : reuse_distance) sum += a;
            return sum / reuse_distance.size();
        } else {
            return reuse_distance.front();
        }
    }
};

template<class T>
class ReusePredict {
    uint64_t total_sets;
    uint64_t total_ways;
    vector<vector<unordered_map<uint64_t, T>>> btb;

public:
    ReusePredict(uint64_t sets, uint64_t ways) : total_sets(sets), total_ways(ways) {
        btb.resize(NUM_CPUS, vector<unordered_map<uint64_t, T>>(sets));
    }

    uint64_t get_set_index(uint64_t ip) {
        return ((ip >> 2) & (total_sets - 1));
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

    uint64_t reuse_predict_evict(uint64_t set, uint64_t cpu) {
        priority_queue<tuple<uint8_t, uint8_t, uint64_t>> chose_victim; // First is predicted reuse distance and second is ip
        for (auto &it : btb[cpu][set]) {
            auto predict_dist = it.second.predict_reuse_distance();
            auto curr_dist = it.second.count_reuse_distance.size();
            if (predict_dist < curr_dist) {
                chose_victim.emplace(curr_dist, predict_dist, it.first);
            } else if (predict_dist == curr_dist) {
                chose_victim.emplace(0, predict_dist, it.first);
            } else {
                chose_victim.emplace(predict_dist - curr_dist, predict_dist, it.first);
            }
//                chose_victim.emplace(it.second.predict_reuse_distance(), it.first);
        }
        return std::get<2>(chose_victim.top());
    }

    uint64_t srrip_evict(uint64_t set, uint64_t cpu) {
        while (true) {
            for (auto &it : btb[cpu][set]) {
                if (it.second.srrip_can_evict()) {
                    return it.first;
                }
            }
            for (auto &it : btb[cpu][set]) {
                it.second.srrip_increase();
            }
        }
    }

    void insert_to_btb(uint64_t ip, uint64_t branch_target, uint8_t branch_type, uint64_t cpu) {
        auto set = get_set_index(ip);
        if (btb[cpu][set].size() >= total_ways) {
            auto victim = reuse_predict_evict(set, cpu);
            auto srrip_victim = srrip_evict(set, cpu);
            // TODO: Combine with SRRIP later
            access_counter.evict(victim, cpu);
            btb[cpu][set].erase(victim);
        }
        assert(btb[cpu][set].size() < total_ways);
        for (auto &it : btb[cpu][set]) {
            it.second.increase(ip);
        }
        btb[cpu][set].emplace(ip, T(ip, branch_target, branch_type, 1));
        btb[cpu][set][ip].srrip_insert_setting();
        access_counter.insert(ip, cpu, branch_target, branch_type);
    }

    void access(uint64_t ip, uint64_t cpu) {
        auto set = get_set_index(ip);
        auto it = btb[cpu][set].find(ip);
        assert(it != btb[cpu][set].end());
        for (auto &i : btb[cpu][set]) {
            i.second.increase(ip);
        }
        it->second.srrip_update();
    }
};


ReusePredict<BASIC_BTB_ENTRY> reuse_predict_btb(BASIC_BTB_SETS, BASIC_BTB_WAYS);

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
    std::cout << "Reuse Predict BTB sets: " << BASIC_BTB_SETS
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
        auto btb_entry = reuse_predict_btb.find_btb_entry(ip, cpu);

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
        auto btb_entry = reuse_predict_btb.find_btb_entry(ip, cpu);

        if (btb_entry == nullptr) {
            stream_buffer.stream_buffer_update(ip);
            if ((branch_target != 0) && taken) {
                // no prediction for this entry so far, so allocate one
                reuse_predict_btb.insert_to_btb(ip, branch_target, branch_type, cpu);
//                reuse_distance.access(ip, branch_target, branch_type, cpu);
            }
        } else {
            // update an existing entry
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
                reuse_predict_btb.access(ip, cpu);
            }
            btb_entry->branch_type = branch_type;
//            reuse_distance.access(ip, branch_target, branch_type, cpu);
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
        auto btb_entry = reuse_predict_btb.find_btb_entry(ip, cpu);

        if (btb_entry == nullptr) {
            if ((branch_target != 0) && taken) {
                // no prediction for this entry so far, so allocate one
                if (to_stream_buffer) {
                    stream_buffer.prefetch(ip, branch_target);
                } else {
                    reuse_predict_btb.insert_to_btb(ip, branch_target, branch_type, cpu);
                }
            }
        } else {
            // update an existing entry
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
                reuse_predict_btb.access(ip, cpu);
            }
            btb_entry->branch_type = branch_type;
//            access_counter.access(ip, cpu);
        }
    }
}

void O3_CPU::btb_final_stats() {
//    reuse_distance.print_final_stats(trace_name, cpu);
    access_counter.print_final_stats(cpu);
}
