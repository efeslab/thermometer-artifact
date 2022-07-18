
/*
 * This file implements a basic Branch Target Buffer (BTB) structure.
 * It uses a set-associative BTB to predict the targets of non-return branches,
 * and it uses a small Return Address Stack (RAS) to predict the target of
 * returns.
 * It uses SSRIP strategy to evict BTB entries.
 */

#include "ooo_cpu.h"
#include "../access_counter.h"
#include "../prefetch_stream_buffer.h"

#define BASIC_BTB_SETS 2048
#define BASIC_BTB_WAYS 4
#define BASIC_BTB_INDIRECT_SIZE 4096
#define BASIC_BTB_RAS_SIZE 32 // TODO: Different from the original value 64, need to rerun tests!
#define BASIC_BTB_CALL_INSTR_SIZE_TRACKERS 1024

#define SRRIP_COUNTER_SIZE 2
#define SRRIP_DISTANT_INTERVAL (1 << SRRIP_COUNTER_SIZE) - 1
#define SRRIP_LONG_INTERVAL (1 << SRRIP_COUNTER_SIZE) - 2

bool taken_only = false;

AccessCounter access_counter(BASIC_BTB_SETS, BASIC_BTB_WAYS);
StreamBuffer stream_buffer(32);

#define FRIENDLY_TYPE @FRIENDLY_TYPE@
#define CHANCE_UPPER @CHANCE_UPPER@

enum FriendlyType {
    ALL,
    CONDITIONAL,
    SMALL,
    CONDITIONAL_AND_SMALL
};

FriendlyType friendly_type = FRIENDLY_TYPE;
static uint64_t distance_boundary = 1 << 12;

struct BASIC_BTB_ENTRY {
    uint64_t ip_tag;
    uint64_t target;
    uint8_t always_taken;
    uint64_t rrpv;
    uint8_t accessed = 0;

    [[nodiscard]] uint64_t distance() const {
        return ip_tag > target ? ip_tag - target : target - ip_tag;
    }

    bool judge_friendly_second(uint8_t branch_type) {
        switch (friendly_type) {
            case ALL:
                return true;
            case CONDITIONAL:
                return branch_type == BRANCH_CONDITIONAL;
            case SMALL:
                return distance() < distance_boundary;
            case CONDITIONAL_AND_SMALL:
                return branch_type == BRANCH_CONDITIONAL && distance() < distance_boundary;
        }
    }

    void insert_setting(uint8_t branch_type) {
        if (judge_friendly_second(branch_type)) {
            rrpv = 0;
            accessed = 0;
        } else {
            rrpv = SRRIP_LONG_INTERVAL;
            accessed = CHANCE_UPPER;
        }
    }

    void update_rrpv() {
        if (accessed >= CHANCE_UPPER) {
            rrpv = 0;
        } else {
            rrpv = SRRIP_DISTANT_INTERVAL - CHANCE_UPPER + (++accessed);
        }
//        if (accessed)
//            rrpv = 0;
//        else {
//            rrpv = SRRIP_DISTANT_INTERVAL;
//            accessed = true;
//        }
    }
};

BASIC_BTB_ENTRY basic_btb[NUM_CPUS][BASIC_BTB_SETS][BASIC_BTB_WAYS];
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

uint64_t basic_btb_set_index(uint64_t ip) { return ((ip >> 2) & (BASIC_BTB_SETS - 1)); }

BASIC_BTB_ENTRY *basic_btb_find_entry(uint8_t cpu, uint64_t ip) {
    uint64_t set = basic_btb_set_index(ip);
    for (uint32_t i = 0; i < BASIC_BTB_WAYS; i++) {
        if (basic_btb[cpu][set][i].ip_tag == ip) {
            return &(basic_btb[cpu][set][i]);
        }
    }

    return NULL;
}

BASIC_BTB_ENTRY *basic_btb_get_srrip_entry(uint8_t cpu, uint64_t set) {
    while (true) {
        for (uint32_t i = 0; i < BASIC_BTB_WAYS; i++) {
            if (basic_btb[cpu][set][i].rrpv == SRRIP_DISTANT_INTERVAL) {
                basic_btb[cpu][set][i].rrpv = SRRIP_LONG_INTERVAL;
                if (basic_btb[cpu][set][i].ip_tag != 0)
                    access_counter.evict(basic_btb[cpu][set][i].ip_tag, cpu);
                return &(basic_btb[cpu][set][i]);
            }
        }
        for (uint32_t i = 0; i < BASIC_BTB_WAYS; i++)
            basic_btb[cpu][set][i].rrpv++;
    }
}

