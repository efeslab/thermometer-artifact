
/*
 * This file implements a basic Branch Target Buffer (BTB) structure.
 * It uses a set-associative BTB to predict the targets of non-return branches,
 * and it uses a small Return Address Stack (RAS) to predict the target of
 * returns.
 */

#include "ooo_cpu.h"
#include "hawkeye_predictor.h"
#include "optgen.h"
#include <unordered_map>
#include <vector>
#include "../prefetch_stream_buffer.h"

#define BASIC_BTB_SETS 256
#define BASIC_BTB_WAYS 4
#define UNCONDITIONAL_BTB_SETS 1024
#define UNCONDITIONAL_BTB_WAYS 7
#define BASIC_BTB_INDIRECT_SIZE 4096
#define BASIC_BTB_RAS_SIZE 32
#define BASIC_BTB_CALL_INSTR_SIZE_TRACKERS 1024

using std::unordered_map;
using std::vector;
StreamBuffer stream_buffer(32);

struct BASIC_BTB_ENTRY {
    uint64_t ip_tag;
    uint64_t target;
    uint8_t always_taken;
    uint8_t branch_type;
    uint64_t lru = 0;

    explicit BASIC_BTB_ENTRY(uint64_t ip = 0, uint64_t target = 0, uint8_t always_taken = 1,
                             uint8_t branch_type = BRANCH_DIRECT_CALL) :
            ip_tag(ip),
            target(target),
            always_taken(always_taken),
            branch_type(branch_type) {}
};

struct FOOTPRINT_BTB_ENTRY {
    uint64_t ip_tag;
    uint64_t target;
    uint8_t always_taken;
    uint8_t branch_type;
    uint64_t lru = 0;
    unordered_map<uint64_t, vector<BASIC_BTB_ENTRY>> call_footprint;
    unordered_map<uint64_t, vector<BASIC_BTB_ENTRY>> return_footprint;

