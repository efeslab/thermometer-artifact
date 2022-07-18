
/*
 * This file implements a basic Branch Target Buffer (BTB) structure.
 * It uses a set-associative BTB to predict the targets of non-return branches,
 * and it uses a small Return Address Stack (RAS) to predict the target of
 * returns.
 * It uses GHRP strategy to evict BTB entries.
 * This only supports one cpu core so far
 */

#include "ooo_cpu.h"
#include <map>
#include "../access_counter.h"
#include "../prefetch_stream_buffer.h"

extern uint8_t total_btb_ways;

#define BASIC_BTB_SETS (2048 * 4 / total_btb_ways)
#define BASIC_BTB_WAYS total_btb_ways
#define BASIC_BTB_INDIRECT_SIZE 4096
#define BASIC_BTB_RAS_SIZE 32
#define BASIC_BTB_CALL_INSTR_SIZE_TRACKERS 1024

StreamBuffer stream_buffer(32);

struct BASIC_BTB_ENTRY {
    uint64_t ip_tag = 0;
    uint64_t target = 0;
    uint8_t always_taken = 0;
    uint64_t lru = 0;
    bool plru_timestamp = false;
    uint64_t lru_timestamp = 0;
    uint16_t signature = 0;
    bool dead_prediction = false;
};

//AccessCounter access_counter(BASIC_BTB_SETS, BASIC_BTB_WAYS);

//std::map<uint64_t, BASIC_BTB_ENTRY> basic_btb[NUM_CPUS][BASIC_BTB_SETS];
vector<vector<std::map<uint64_t, BASIC_BTB_ENTRY>>> basic_btb;

bool is_ghrp_plru = true;
// some arguments from their source code
// deadThresh is missing in their code, so I use it as same as bypassThresh
const int numCounts = 4096 * 1;
const int numPredTables = 3;
const static int counterSize = 4; // for n-bit counter, set it to 2^n
const int bypassThresh = 3, deadThresh = 3;

class GHRP {
private:
    uint32_t numLines = 0;
    uint64_t current_timestamp = 0;
//    uint16_t global_history[BASIC_BTB_SETS] = { 0 };
    vector<uint16_t> global_history;
    int predTables[numCounts][numPredTables] = {{0}};
    void init_pred_tables() {
        for (int i = 0; i < numCounts; i++) {
            for (int j = 0; j < numPredTables; j++) {
                predTables[i][j] = 0;
            }
        }
    }
public:
    GHRP() = default;

    void init() {
        numLines = BASIC_BTB_WAYS;
        current_timestamp = 0;
        global_history.resize(BASIC_BTB_SETS, 0);
    }

    void update_global_history(uint64_t pc, uint64_t set) {
        global_history[set] = (global_history[set] << 4) | ((pc & 7) << 1);
    }

    void hit_access(uint8_t cpu, uint64_t set, uint64_t addr, uint64_t pc) {
        auto sign = make_signature(pc, global_history[set]);
        auto cntrs = getCounters(computeIndices(sign));

        current_timestamp++;
        // update_history(addr);

        auto it = basic_btb[cpu][set].find(addr);
        auto &block = it->second;
        updatePredTable(computeIndices(block.signature), false);
        block.plru_timestamp = true;
        block.lru_timestamp = current_timestamp;
        block.dead_prediction = majorityVote(cntrs, deadThresh);
        block.signature = sign;
        if (basic_btb[cpu][set].size() == numLines) {
            bool all_set = true;
            for (const auto &kv : basic_btb[cpu][set]) {
                if (!kv.second.plru_timestamp) {
                    all_set = false;
                    break;
                }
            }
            if (all_set) {
                for (const auto &kv : basic_btb[cpu][set]) {
                    basic_btb[cpu][set][kv.first].plru_timestamp = false;
                }
            }
        }
        basic_btb[cpu][set][addr].plru_timestamp = true;
    }

    bool miss_access(uint8_t cpu, uint64_t set, uint64_t addr, uint64_t pc) {
        auto sign = make_signature(pc, global_history[set]);
        auto cntrs = getCounters(computeIndices(sign));

        current_timestamp++;
        // update_history(addr);

//        auto it = basic_btb[cpu][set].find(addr);
        bool need_bypass = majorityVote(cntrs, bypassThresh);
        // if bypass, return false without doing evition or update cache lines
        if (need_bypass)
            return false;

        // miss
        do_evition(cpu, set);
        BASIC_BTB_ENTRY block;
        updatePredTable(computeIndices(block.signature), true);
        block.plru_timestamp = true;
        block.lru_timestamp = current_timestamp;
        block.dead_prediction = majorityVote(cntrs, deadThresh);
        block.signature = sign;
        basic_btb[cpu][set].emplace(addr, block);

        if (basic_btb[cpu][set].size() == numLines) {
            bool all_set = true;
            for (const auto &kv : basic_btb[cpu][set]) {
                if (!kv.second.plru_timestamp) {
                    all_set = false;
                    break;
                }
            }
            if (all_set) {
                for (const auto &kv : basic_btb[cpu][set]) {
                    basic_btb[cpu][set][kv.first].plru_timestamp = false;
                }
            }
        }
        basic_btb[cpu][set][addr].plru_timestamp = true;
        return true;
    }

