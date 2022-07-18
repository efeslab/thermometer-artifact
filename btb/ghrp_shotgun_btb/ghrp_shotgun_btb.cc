
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
#include <unordered_map>
#include "../prefetch_stream_buffer.h"

using std::map;
using std::unordered_map;
using std::vector;

#define BASIC_BTB_SETS 256
#define BASIC_BTB_WAYS 4
#define UNCONDITIONAL_BTB_SETS 1024
#define UNCONDITIONAL_BTB_WAYS 7
#define BASIC_BTB_INDIRECT_SIZE 4096
#define BASIC_BTB_RAS_SIZE 32
#define BASIC_BTB_CALL_INSTR_SIZE_TRACKERS 1024

uint64_t timestamp = 0;
StreamBuffer stream_buffer(32);

struct BASIC_BTB_ENTRY {
    uint64_t ip_tag = 0;
    uint64_t target = 0;
    uint8_t always_taken = 1; // TODO: should this be 0 or 1?
    uint8_t branch_type = BRANCH_DIRECT_CALL;
    uint64_t lru = 0;
    bool plru_timestamp = false;
    uint64_t lru_timestamp = 0;
    uint16_t signature = 0;
    bool dead_prediction = false;

    BASIC_BTB_ENTRY() = default;

    explicit BASIC_BTB_ENTRY(uint64_t ip, uint64_t target, uint8_t always_taken, uint8_t branch_type) :
            ip_tag(ip),
            target(target),
            always_taken(always_taken),
            branch_type(branch_type) {}
};

struct FOOTPRINT_BTB_ENTRY {
    uint64_t ip_tag = 0;
    uint64_t target = 0;
    uint8_t always_taken = 1;
    uint8_t branch_type = BRANCH_DIRECT_CALL;
    uint64_t lru = 0;
    bool plru_timestamp = false;
    uint64_t lru_timestamp = 0;
    uint16_t signature = 0;
    bool dead_prediction = false;
    unordered_map<uint64_t, vector<BASIC_BTB_ENTRY>> call_footprint;
    unordered_map<uint64_t, vector<BASIC_BTB_ENTRY>> return_footprint;

    FOOTPRINT_BTB_ENTRY() = default;

    explicit FOOTPRINT_BTB_ENTRY(uint64_t ip, uint64_t target, uint8_t always_taken, uint8_t branch_type) :
            ip_tag(ip),
            target(target),
            always_taken(always_taken),
            branch_type(branch_type) {}

    void cache_access(BASIC_BTB_ENTRY *access_entry, bool update_call = true) {
        // access_taken refers to taken or not!
        auto access_block = access_entry->ip_tag >> LOG2_BLOCK_SIZE;
        uint64_t original_block;
        unordered_map<uint64_t, vector<BASIC_BTB_ENTRY>> *footprint;
        if (update_call) {
            original_block = target >> LOG2_BLOCK_SIZE;
            footprint = &call_footprint;
        } else {
            original_block = ip_tag >> LOG2_BLOCK_SIZE;
            footprint = &return_footprint;
        }
        // Update footprint
        if (access_block <= original_block + 6 && access_block >= original_block - 2) {
            (*footprint)[access_block].push_back(*access_entry);
        }
    }

    void operate_prefetch(O3_CPU *o3_cpu, bool prefetch_call) {
        // This function does not actually prefetch instrs. It just add it to prefetch list.
        unordered_map<uint64_t, vector<BASIC_BTB_ENTRY>> *footprint;
        uint64_t key;
        if (prefetch_call) {
            key = ip_tag;
            footprint = &call_footprint;
        } else {
            key = target;
            footprint = &return_footprint;
        }
        for (auto &a : *footprint) {
            auto &region = o3_cpu->shotgun_prefetch_region[key];
            region.clear();
            for (auto &entry : a.second) {
                region.push_back(entry.ip_tag);
                // o3_cpu->prefetch_btb(entry.ip_tag, entry.target, entry.branch_type, entry.always_taken != 0);
            }
        }
        footprint->clear();
    }
};