    explicit FOOTPRINT_BTB_ENTRY(uint64_t ip = 0, uint64_t target = 0, uint8_t always_taken = 1,
                                 uint8_t branch_type = BRANCH_DIRECT_CALL) :
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
vector<vector<vector<BASIC_BTB_ENTRY>>> conditional_btb(
        NUM_CPUS,
        vector<vector<BASIC_BTB_ENTRY>>(
                BASIC_BTB_SETS,
                vector<BASIC_BTB_ENTRY>(BASIC_BTB_WAYS)));
vector<vector<vector<FOOTPRINT_BTB_ENTRY>>> unconditional_btb(
        NUM_CPUS,
        vector<vector<FOOTPRINT_BTB_ENTRY>>(
                UNCONDITIONAL_BTB_SETS,
                vector<FOOTPRINT_BTB_ENTRY>(UNCONDITIONAL_BTB_WAYS)));


//#define NUM_CORE 1
//#define LLC_SETS NUM_CORE*2048
//#define LLC_WAYS 16

//3-bit RRIP counters or all lines
#define maxRRPV 7
//uint32_t rrpv[BASIC_BTB_SETS][BASIC_BTB_WAYS];


//Per-set timers; we only use 64 of these
//Budget = 64 sets * 1 timer per set * 10 bits per timer = 80 bytes
#define TIMER_SIZE 1024
//uint64_t perset_mytimer[BASIC_BTB_SETS];

// Signatures for sampled sets; we only use 64 of these
// Budget = 64 sets * 16 ways * 12-bit signature per line = 1.5B
//uint64_t signatures[BASIC_BTB_SETS][BASIC_BTB_WAYS];
//bool prefetched[BASIC_BTB_SETS][BASIC_BTB_WAYS];

// Hawkeye Predictors for demand and prefetch requests
// Predictor with 2K entries and 5-bit counter per entry
// Budget = 2048*5/8 bytes = 1.2KB
//#define MAX_SHCT 31
//#define SHCT_SIZE_BITS 11
//#define SHCT_SIZE (1<<SHCT_SIZE_BITS)
#include "./hawkeye_predictor.h"
//HAWKEYE_PC_PREDICTOR* demand_predictor;  //Predictor
//HAWKEYE_PC_PREDICTOR* prefetch_predictor;  //Predictor

//#define OPTGEN_VECTOR_SIZE 128
#include "optgen.h"
//OPTgen perset_optgen[BASIC_BTB_SETS]; // per-set occupancy vectors; we only use 64 of these

#include <math.h>

#define bitmask(l) (((l) == 64) ? (unsigned long long)(-1LL) : ((1LL << (l))-1LL))
#define bits(x, i, l) (((x) >> (i)) & bitmask(l))
//Sample 64 sets per core
#define SAMPLED_SET(set, total_sets) (bits(set, 0 , 6) == bits(set, ((unsigned long long)log2(total_sets) - 6), 6) )

// Sampler to track 8x cache history for sampled sets
// 2800 entris * 4 bytes per entry = 11.2KB
#define SAMPLED_CACHE_SIZE 2800
#define SAMPLER_WAYS 8
#define SAMPLER_SETS SAMPLED_CACHE_SIZE/SAMPLER_WAYS
vector<map<uint64_t, ADDR_INFO> > addr_history; // Sampler

class Hawkeye {
private:
    uint64_t total_sets;
    uint64_t total_ways;
    vector<vector<uint64_t>> rrpv;
    vector<uint64_t> perset_mytimer;
    vector<vector<uint64_t>> signatures;
    vector<vector<bool>> prefetched;
    HAWKEYE_PC_PREDICTOR *demand_predictor;  //Predictor
    HAWKEYE_PC_PREDICTOR *prefetch_predictor;  //Predictor
    vector<OPTgen> perset_optgen;
    vector<map<uint64_t, ADDR_INFO> > addr_history;

public:
    Hawkeye(uint64_t sets, uint64_t ways) : total_sets(sets), total_ways(ways) {
        rrpv.resize(sets, vector<uint64_t>(ways));
        perset_mytimer.resize(sets);
        signatures.resize(sets, vector<uint64_t>(ways));
        prefetched.resize(sets, vector<bool>(ways));
        demand_predictor = new HAWKEYE_PC_PREDICTOR();
        prefetch_predictor = new HAWKEYE_PC_PREDICTOR();
        perset_optgen.resize(sets);
    }

    ~Hawkeye() {
        delete demand_predictor;
        delete prefetch_predictor;
    }

    // initialize replacement state
    void InitReplacementState() {
        for (int i = 0; i < total_sets; i++) {
            for (int j = 0; j < total_ways; j++) {
                rrpv[i][j] = maxRRPV;
                signatures[i][j] = 0;
                prefetched[i][j] = false;
            }
            perset_mytimer[i] = 0;
            perset_optgen[i].init(total_ways - 2);
        }

        addr_history.resize(SAMPLER_SETS);
        for (int i = 0; i < SAMPLER_SETS; i++)
            addr_history[i].clear();

//    demand_predictor = new HAWKEYE_PC_PREDICTOR();
//    prefetch_predictor = new HAWKEYE_PC_PREDICTOR();

        cout << "Initialize Hawkeye state" << endl;
    }

// find replacement victim
// return value should be 0 ~ 15 or 16 (bypass)
    uint32_t
    GetVictimInSet(uint32_t cpu, uint32_t set, const BLOCK *current_set, uint64_t PC, uint64_t paddr, uint32_t type) {
        // look for the maxRRPV line
        for (uint32_t i = 0; i < total_ways; i++)
            if (rrpv[set][i] == maxRRPV)
                return i;

        //If we cannot find a cache-averse line, we evict the oldest cache-friendly line
        uint32_t max_rrip = 0;
        int32_t lru_victim = -1;
        for (uint32_t i = 0; i < total_ways; i++) {
            if (rrpv[set][i] >= max_rrip) {
                max_rrip = rrpv[set][i];
                lru_victim = i;
            }
        }

        assert (lru_victim != -1);
        //The predictor is trained negatively on LRU evictions
        if (SAMPLED_SET(set, total_sets)) {
            if (prefetched[set][lru_victim])
                prefetch_predictor->decrement(signatures[set][lru_victim]);
            else
                demand_predictor->decrement(signatures[set][lru_victim]);
        }
        return lru_victim;

        // WE SHOULD NOT REACH HERE
        assert(0);
        return 0;
    }