    bool access(uint8_t cpu, uint64_t set, uint64_t addr, uint64_t pc) {
        auto sign = make_signature(pc, global_history[set]);
        auto cntrs = getCounters(computeIndices(sign));

        current_timestamp++;
        // update_history(addr);

        auto it = basic_btb[cpu][set].find(addr);
        bool is_hit = (it != basic_btb[cpu][set].end());
        if (!is_hit) {
            bool need_bypass = majorityVote(cntrs, bypassThresh);
            // if bypass, return false without doing evition or update cache lines
            if (need_bypass)
                return false;

            // miss
            do_evition(cpu, set);
            BASIC_BTB_ENTRY block;
            updatePredTable(computeIndices(block.signature), true);
            block.plru_timestamp = true;
            block.lru_timestamp = current_timestamp;
            block.dead_prediction = majorityVote(cntrs, deadThresh);
            block.signature = sign;
            basic_btb[cpu][set].emplace(addr, block);
        } else {
            // hit
            auto &block = it->second;
            updatePredTable(computeIndices(block.signature), false);
            block.plru_timestamp = true;
            block.lru_timestamp = current_timestamp;
            block.dead_prediction = majorityVote(cntrs, deadThresh);
            block.signature = sign;
        }

        if (basic_btb[cpu][set].size() == numLines) {
            bool all_set = true;
            for (const auto &kv : basic_btb[cpu][set]) {
                if (!kv.second.plru_timestamp) {
                    all_set = false;
                    break;
                }
            }
            if (all_set) {
                for (const auto &kv : basic_btb[cpu][set]) {
                    basic_btb[cpu][set][kv.first].plru_timestamp = false;
                }
            }
        }
        basic_btb[cpu][set][addr].plru_timestamp = true;

        return is_hit;
    }

private:
    void do_evition(uint8_t cpu, uint64_t set) {
        if (basic_btb[cpu][set].size() < numLines)
            return;

        for (auto p : basic_btb[cpu][set])
            if (p.second.dead_prediction) {
                basic_btb[cpu][set].erase(p.first);
//                access_counter.evict(p.first, cpu);
                return;
            }

        // otherwise, return LRU block
        uint64_t oldest_addr = basic_btb[cpu][set].begin()->first;
        for (auto p : basic_btb[cpu][set]) {
            if (is_ghrp_plru) {
                if (!p.second.plru_timestamp) {
                    oldest_addr = p.first;
                    break;
                }
            } else {
                if (p.second.lru_timestamp < basic_btb[cpu][set][oldest_addr].lru_timestamp) {
                    oldest_addr = p.first;
                }
            }
        }
        assert(basic_btb[cpu][set].count(oldest_addr));
//        auto ret = cache[oldest_addr];
        basic_btb[cpu][set].erase(oldest_addr);
//        access_counter.evict(oldest_addr, cpu);
    }

    inline uint16_t make_signature(uint64_t pc, uint16_t his) {
        return (uint16_t)(pc ^ his);
    }

    inline vector<uint64_t> computeIndices(uint16_t signature) {
        vector<uint64_t> indices;
        for (int i = 0; i < numPredTables; i++)
            indices.push_back(hash(signature, i));
        return indices;
    }
    inline vector<int> getCounters(vector<uint64_t> indices) {
        vector<int> counters;
        for (int t = 0; t < numPredTables; t++)
            counters.push_back(predTables[indices[t]][t]);
        return counters;
    }

    inline bool majorityVote(vector<int> counters, int threshold) {
        int vote = 0;
        for (int i = 0; i < numPredTables; i++)
            if (counters[i] >= threshold)
                vote++;
        return (vote * 2 >= numPredTables);
    }
    inline void updatePredTable(vector<uint64_t> indices, bool isDead) {
        for (int t = 0; t < numPredTables; t++) {
            if (isDead) {
                if (predTables[indices[t]][t] < counterSize - 1) {
                    predTables[indices[t]][t] += 1;
                }
            } else {
                if (predTables[indices[t]][t] > 0) {
                    predTables[indices[t]][t] -= 1;
                }
            }
        }
    }

