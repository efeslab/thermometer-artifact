
/*
 * This file implements a basic Branch Target Buffer (BTB) structure.
 * It uses a set-associative BTB to predict the targets of non-return branches,
 * and it uses a small Return Address Stack (RAS) to predict the target of
 * returns.
 */

#include "ooo_cpu.h"
#include <vector>
#include "../prefetch_stream_buffer.h"

using std::unordered_map;
using std::vector;

#define CONDITIONAL_BTB_SETS 384
#define CONDITIONAL_BTB_WAYS 4
#define UNCONDITIONAL_BTB_SETS 1280
#define UNCONDITIONAL_BTB_WAYS 4
#define BASIC_BTB_INDIRECT_SIZE 4096
#define BASIC_BTB_RAS_SIZE 1536
#define BASIC_BTB_CALL_INSTR_SIZE_TRACKERS 1024

uint64_t con_timestamp = 0;
uint64_t uncon_timestamp = 0;
StreamBuffer stream_buffer(32);

struct BASIC_BTB_ENTRY {
    uint64_t ip_tag = 0;
    uint64_t target = 0;
    uint8_t always_taken = 0;
    uint64_t lru = 0;

    BASIC_BTB_ENTRY() = default;

    BASIC_BTB_ENTRY(uint64_t ip, uint64_t target, uint8_t always_taken, uint8_t branch_type) :
            ip_tag(ip),
            target(target),
            always_taken(always_taken) {}
};

struct FOOTPRINT_BTB_ENTRY {
    uint64_t ip_tag = 0;
    uint64_t target = 0;
    uint8_t always_taken = 0;
    uint8_t branch_type = BRANCH_DIRECT_CALL;
    uint64_t lru = 0;
    unordered_map<uint64_t, vector<BASIC_BTB_ENTRY>> call_footprint;
    unordered_map<uint64_t, vector<BASIC_BTB_ENTRY>> return_footprint;

    FOOTPRINT_BTB_ENTRY() = default;

    FOOTPRINT_BTB_ENTRY(uint64_t ip, uint64_t target, uint8_t always_taken, uint8_t branch_type) :
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
    vector<vector<vector<BASIC_BTB_ENTRY>>> conditional_btb;
    vector<vector<vector<FOOTPRINT_BTB_ENTRY>>> unconditional_btb;

    uint64_t conditional_btb_lru_counter[NUM_CPUS];
    uint64_t unconditional_btb_lru_counter[NUM_CPUS];

    void init() {
        conditional_btb.resize(NUM_CPUS,
                               vector<vector<BASIC_BTB_ENTRY>>(CONDITIONAL_BTB_SETS,
                                                               vector<BASIC_BTB_ENTRY>(CONDITIONAL_BTB_WAYS)));
        unconditional_btb.resize(NUM_CPUS,
                                 vector<vector<FOOTPRINT_BTB_ENTRY>>(UNCONDITIONAL_BTB_SETS,
                                                                     vector<FOOTPRINT_BTB_ENTRY>(
                                                                             UNCONDITIONAL_BTB_WAYS)));
    }

    void update_current(O3_CPU *o3_cpu, FOOTPRINT_BTB_ENTRY *entry, uint8_t type) {
        current_unconditional = entry;
        branch_type = type;
        if (current_unconditional != nullptr)
            current_unconditional->operate_prefetch(o3_cpu, branch_type != BRANCH_RETURN);
    }

    void add_footprint(BASIC_BTB_ENTRY *access_entry) {
        // It doesn't matter whether the pointer is changed later.
        if (current_unconditional != nullptr)
            current_unconditional->cache_access(access_entry, branch_type != BRANCH_RETURN);
    }
};

Shotgun shotgun;

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

uint64_t basic_btb_set_index(uint64_t ip, uint64_t total_sets) { return ((ip >> 2) % total_sets); }

template<class T>
T *basic_btb_find_entry(uint8_t cpu, uint64_t ip, vector<vector<vector<T>>> &btb) {
    uint64_t set = basic_btb_set_index(ip, btb[cpu].size());
    for (auto &a : btb[cpu][set]) {
        if (a.ip_tag == ip) {
            return &(a);
        }
    }
    return nullptr;
}

template<class T>
T *basic_btb_get_lru_entry(uint8_t cpu, uint64_t set, vector<vector<vector<T>>> &btb) {
    uint32_t lru_way = 0;
    uint64_t lru_value = btb[cpu][set][lru_way].lru;
    for (uint32_t i = 0; i < btb[cpu][set].size(); i++) {
        if (btb[cpu][set][i].lru < lru_value) {
            lru_way = i;
            lru_value = btb[cpu][set][lru_way].lru;
        }
    }

    return &(btb[cpu][set][lru_way]);
}

template<class T>
void basic_btb_update_lru(uint8_t cpu, T *btb_entry, bool is_conditional) {
    if (is_conditional) {
        btb_entry->lru = shotgun.conditional_btb_lru_counter[cpu];
        shotgun.conditional_btb_lru_counter[cpu]++;
    } else {
        btb_entry->lru = shotgun.unconditional_btb_lru_counter[cpu];
        shotgun.unconditional_btb_lru_counter[cpu]++;
    }
}