    void replace_addr_history_element(unsigned int sampler_set) {
        uint64_t lru_addr = 0;

        for (map<uint64_t, ADDR_INFO>::iterator it = addr_history[sampler_set].begin();
             it != addr_history[sampler_set].end(); it++) {
            //     uint64_t timer = (it->second).last_quanta;

            if ((it->second).lru == (SAMPLER_WAYS - 1)) {
                //lru_time =  (it->second).last_quanta;
                lru_addr = it->first;
                break;
            }
        }

        addr_history[sampler_set].erase(lru_addr);
    }

    void update_addr_history_lru(unsigned int sampler_set, unsigned int curr_lru) {
        for (map<uint64_t, ADDR_INFO>::iterator it = addr_history[sampler_set].begin();
             it != addr_history[sampler_set].end(); it++) {
            if ((it->second).lru < curr_lru) {
                (it->second).lru++;
                assert((it->second).lru < SAMPLER_WAYS);
            }
        }
    }

// called on every cache hit and cache fill
    void
    UpdateReplacementState(uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr, uint64_t PC, uint64_t victim_addr,
                           uint32_t type, uint8_t hit) {
        paddr = (paddr >> 6) << 6;

        if (type == PREFETCH) {
            if (!hit)
                prefetched[set][way] = true;
        } else
            prefetched[set][way] = false;

        //Ignore writebacks
        if (type == WRITEBACK)
            return;


        //If we are sampling, OPTgen will only see accesses from sampled sets
        if (SAMPLED_SET(set, total_sets)) {
            //The current timestep
            uint64_t curr_quanta = perset_mytimer[set] % OPTGEN_VECTOR_SIZE;

            uint32_t sampler_set = (paddr >> 6) % SAMPLER_SETS;
            uint64_t sampler_tag = CRC(paddr >> 12) % 256;
            assert(sampler_set < SAMPLER_SETS);

            // This line has been used before. Since the right end of a usage interval is always
            //a demand, ignore prefetches
            if ((addr_history[sampler_set].find(sampler_tag) != addr_history[sampler_set].end()) &&
                (type != PREFETCH)) {
                unsigned int curr_timer = perset_mytimer[set];
                if (curr_timer < addr_history[sampler_set][sampler_tag].last_quanta)
                    curr_timer = curr_timer + TIMER_SIZE;
                bool wrap = ((curr_timer - addr_history[sampler_set][sampler_tag].last_quanta) > OPTGEN_VECTOR_SIZE);
                uint64_t last_quanta = addr_history[sampler_set][sampler_tag].last_quanta % OPTGEN_VECTOR_SIZE;
                //and for prefetch hits, we train the last prefetch trigger PC
                if (!wrap && perset_optgen[set].should_cache(curr_quanta, last_quanta)) {
                    if (addr_history[sampler_set][sampler_tag].prefetched)
                        prefetch_predictor->increment(addr_history[sampler_set][sampler_tag].PC);
                    else
                        demand_predictor->increment(addr_history[sampler_set][sampler_tag].PC);
                } else {
                    //Train the predictor negatively because OPT would not have cached this line
                    if (addr_history[sampler_set][sampler_tag].prefetched)
                        prefetch_predictor->decrement(addr_history[sampler_set][sampler_tag].PC);
                    else
                        demand_predictor->decrement(addr_history[sampler_set][sampler_tag].PC);
                }
                //Some maintenance operations for OPTgen
                perset_optgen[set].add_access(curr_quanta);
                update_addr_history_lru(sampler_set, addr_history[sampler_set][sampler_tag].lru);

                //Since this was a demand access, mark the prefetched bit as false
                addr_history[sampler_set][sampler_tag].prefetched = false;
            }
                // This is the first time we are seeing this line (could be demand or prefetch)
            else if (addr_history[sampler_set].find(sampler_tag) == addr_history[sampler_set].end()) {
                // Find a victim from the sampled cache if we are sampling
                if (addr_history[sampler_set].size() == SAMPLER_WAYS)
                    replace_addr_history_element(sampler_set);

                assert(addr_history[sampler_set].size() < SAMPLER_WAYS);
                //Initialize a new entry in the sampler
                addr_history[sampler_set][sampler_tag].init(curr_quanta);
                //If it's a prefetch, mark the prefetched bit;
                if (type == PREFETCH) {
                    addr_history[sampler_set][sampler_tag].mark_prefetch();
                    perset_optgen[set].add_prefetch(curr_quanta);
                } else
                    perset_optgen[set].add_access(curr_quanta);
                update_addr_history_lru(sampler_set, SAMPLER_WAYS - 1);
            } else //This line is a prefetch
            {
                assert(addr_history[sampler_set].find(sampler_tag) != addr_history[sampler_set].end());
                //if(hit && prefetched[set][way])
                uint64_t last_quanta = addr_history[sampler_set][sampler_tag].last_quanta % OPTGEN_VECTOR_SIZE;
                if (perset_mytimer[set] - addr_history[sampler_set][sampler_tag].last_quanta < 5 * NUM_CPUS) {
                    if (perset_optgen[set].should_cache(curr_quanta, last_quanta)) {
                        if (addr_history[sampler_set][sampler_tag].prefetched)
                            prefetch_predictor->increment(addr_history[sampler_set][sampler_tag].PC);
                        else
                            demand_predictor->increment(addr_history[sampler_set][sampler_tag].PC);
                    }
                }

                //Mark the prefetched bit
                addr_history[sampler_set][sampler_tag].mark_prefetch();
                //Some maintenance operations for OPTgen
                perset_optgen[set].add_prefetch(curr_quanta);
                update_addr_history_lru(sampler_set, addr_history[sampler_set][sampler_tag].lru);
            }

            // Get Hawkeye's prediction for this line
            bool new_prediction = demand_predictor->get_prediction(PC);
            if (type == PREFETCH)
                new_prediction = prefetch_predictor->get_prediction(PC);
            // Update the sampler with the timestamp, PC and our prediction
            // For prefetches, the PC will represent the trigger PC
            addr_history[sampler_set][sampler_tag].update(perset_mytimer[set], PC, new_prediction);
            addr_history[sampler_set][sampler_tag].lru = 0;
            //Increment the set timer
            perset_mytimer[set] = (perset_mytimer[set] + 1) % TIMER_SIZE;
        }

        bool new_prediction = demand_predictor->get_prediction(PC);
        if (type == PREFETCH)
            new_prediction = prefetch_predictor->get_prediction(PC);

        signatures[set][way] = PC;

        //Set RRIP values and age cache-friendly line
        if (!new_prediction)
            rrpv[set][way] = maxRRPV;
        else {
            rrpv[set][way] = 0;
            if (!hit) {
                bool saturated = false;
                for (uint32_t i = 0; i < total_ways; i++)
                    if (rrpv[set][i] == maxRRPV - 1)
                        saturated = true;

                //Age all the cache-friendly  lines
                for (uint32_t i = 0; i < total_ways; i++) {
                    if (!saturated && rrpv[set][i] < maxRRPV - 1)
                        rrpv[set][i]++;
                }
            }
            rrpv[set][way] = 0;
        }
    }
};

Hawkeye conditional_hawkeye(BASIC_BTB_SETS, BASIC_BTB_WAYS);
Hawkeye unconditional_hawkeye(UNCONDITIONAL_BTB_SETS, UNCONDITIONAL_BTB_WAYS);

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

template<class T>
T *basic_btb_find_entry(uint8_t cpu, uint64_t ip, uint32_t *way, vector<vector<vector<T>>> &btb) {
    uint64_t set = basic_btb_set_index(ip, btb[cpu].size());
    for (uint32_t i = 0; i < btb[cpu][set].size(); i++) {
        if (btb[cpu][set][i].ip_tag == ip) {
            *way = i;
            return &(btb[cpu][set][i]);
        }
    }
    return nullptr;
}

//BASIC_BTB_ENTRY *basic_btb_get_lru_entry(uint8_t cpu, uint64_t set) {
//    uint32_t lru_way = 0;
//    uint64_t lru_value = basic_btb[cpu][set][lru_way].lru;
//    for (uint32_t i = 0; i < BASIC_BTB_WAYS; i++) {
//        if (basic_btb[cpu][set][i].lru < lru_value) {
//            lru_way = i;
//            lru_value = basic_btb[cpu][set][lru_way].lru;
//        }
//    }
//
//    return &(basic_btb[cpu][set][lru_way]);
//}

//void basic_btb_update_lru(uint8_t cpu, BASIC_BTB_ENTRY *btb_entry) {
//    btb_entry->lru = basic_btb_lru_counter[cpu];
//    basic_btb_lru_counter[cpu]++;
//}

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
    std::cout << "Hawkeye Shotgun BTB sets: " << BASIC_BTB_SETS
              << " ways: " << BASIC_BTB_WAYS
              << " unconditional BTB sets: " << UNCONDITIONAL_BTB_SETS
              << " ways: " << UNCONDITIONAL_BTB_WAYS
              << " indirect buffer size: " << BASIC_BTB_INDIRECT_SIZE
              << " RAS size: " << BASIC_BTB_RAS_SIZE << std::endl;