class Shotgun {
private:
    FOOTPRINT_BTB_ENTRY *current_unconditional = nullptr;
    uint8_t branch_type = BRANCH_OTHER;

public:
    void update_current(O3_CPU *o3_cpu, FOOTPRINT_BTB_ENTRY *entry, uint8_t type) {
        current_unconditional = entry;
        branch_type = type;
        if (current_unconditional != nullptr)
            current_unconditional->operate_prefetch(o3_cpu, branch_type != BRANCH_RETURN);
    }

    void add_footprint(BASIC_BTB_ENTRY *access_entry) {
        if (current_unconditional != nullptr)
            current_unconditional->cache_access(access_entry, branch_type != BRANCH_RETURN);
    }
};

Shotgun shotgun;
vector<vector<std::map<uint64_t, BASIC_BTB_ENTRY>>> basic_btb(
        NUM_CPUS,
        vector<std::map<uint64_t, BASIC_BTB_ENTRY>>(BASIC_BTB_SETS));
vector<vector<std::map<uint64_t, FOOTPRINT_BTB_ENTRY>>> unconditional_btb(
        NUM_CPUS,
        vector<std::map<uint64_t, FOOTPRINT_BTB_ENTRY>>(UNCONDITIONAL_BTB_SETS));


//std::map<uint64_t, BASIC_BTB_ENTRY> basic_btb[NUM_CPUS][BASIC_BTB_SETS];

bool is_ghrp_plru = true;
// some arguments from their source code
// deadThresh is missing in their code, so I use it as same as bypassThresh
const int numCounts = 4096 * 1;
const int numPredTables = 3;
const static int counterSize = 4; // for n-bit counter, set it to 2^n
const int bypassThresh = 3, deadThresh = 3;

template<class T>
class GHRP {
private:
    vector<vector<std::map<uint64_t, T>>> *btb;
    uint32_t numLines;
    uint64_t current_timestamp;
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
    GHRP(uint64_t sets, uint64_t ways, vector<vector<std::map<uint64_t, T>>> *chosen_btb) : btb(chosen_btb) {
        numLines = ways;
        current_timestamp = 0;
        global_history.resize(sets, 0);
    }

    void update_global_history(uint64_t pc, uint64_t set) {
        global_history[set] = (global_history[set] << 4) | ((pc & 7) << 1);
    }

    void hit_access(uint8_t cpu, uint64_t set, uint64_t addr, uint64_t pc) {
        auto sign = make_signature(pc, global_history[set]);
        auto cntrs = getCounters(computeIndices(sign));

        current_timestamp++;
        // update_history(addr);

        auto it = (*btb)[cpu][set].find(addr);
        auto &block = it->second;
        updatePredTable(computeIndices(block.signature), false);
        block.plru_timestamp = true;
        block.lru_timestamp = current_timestamp;
        block.dead_prediction = majorityVote(cntrs, deadThresh);
        block.signature = sign;
        if ((*btb)[cpu][set].size() == numLines) {
            bool all_set = true;
            for (const auto &kv : (*btb)[cpu][set]) {
                if (!kv.second.plru_timestamp) {
                    all_set = false;
                    break;
                }
            }
            if (all_set) {
                for (const auto &kv : (*btb)[cpu][set]) {
                    (*btb)[cpu][set][kv.first].plru_timestamp = false;
                }
            }
        }
        (*btb)[cpu][set][addr].plru_timestamp = true;
    }