    // 3 hash functions from their source code, with my adjustment
    typedef uint64_t UINT64;
    inline UINT64 mix(UINT64 a, UINT64 b, UINT64 c) {
        a -= b;
        a -= c;
        a ^= (c >> 13);
        b -= c;
        b -= a;
        b ^= (a << 8);
        c -= a;
        c -= b;
        c ^= (b >> 13);
        a -= b;
        a -= c;
        a ^= (c >> 12);
        b -= c;
        b -= a;
        b ^= (a << 16);
        c -= a;
        c -= b;
        c ^= (b >> 5);
        a -= b;
        a -= c;
        a ^= (c >> 3);
        b -= c;
        b -= a;
        b ^= (a << 10);
        c -= a;
        c -= b;
        c ^= (b >> 15);
        return c;
    }
    inline UINT64 f1(UINT64 x) {
        UINT64 fone = mix(0xfeedface, 0xdeadb10c, x);
        return fone;
    }
    inline UINT64 f2(UINT64 x) {
        UINT64 ftwo = mix(0xc001d00d, 0xfade2b1c, x);
        return ftwo;
    }
    inline UINT64 fi(UINT64 x) {
        UINT64 ind = (f1(x)) + (f2(x));
        return ind;
    }
    inline uint64_t hash(uint16_t signature, int i) {
        if (i == 0)
            return f1(signature) & (numCounts - 1);
        else if (i == 1)
            return f2(signature) & (numCounts - 1);
        else if (i == 2)
            return fi(signature) & (numCounts - 1);
        else
            assert(false);
    }
};

GHRP ghrp[NUM_CPUS];

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

uint64_t basic_btb_set_index(uint64_t ip) { return ((ip >> 2) & (BASIC_BTB_SETS-1)); }

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
    std::cout << "GHRP BTB sets: " << BASIC_BTB_SETS
              << " ways: " << (int) BASIC_BTB_WAYS
              << " indirect buffer size: " << BASIC_BTB_INDIRECT_SIZE
              << " RAS size: " << BASIC_BTB_RAS_SIZE << std::endl;

//    for (uint32_t i = 0; i < BASIC_BTB_SETS; i++) {
//        for (uint32_t j = 0; j < BASIC_BTB_WAYS; j++) {
//            basic_btb[cpu][i][j].ip_tag = 0;
//            basic_btb[cpu][i][j].target = 0;
//            basic_btb[cpu][i][j].always_taken = 0;
//            basic_btb[cpu][i][j].lru = 0;
//        }
//    }
//    basic_btb_lru_counter[cpu] = 0;

    basic_btb.resize(NUM_CPUS, vector<std::map<uint64_t, BASIC_BTB_ENTRY>>(BASIC_BTB_SETS));

    ghrp[cpu].init();

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
        uint64_t set = basic_btb_set_index(ip);
        auto it = basic_btb[cpu][set].find(ip);
        if (it == basic_btb[cpu][set].end()) {
            // no prediction for this IP
            return std::make_pair(stream_buffer.stream_buffer_predict(ip), true);
        }
        always_taken = it->second.always_taken;
        // TODO: Update
        ghrp[cpu].hit_access(cpu, set, ip, ip);

        return std::make_pair(it->second.target, always_taken);
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
        uint64_t set = basic_btb_set_index(ip);
        auto it = basic_btb[cpu][set].find(ip);
        if (it == basic_btb[cpu][set].end()) {
            stream_buffer.stream_buffer_update(ip);
            if ((branch_target != 0) && taken) {
                // no prediction for this entry so far, so allocate one
                // TODO: do_eviction here
                if (ghrp[cpu].miss_access(cpu, set, ip, ip)) {
                    basic_btb[cpu][set][ip].ip_tag = ip;
                    basic_btb[cpu][set][ip].target = branch_target;
                    basic_btb[cpu][set][ip].always_taken = 1;
//                    access_counter.insert(ip, cpu, branch_target, branch_type);
                }
                ghrp[cpu].update_global_history(ip, set);
            }
        } else {
            // update an existing entry
            if (!taken) {
                it->second.always_taken = 0;
            } else {
                it->second.target = branch_target;
            }
            ghrp[cpu].update_global_history(ip, set);
//            access_counter.access(ip, cpu);
        }
    }
}

void O3_CPU::prefetch_btb(uint64_t ip, uint64_t branch_target, uint8_t branch_type, bool taken, bool to_stream_buffer) {
    // TODO: Here we only prefetch for direct btb, since the hash for indirect branch may change later
    if (branch_type != BRANCH_RETURN &&
        branch_type != BRANCH_INDIRECT &&
        branch_type != BRANCH_INDIRECT_CALL) {
        uint64_t set = basic_btb_set_index(ip);
        auto it = basic_btb[cpu][set].find(ip);
        if (it == basic_btb[cpu][set].end()) {
            if ((branch_target != 0) && taken) {
                if (to_stream_buffer) {
                    stream_buffer.prefetch(ip, branch_target);
                } else {
                    // no prediction for this entry so far, so allocate one
                    // TODO: do_eviction here
                    if (ghrp[cpu].miss_access(cpu, set, ip, ip)) {
                        basic_btb[cpu][set][ip].ip_tag = ip;
                        basic_btb[cpu][set][ip].target = branch_target;
                        basic_btb[cpu][set][ip].always_taken = 1;
//                    access_counter.insert(ip, cpu, branch_target, branch_type);
                    }
                    ghrp[cpu].update_global_history(ip, set);
                }
            }
        } else {
            // update an existing entry
            ghrp[cpu].hit_access(cpu, set, ip, ip);
            if (!taken) {
                it->second.always_taken = 0;
            } else {
                it->second.target = branch_target;
            }
            ghrp[cpu].update_global_history(ip, set);
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
}