    conditional_hawkeye.InitReplacementState();
    unconditional_hawkeye.InitReplacementState();

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
        uint32_t way = -1;
        auto btb_entry = basic_btb_find_entry(cpu, ip, &way, conditional_btb);

        if (btb_entry == nullptr) {
            // no prediction for this IP
            return std::make_pair(stream_buffer.stream_buffer_predict(ip), true);
        }

        always_taken = btb_entry->always_taken;
//        basic_btb_update_lru(cpu, btb_entry);
        auto set = basic_btb_set_index(ip, BASIC_BTB_SETS);
        assert(way != -1);
        conditional_hawkeye.UpdateReplacementState(cpu, set, way, ip, ip, 0, 0, 1);

        return std::make_pair(btb_entry->target, always_taken);
    } else {
        // Access U-BTB
        uint32_t way = -1;
        auto btb_entry = basic_btb_find_entry(cpu, ip, &way, unconditional_btb);

        if (btb_entry == nullptr) {
            // no prediction for this IP
            always_taken = true;
            return std::make_pair(0, always_taken);
        }

        always_taken = btb_entry->always_taken;
//        basic_btb_update_lru(cpu, btb_entry);
        auto set = basic_btb_set_index(ip, UNCONDITIONAL_BTB_SETS);
        assert(way != -1);
        unconditional_hawkeye.UpdateReplacementState(cpu, set, way, ip, ip, 0, 0, 1);

        return std::make_pair(btb_entry->target, always_taken);
    }

    return std::make_pair(0, always_taken);
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
        // TODO: Update basic_btb and add footprint
        uint32_t way = -1;
        auto btb_entry = basic_btb_find_entry(cpu, ip, &way, conditional_btb);

        if (btb_entry == nullptr) {
            stream_buffer.stream_buffer_update(ip);
            if ((branch_target != 0) && taken) {
                // no prediction for this entry so far, so allocate one
                uint64_t set = basic_btb_set_index(ip, BASIC_BTB_SETS);

                way = conditional_hawkeye.GetVictimInSet(cpu, set, nullptr, ip, ip, 0);
                assert(way != -1);

                auto repl_entry = &(conditional_btb[cpu][set][way]);

                repl_entry->ip_tag = ip;
                repl_entry->target = branch_target;
                repl_entry->always_taken = 1;
                repl_entry->branch_type = branch_type;
//                basic_btb_update_lru(cpu, repl_entry);
                conditional_hawkeye.UpdateReplacementState(cpu, set, way, ip, ip, 0, 0, 0);
                shotgun.add_footprint(repl_entry);
            }
        } else {
            // update an existing entry
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
            }
            btb_entry->branch_type = branch_type;
            shotgun.add_footprint(btb_entry);
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
        uint32_t way = -1;
        auto btb_entry = basic_btb_find_entry(cpu, call_ip, &way, unconditional_btb);
        shotgun.update_current(this, btb_entry, branch_type);
    } else {
        // BRANCH_DIRECT_JUMP or BRANCH_DIRECT_CALL
        uint32_t way = -1;
        auto btb_entry = basic_btb_find_entry(cpu, ip, &way, unconditional_btb);

        if (btb_entry == nullptr) {
            if ((branch_target != 0) && taken) {
                // no prediction for this entry so far, so allocate one
                uint64_t set = basic_btb_set_index(ip, UNCONDITIONAL_BTB_SETS);

                way = unconditional_hawkeye.GetVictimInSet(cpu, set, nullptr, ip, ip, 0);
                assert(way != -1);

                auto repl_entry = &(unconditional_btb[cpu][set][way]);

                repl_entry->ip_tag = ip;
                repl_entry->target = branch_target;
                repl_entry->always_taken = 1;
                repl_entry->branch_type = branch_type;
//                basic_btb_update_lru(cpu, repl_entry);
                unconditional_hawkeye.UpdateReplacementState(cpu, set, way, ip, ip, 0, 0, 0);
                shotgun.update_current(this, repl_entry, branch_type);
            }
        } else {
            // update an existing entry
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
            }
            btb_entry->branch_type = branch_type;
            // Prefetch conditional branches in the target region
            shotgun.update_current(this, btb_entry, branch_type);
        }
    }
}