    bool miss_access(uint8_t cpu, uint64_t set, uint64_t addr, uint64_t pc) {
        auto sign = make_signature(pc, global_history[set]);
        auto cntrs = getCounters(computeIndices(sign));

        current_timestamp++;
        // update_history(addr);

//        auto it = (*btb)[cpu][set].find(addr);
        bool need_bypass = majorityVote(cntrs, bypassThresh);
        // if bypass, return false without doing evition or update cache lines
        if (need_bypass)
            return false;

        // miss
        do_evition(cpu, set);
        T block;
        updatePredTable(computeIndices(block.signature), true);
        block.plru_timestamp = true;
        block.lru_timestamp = current_timestamp;
        block.dead_prediction = majorityVote(cntrs, deadThresh);
        block.signature = sign;
        (*btb)[cpu][set].emplace(addr, block);

        if ((*btb)[cpu][set].size() == numLines) {
            bool all_set = true;
            for (const auto &kv : (*btb)[cpu][set]) {
                if (!kv.second.plru_timestamp) {
                    all_set = false;
                    break;
                }
            }
            if (all_set) {
                for (const auto &kv : (*btb)[cpu][set]) {
                    (*btb)[cpu][set][kv.first].plru_timestamp = false;
                }
            }
        }
        (*btb)[cpu][set][addr].plru_timestamp = true;
        return true;
    }

    bool access(uint8_t cpu, uint64_t set, uint64_t addr, uint64_t pc) {
        auto sign = make_signature(pc, global_history[set]);
        auto cntrs = getCounters(computeIndices(sign));

        current_timestamp++;
        // update_history(addr);

        auto it = (*btb)[cpu][set].find(addr);
        bool is_hit = (it != (*btb)[cpu][set].end());
        if (!is_hit) {
            bool need_bypass = majorityVote(cntrs, bypassThresh);
            // if bypass, return false without doing evition or update cache lines
            if (need_bypass)
                return false;

            // miss
            do_evition(cpu, set);
            T block;
            updatePredTable(computeIndices(block.signature), true);
            block.plru_timestamp = true;
            block.lru_timestamp = current_timestamp;
            block.dead_prediction = majorityVote(cntrs, deadThresh);
            block.signature = sign;
            (*btb)[cpu][set].emplace(addr, block);
        } else {
            // hit
            auto &block = it->second;
            updatePredTable(computeIndices(block.signature), false);
            block.plru_timestamp = true;
            block.lru_timestamp = current_timestamp;
            block.dead_prediction = majorityVote(cntrs, deadThresh);
            block.signature = sign;
        }

        if ((*btb)[cpu][set].size() == numLines) {
            bool all_set = true;
            for (const auto &kv : (*btb)[cpu][set]) {
                if (!kv.second.plru_timestamp) {
                    all_set = false;
                    break;
                }
            }
            if (all_set) {
                for (const auto &kv : (*btb)[cpu][set]) {
                    (*btb)[cpu][set][kv.first].plru_timestamp = false;
                }
            }
        }
        (*btb)[cpu][set][addr].plru_timestamp = true;

        return is_hit;
    }

private:
    void do_evition(uint8_t cpu, uint64_t set) {
        if ((*btb)[cpu][set].size() < numLines)
            return;

        for (auto p : (*btb)[cpu][set])
            if (p.second.dead_prediction) {
                (*btb)[cpu][set].erase(p.first);
                return;
            }

        // otherwise, return LRU block
        uint64_t oldest_addr = (*btb)[cpu][set].begin()->first;
        for (auto p : (*btb)[cpu][set]) {
            if (is_ghrp_plru) {
                if (!p.second.plru_timestamp) {
                    oldest_addr = p.first;
                    break;
                }
            } else {
                if (p.second.lru_timestamp < (*btb)[cpu][set][oldest_addr].lru_timestamp) {
                    oldest_addr = p.first;
                }
            }
        }
        assert((*btb)[cpu][set].count(oldest_addr));
//        auto ret = cache[oldest_addr];
        (*btb)[cpu][set].erase(oldest_addr);
    }

