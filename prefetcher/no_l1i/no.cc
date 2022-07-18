#include "ooo_cpu.h"

void O3_CPU::l1i_prefetcher_initialize(){
    cout << "IFETCH_BUFFER_SIZE = " << IFETCH_BUFFER_SIZE << endl;
    cout << "CPU " << cpu << " L1I no prefetcher" << endl;
}

void O3_CPU::l1i_prefetcher_branch_operate(uint64_t ip, uint8_t branch_type, uint64_t predicted_branch_target,
                                           bool predicted_branch_taken, bool always_taken,
                                           uint64_t real_branch_target)
{

}

void O3_CPU::l1i_prefetcher_cache_operate(uint64_t v_addr, uint8_t cache_hit, uint8_t prefetch_hit)
{

}

void O3_CPU::l1i_prefetcher_cycle_operate()
{

}

void O3_CPU::l1i_prefetcher_cache_fill(uint64_t v_addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_v_addr)
{

}

void O3_CPU::l1i_prefetcher_final_stats()
{

}

void O3_CPU::l1i_prefetcher_resolved_branch_operate(ooo_model_instr &instr, bool decode_stage) {}