void O3_CPU::prefetch_btb(uint64_t ip, uint64_t branch_target, uint8_t branch_type, bool taken) {
    // TODO: Here we only prefetch for direct btb, since the hash for indirect branch may change later
    if (branch_type == BRANCH_CONDITIONAL) {
        // use BTB
        uint32_t way = -1;
        auto btb_entry = basic_btb_find_entry(cpu, ip, &way, conditional_btb);

        if (btb_entry == NULL) {
            if ((branch_target != 0) && taken) {
                // no prediction for this entry so far, so allocate one
                if (to_stream_buffer) {
                    stream_buffer.prefetch(ip, branch_target);
                } else {
                    uint64_t set = basic_btb_set_index(ip, BASIC_BTB_SETS);

                    way = conditional_hawkeye.GetVictimInSet(cpu, set, nullptr, ip, ip, PREFETCH);
                    assert(way != -1);

                    auto repl_entry = &(conditional_btb[cpu][set][way]);

                    repl_entry->ip_tag = ip;
                    repl_entry->target = branch_target;
                    repl_entry->always_taken = 1;
                    repl_entry->branch_type = branch_type;
//                basic_btb_update_lru(cpu, repl_entry);
                    conditional_hawkeye.UpdateReplacementState(cpu, set, way, ip, ip, 0, PREFETCH, 0);
                }
            }
        } else {
            auto set = basic_btb_set_index(ip, BASIC_BTB_SETS);
            assert(way != -1);
            conditional_hawkeye.UpdateReplacementState(cpu, set, way, ip, ip, 0, PREFETCH, 1);
            // update an existing entry
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
            }
            btb_entry->branch_type = branch_type;
        }
    }
}