    inline uint16_t make_signature(uint64_t pc, uint16_t his) {
        return (uint16_t) (pc ^ his);
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

vector<GHRP<BASIC_BTB_ENTRY>> basic_ghrp(NUM_CPUS, GHRP<BASIC_BTB_ENTRY>(BASIC_BTB_SETS, BASIC_BTB_WAYS, &basic_btb));
vector<GHRP<FOOTPRINT_BTB_ENTRY>> unconditional_ghrp(NUM_CPUS, GHRP<FOOTPRINT_BTB_ENTRY>(UNCONDITIONAL_BTB_SETS,
                                                                                         UNCONDITIONAL_BTB_WAYS,
                                                                                         &unconditional_btb));

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
    if (addr1 > addr2) {
        return addr1 - addr2;
    }

    return addr2 - addr1;
}

uint64_t basic_btb_set_index(uint64_t ip, uint64_t total_sets) { return ((ip >> 2) & (total_sets - 1)); }

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
    std::cout << "GHRP Shotgun BTB sets: " << BASIC_BTB_SETS
              << " ways: " << BASIC_BTB_WAYS
              << " unconditional BTB sets: " << UNCONDITIONAL_BTB_SETS
              << " ways: " << UNCONDITIONAL_BTB_WAYS
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
    } else if (branch_type == BRANCH_CONDITIONAL) {
        // Access C-BTB
        uint64_t set = basic_btb_set_index(ip, BASIC_BTB_SETS);
        auto it = basic_btb[cpu][set].find(ip);
        if (it == basic_btb[cpu][set].end()) {
            // no prediction for this IP
            return std::make_pair(stream_buffer.stream_buffer_predict(ip), true);
        }
        always_taken = it->second.always_taken;
        basic_ghrp[cpu].hit_access(cpu, set, ip, ip);

        return std::make_pair(it->second.target, always_taken);
    } else {
        // Access U-BTB
        uint64_t set = basic_btb_set_index(ip, UNCONDITIONAL_BTB_SETS);
        auto it = unconditional_btb[cpu][set].find(ip);
        if (it == unconditional_btb[cpu][set].end()) {
            // no prediction for this IP
            always_taken = true;
            return std::make_pair(0, always_taken);
        }
        always_taken = it->second.always_taken;
        unconditional_ghrp[cpu].hit_access(cpu, set, ip, ip);

        return std::make_pair(it->second.target, always_taken);
    }
}

