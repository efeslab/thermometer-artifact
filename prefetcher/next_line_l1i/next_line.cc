#include "ooo_cpu.h"
#include "../fdip.h"

extern uint8_t pt;

FDIP fdip_prefetcher;


void O3_CPU::l1i_prefetcher_initialize() {
    cout << "IFETCH_BUFFER_SIZE = " << IFETCH_BUFFER_SIZE << endl;
    cout << "CPU " << cpu << " L1I next line prefetcher" << endl;
    if (IFETCH_BUFFER_SIZE == 192)
        fdip_prefetcher.initialize(cpu);
}

void O3_CPU::l1i_prefetcher_branch_operate(uint64_t ip, uint8_t branch_type, uint64_t predicted_branch_target,
                                           bool predicted_branch_taken, bool always_taken,
                                           uint64_t real_branch_target) {
    if (IFETCH_BUFFER_SIZE == 192) {
        fdip_prefetcher.branch_operate(ip, branch_type, predicted_branch_target,
                                       predicted_branch_taken, always_taken,
                                       real_branch_target);
    }
}

void O3_CPU::l1i_prefetcher_cache_operate(uint64_t v_addr, uint8_t cache_hit, uint8_t prefetch_hit) {
    //cout << "access v_addr: 0x" << hex << v_addr << dec << endl;

    if ((cache_hit == 0) && (L1I.get_occupancy(0, 0) < (L1I.get_size(0, 0) >> 1))) {
        uint64_t pf_addr = v_addr + (1 << LOG2_BLOCK_SIZE);
        prefetch_code_line(pf_addr);
    }
}

void O3_CPU::l1i_prefetcher_cycle_operate() {
    if (IFETCH_BUFFER_SIZE == 192)
        fdip_prefetcher.cycle_operate(this, pt);
}

void O3_CPU::l1i_prefetcher_cache_fill(uint64_t v_addr, uint32_t set, uint32_t way, uint8_t prefetch,
                                       uint64_t evicted_v_addr) {
    //cout << hex << "fill: 0x" << v_addr << dec << " " << set << " " << way << " " << (uint32_t)prefetch << " " << hex << "evict: 0x" << evicted_v_addr << dec << endl;
}

void O3_CPU::l1i_prefetcher_final_stats() {
    cout << "CPU " << cpu << " L1I next line prefetcher final stats" << endl;
    if (IFETCH_BUFFER_SIZE == 192)
        fdip_prefetcher.final_stats(cpu);

}

void O3_CPU::l1i_prefetcher_resolved_branch_operate(ooo_model_instr &instr, bool decode_stage) {
    if (IFETCH_BUFFER_SIZE == 192) {
        fdip_prefetcher.resolved_branch(this, instr, decode_stage, pt);
    }
}