uint64_t basic_btb_indirect_hash(uint8_t cpu, uint64_t ip) {
    uint64_t hash = (ip >> 2) ^ (basic_btb_conditional_history[cpu]);
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
    std::cout << "Basic Shotgun BTB sets: " << CONDITIONAL_BTB_SETS
              << " ways: " << (int) CONDITIONAL_BTB_WAYS
              << " unconditional BTB sets: " << UNCONDITIONAL_BTB_SETS
              << " ways: " << UNCONDITIONAL_BTB_WAYS
              << " indirect buffer size: " << BASIC_BTB_INDIRECT_SIZE
              << " RAS size: " << BASIC_BTB_RAS_SIZE << std::endl;

    open_btb_record("w", true);

    // TODO: If NUM_CPU > 1, the vector would be resize multiple times. Modify it if needed.
    shotgun.init();

    shotgun.conditional_btb_lru_counter[cpu] = 0;
    shotgun.unconditional_btb_lru_counter[cpu] = 0;

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
        auto btb_entry = basic_btb_find_entry(cpu, ip, shotgun.conditional_btb);
        if (btb_entry == nullptr) {
            return std::make_pair(stream_buffer.stream_buffer_predict(ip), true);
        }
        basic_btb_update_lru(cpu, btb_entry, true);
        return std::make_pair(btb_entry->target, btb_entry->always_taken);
    } else {
        // Access U-BTB
        auto btb_entry = basic_btb_find_entry(cpu, ip, shotgun.unconditional_btb);

        if (btb_entry == nullptr) {
            // no prediction for this IP
            return std::make_pair(0, true);
        }

        basic_btb_update_lru(cpu, btb_entry, false);

        return std::make_pair(btb_entry->target, btb_entry->always_taken);
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
        // Update conditional_btb and footprint
        auto btb_entry = basic_btb_find_entry(cpu, ip, shotgun.conditional_btb);
        if (btb_entry == nullptr) {
            stream_buffer.stream_buffer_update(ip);
            if (branch_target != 0 && taken) {
                // no prediction for this entry so far, so allocate one
                auto set = basic_btb_set_index(ip, CONDITIONAL_BTB_SETS);
                auto repl_entry = basic_btb_get_lru_entry(cpu, set, shotgun.conditional_btb);
                repl_entry->ip_tag = ip;
                repl_entry->target = branch_target;
                repl_entry->always_taken = 1;
                basic_btb_update_lru(cpu, repl_entry, true);
                // Add footprint
                shotgun.add_footprint(repl_entry);
                assert(btb_conditional_record != nullptr);
                fprintf(btb_conditional_record, "%llu %llu\n", ip, con_timestamp);
            }
        } else {
            // update an existing entry
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
                assert(btb_conditional_record != nullptr);
                fprintf(btb_conditional_record, "%llu %llu\n", ip, con_timestamp);
            }
            shotgun.add_footprint(btb_entry);
        }
        con_timestamp++;
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
        auto btb_entry = basic_btb_find_entry(cpu, call_ip, shotgun.unconditional_btb);
        shotgun.update_current(this, btb_entry, branch_type);
    } else {
        // use BTB
        auto btb_entry = basic_btb_find_entry(cpu, ip, shotgun.unconditional_btb);

        if (btb_entry == nullptr) {
            if ((branch_target != 0) && taken) {
                // no prediction for this entry so far, so allocate one
                uint64_t set = basic_btb_set_index(ip, UNCONDITIONAL_BTB_SETS);
                auto repl_entry = basic_btb_get_lru_entry(cpu, set, shotgun.unconditional_btb);

                repl_entry->ip_tag = ip;
                repl_entry->target = branch_target;
                repl_entry->always_taken = 1;
                basic_btb_update_lru(cpu, repl_entry, false);

                shotgun.update_current(this, repl_entry, branch_type);

                assert(btb_unconditional_record != nullptr);
                fprintf(btb_unconditional_record, "%llu %llu\n", ip, uncon_timestamp);
            }
        } else {
            // update an existing entry
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                // Only update target on taken!!!
                btb_entry->target = branch_target;

                assert(btb_unconditional_record != nullptr);
                fprintf(btb_unconditional_record, "%llu %llu\n", ip, uncon_timestamp);
            }

            shotgun.update_current(this, btb_entry, branch_type);
        }
        uncon_timestamp++;
    }
}

void O3_CPU::prefetch_btb(uint64_t ip, uint64_t branch_target, uint8_t branch_type, bool taken, bool to_stream_buffer) {
    // TODO: Here we only prefetch for direct btb, since the hash for indirect branch may change later
    if (branch_type == BRANCH_CONDITIONAL) {
        auto btb_entry = basic_btb_find_entry(cpu, ip, shotgun.conditional_btb);

        if (btb_entry == nullptr) {
            if ((branch_target != 0) && taken) {
                // no prediction for this entry so far, so allocate one
                if (to_stream_buffer) {
                    stream_buffer.prefetch(ip, branch_target);
                } else {
                    uint64_t set = basic_btb_set_index(ip, CONDITIONAL_BTB_SETS);
                    auto repl_entry = basic_btb_get_lru_entry(cpu, set, shotgun.conditional_btb);

                    repl_entry->ip_tag = ip;
                    repl_entry->target = branch_target;
                    repl_entry->always_taken = 1;
                    basic_btb_update_lru(cpu, repl_entry, true);
                }
            }
        } else {
            basic_btb_update_lru(cpu, btb_entry, true);
            // update an existing entry
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                // Only update target on taken!!!
                btb_entry->target = branch_target;
            }
        }
    }
}


void O3_CPU::btb_final_stats() {}