void O3_CPU::update_btb(uint64_t ip, uint64_t branch_target, uint8_t taken,
                        uint8_t branch_type) {
    // updates for indirect branches
    if ((branch_type == BRANCH_INDIRECT) ||
        (branch_type == BRANCH_INDIRECT_CALL)) {
        basic_btb_indirect[cpu][basic_btb_indirect_hash(cpu, ip)] = branch_target;
        return;
    }
    if (branch_type == BRANCH_CONDITIONAL) {
        basic_btb_conditional_history[cpu] <<= 1;
        if (taken) {
            basic_btb_conditional_history[cpu] |= 1;
        }
        // Update basic_btb and add footprint
        auto set = basic_btb_set_index(ip, BASIC_BTB_SETS);
        auto it = basic_btb[cpu][set].find(ip);
        if (it == basic_btb[cpu][set].end()) {
            stream_buffer.stream_buffer_update(ip);
            if ((branch_target != 0) && taken) {
                // no prediction for this entry so far, so allocate one
                // TODO: do_eviction here
                if (basic_ghrp[cpu].miss_access(cpu, set, ip, ip)) {
                    basic_btb[cpu][set][ip].ip_tag = ip;
                    basic_btb[cpu][set][ip].target = branch_target;
                    basic_btb[cpu][set][ip].always_taken = 1;
                    basic_btb[cpu][set][ip].branch_type = branch_type;
                }
                basic_ghrp[cpu].update_global_history(ip, set);
                // Add footprint
                shotgun.add_footprint(&basic_btb[cpu][set][ip]);
            }
        } else {
            // update an existing entry
            if (!taken) {
                it->second.always_taken = 0;
            } else {
                it->second.target = branch_target;
            }
            it->second.branch_type = branch_type;
            basic_ghrp[cpu].update_global_history(ip, set);
            shotgun.add_footprint(&(it->second));
        }

    } else if (branch_type == BRANCH_RETURN) {
        // recalibrate call-return offset
        // if our return prediction got us into the right ball park, but not the
        // exactly correct byte target, then adjust our call instr size tracker
        uint64_t call_ip = pop_basic_btb_ras(cpu);
        uint64_t estimated_call_instr_size = basic_btb_abs_addr_dist(call_ip, branch_target);
        if (estimated_call_instr_size <= 10) {
            basic_btb_call_instr_sizes[cpu][basic_btb_call_size_tracker_hash(call_ip)] = estimated_call_instr_size;
        }
        // Predecode and update current
        auto set = basic_btb_set_index(call_ip, UNCONDITIONAL_BTB_SETS);
        auto it = unconditional_btb[cpu][set].find(call_ip);
        auto btb_entry = it == unconditional_btb[cpu][set].end() ? nullptr : &(it->second);
        shotgun.update_current(this, btb_entry, branch_type);
    } else {
        // BRANCH_DIRECT_JUMP or BRANCH_DIRECT_CALL
        uint64_t set = basic_btb_set_index(ip, UNCONDITIONAL_BTB_SETS);
        auto it = unconditional_btb[cpu][set].find(ip);
        if (it == unconditional_btb[cpu][set].end()) {
            if ((branch_target != 0) && taken) {
                // no prediction for this entry so far, so allocate one
                // TODO: do_eviction here
                if (unconditional_ghrp[cpu].miss_access(cpu, set, ip, ip)) {
                    unconditional_btb[cpu][set][ip].ip_tag = ip;
                    unconditional_btb[cpu][set][ip].target = branch_target;
                    unconditional_btb[cpu][set][ip].always_taken = 1;
                    unconditional_btb[cpu][set][ip].branch_type = branch_type;
                }
                unconditional_ghrp[cpu].update_global_history(ip, set);
                shotgun.update_current(this, &(unconditional_btb[cpu][set][ip]), branch_type);
            }
        } else {
            // update an existing entry
            if (!taken) {
                it->second.always_taken = 0;
            } else {
                it->second.target = branch_target;
            }
            it->second.branch_type = branch_type;
            unconditional_ghrp[cpu].update_global_history(ip, set);
            // Prefetch conditional branches in the target region
            shotgun.update_current(this, &(it->second), branch_type);
        }
    }
}

void O3_CPU::prefetch_btb(uint64_t ip, uint64_t branch_target, uint8_t branch_type, bool taken) {
    // Only for conditional branch
    if (branch_type == BRANCH_CONDITIONAL) {
        uint64_t set = basic_btb_set_index(ip, BASIC_BTB_SETS);
        auto it = basic_btb[cpu][set].find(ip);
        if (it == basic_btb[cpu][set].end()) {
            if ((branch_target != 0) && taken) {
                // no prediction for this entry so far, so allocate one
                // do_eviction here
                if (to_stream_buffer) {
                    stream_buffer.prefetch(ip, branch_target);
                } else {
                    if (basic_ghrp[cpu].miss_access(cpu, set, ip, ip)) {
                        basic_btb[cpu][set][ip].ip_tag = ip;
                        basic_btb[cpu][set][ip].target = branch_target;
                        basic_btb[cpu][set][ip].always_taken = 1;
                        basic_btb[cpu][set][ip].branch_type = branch_type;
                    }
                    basic_ghrp[cpu].update_global_history(ip, set);
                }
            }
        } else {
            // update an existing entry
            basic_ghrp[cpu].hit_access(cpu, set, ip, ip);
            if (!taken) {
                it->second.always_taken = 0;
            } else {
                it->second.target = branch_target;
            }
            it->second.branch_type = branch_type;
            basic_ghrp[cpu].update_global_history(ip, set);
        }
    }
}

