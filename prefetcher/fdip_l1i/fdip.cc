#include "ooo_cpu.h"
#include "../fdip.h"
#include <deque>
#include <unordered_map>
#include <utility>

using std::cout;
using std::endl;
using std::deque;
using std::unordered_map;
using std::pair;

extern uint8_t pt;

FDIP fdip_prefetcher;

void O3_CPU::l1i_prefetcher_initialize() {
    fdip_prefetcher.initialize(cpu);
}

void O3_CPU::l1i_prefetcher_branch_operate(uint64_t ip, uint8_t branch_type, uint64_t predicted_branch_target,
                                           bool predicted_branch_taken, bool always_taken,
                                           uint64_t real_branch_target) {
    // TODO: Add branch instructions to the record map
    // What should we do when meet a branch for the second time, and instr info changed?
    fdip_prefetcher.branch_operate(ip, branch_type, predicted_branch_target,
                                   predicted_branch_taken, always_taken,
                                   real_branch_target);
}

void O3_CPU::l1i_prefetcher_cache_operate(uint64_t v_addr, uint8_t cache_hit, uint8_t prefetch_hit) {

}

void O3_CPU::l1i_prefetcher_cycle_operate() {
    fdip_prefetcher.cycle_operate(this, pt);
}

void O3_CPU::l1i_prefetcher_cache_fill(uint64_t v_addr, uint32_t set, uint32_t way, uint8_t prefetch,
                                       uint64_t evicted_v_addr) {

}

void O3_CPU::l1i_prefetcher_final_stats() {
    fdip_prefetcher.final_stats(cpu);
}

// TODO: Add a function that is called after the branch is resolved
void O3_CPU::l1i_prefetcher_resolved_branch_operate(ooo_model_instr &instr, bool decode_stage) {
    fdip_prefetcher.resolved_branch(this, instr, decode_stage, pt);
}