//void basic_btb_update_lru(uint8_t cpu, BASIC_BTB_ENTRY *btb_entry) {
//    btb_entry->rrpv = 0;
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
    std::cout << "SRRIP Second BTB sets: " << BASIC_BTB_SETS
              << " ways: " << BASIC_BTB_WAYS
              << " indirect buffer size: " << BASIC_BTB_INDIRECT_SIZE
              << " RAS size: " << BASIC_BTB_RAS_SIZE << std::endl;

    for (uint32_t i = 0; i < BASIC_BTB_SETS; i++) {
        for (uint32_t j = 0; j < BASIC_BTB_WAYS; j++) {
            basic_btb[cpu][i][j].ip_tag = 0;
            basic_btb[cpu][i][j].target = 0;
            basic_btb[cpu][i][j].always_taken = 0;
            basic_btb[cpu][i][j].rrpv = SRRIP_DISTANT_INTERVAL;
        }
    }
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
    } else {
        // use BTB for all other branches + direct calls
        auto btb_entry = basic_btb_find_entry(cpu, ip);

        if (btb_entry == NULL) {
            // no prediction for this IP
            return std::make_pair(stream_buffer.stream_buffer_predict(ip), true);
        }

        always_taken = btb_entry->always_taken;

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
        auto btb_entry = basic_btb_find_entry(cpu, ip);

        if (btb_entry == nullptr) {
            stream_buffer.stream_buffer_update(ip);
            if ((branch_target != 0) && taken) {
                // no prediction for this entry so far, so allocate one
                uint64_t set = basic_btb_set_index(ip);
                auto repl_entry = basic_btb_get_srrip_entry(cpu, set);
                access_counter.insert(ip, cpu, branch_target, branch_type);

                repl_entry->ip_tag = ip;
                repl_entry->target = branch_target;
                repl_entry->always_taken = 1;
                repl_entry->insert_setting(branch_type);
//                basic_btb_update_lru(cpu, repl_entry);
            }
        } else {
            if (taken_only) {
                if (taken) {
//                    btb_entry->rrpv = 0;
                    btb_entry->update_rrpv();
                }
            } else {
                // On re-reference update rrpv to 0
//                btb_entry->rrpv = 0;
                btb_entry->update_rrpv();
            }
            // update an existing entry
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
            }
            access_counter.access(ip, cpu);
        }
    }
}

void O3_CPU::prefetch_btb(uint64_t ip, uint64_t branch_target, uint8_t branch_type, bool taken, bool to_stream_buffer) {
    // TODO: Here we only prefetch for direct btb, since the hash for indirect branch may change later
    if (branch_type != BRANCH_RETURN &&
        branch_type != BRANCH_INDIRECT &&
        branch_type != BRANCH_INDIRECT_CALL) {
        auto btb_entry = basic_btb_find_entry(cpu, ip);

        if (btb_entry == NULL) {
            if ((branch_target != 0) && taken) {
                // no prediction for this entry so far, so allocate one
                if (to_stream_buffer) {
                    stream_buffer.prefetch(ip, branch_target);
                } else {
                    uint64_t set = basic_btb_set_index(ip);
                    auto repl_entry = basic_btb_get_srrip_entry(cpu, set);
                    access_counter.insert(ip, cpu, branch_target, branch_type);

                    repl_entry->ip_tag = ip;
                    repl_entry->target = branch_target;
                    repl_entry->always_taken = 1;
                    repl_entry->insert_setting(branch_type);
                }
            }
        } else {
            if (taken_only) {
                if (taken) {
//                    btb_entry->rrpv = 0;
                    btb_entry->update_rrpv();
                }
            } else {
                // On re-reference update rrpv to 0
//                btb_entry->rrpv = 0;
                btb_entry->update_rrpv();
            }
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
            }
            access_counter.access(ip, cpu);
        }
    }
}

void O3_CPU::btb_final_stats() {
    access_counter.print_final_stats(cpu);
}
