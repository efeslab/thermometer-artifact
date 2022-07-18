#include "ooo_cpu.h"
#include <deque>
#include <unordered_map>
#include <utility>
#include <unordered_set>

using std::cout;
using std::endl;
using std::deque;
using std::unordered_map;
using std::pair;
using std::unordered_set;

extern uint8_t predecode_latency;
extern uint8_t pt;


struct predecode_entry {
    uint64_t ip;
    uint64_t target;
    uint8_t branch_type;
    uint8_t timer;
    bool taken;

    predecode_entry(uint64_t ip, uint64_t target, uint8_t branch_type, bool taken) :
            ip(ip),
            target(target),
            branch_type(branch_type),
            timer(0),
            taken(taken) {}
};

struct Instr {
    uint64_t actual_target = 0;
    uint64_t predict_target = 0;
    uint8_t branch_type = NOT_BRANCH;
    bool actual_taken = false;
    bool predict_taken = false;
    bool mispredicted = false;
    bool btb_miss = false;

    Instr() = default;
    Instr(const Instr &other) = default;
    Instr(uint8_t branch_type, uint64_t predicted_branch_target,
          bool predicted_branch_taken, bool always_taken,
          uint64_t real_branch_target) :
            actual_target(real_branch_target),
            predict_target(predicted_branch_target),
            branch_type(branch_type),
            actual_taken(actual_target != 0),
            predict_taken(predicted_branch_taken || always_taken),
            mispredicted(predicted_branch_target != real_branch_target) {
        if (branch_type == BRANCH_RETURN || branch_type == BRANCH_INDIRECT || branch_type == BRANCH_INDIRECT_CALL) {
            btb_miss = mispredicted || (predicted_branch_target == 0 && predict_taken);
        } else {
            btb_miss = predicted_branch_target == 0 && predict_taken;
        }
    }
};

class FDIP {
public:
    FDIP() = default;

    ~FDIP() = default;

    unordered_map<uint64_t, Instr> branch_record;

    // Only store predicted target and instr_id
    deque<pair<uint64_t, uint64_t>> FTQ;

    // Branches wait to be decoded
    deque<predecode_entry> decode_queue;

    // Structure to maintain bitmap
    unordered_map<uint64_t, vector<uint64_t>> footprint;

    // This should be smaller than instr_unique_id due to size restriction of IFETCH_BUFFER
    // Different from pc / ip
    uint64_t runahead_instr_unique_id;
    uint64_t runahead_ip;
};

FDIP fdip_prefetcher;

void O3_CPU::l1i_prefetcher_initialize() {
    cout << "CPU " << cpu << " L1I FDIP" << endl;
}

void O3_CPU::l1i_prefetcher_branch_operate(uint64_t ip, uint8_t branch_type, uint64_t predicted_branch_target,
                                           bool predicted_branch_taken, bool always_taken,
                                           uint64_t real_branch_target) {
    // TODO: Add branch instructions to the record map
    // What should we do when meet a branch for the second time, and instr info changed?
    fdip_prefetcher.branch_record[ip] = Instr(branch_type, predicted_branch_target,
                                              predicted_branch_taken, always_taken,
                                              real_branch_target);
}

void O3_CPU::l1i_prefetcher_cache_operate(uint64_t v_addr, uint8_t cache_hit, uint8_t prefetch_hit) {
    // Called on each cache load
    auto block_addr = v_addr >> LOG2_BLOCK_SIZE;
    auto it = fdip_prefetcher.footprint.find(block_addr);
    if (it == fdip_prefetcher.footprint.end()) {
        fdip_prefetcher.footprint.emplace_hint(it, block_addr, vector<uint64_t>());
        it = fdip_prefetcher.footprint.find(block_addr);
    }
    if (fdip_prefetcher.branch_record.find(v_addr) != fdip_prefetcher.branch_record.end())
        it->second.push_back(v_addr);
    if (!cache_hit) {
        unordered_set<uint64_t> prefetch_set;
        // the 3 here is because of size limit of airbtb, not sure whether it's the reason of relative poor performance
        for (uint64_t i = 0; !it->second.empty(); i++) {
            auto ip = it->second.back();
            it->second.pop_back();
            if (prefetch_set.find(ip) == prefetch_set.end()) {
                auto instr = fdip_prefetcher.branch_record.find(ip);
                if (instr != fdip_prefetcher.branch_record.end()) {
                    fdip_prefetcher.decode_queue.emplace_back(ip, instr->second.actual_target,
                                                              instr->second.branch_type, instr->second.actual_taken);
                }
                prefetch_set.insert(ip);
            }
        }
        it->second.clear();
    }
//    for (uint64_t ip = block_addr << LOG2_BLOCK_SIZE; (ip >> LOG2_BLOCK_SIZE) == block_addr; ip += 4) {
//        auto it = fdip_prefetcher.branch_record.find(ip);
//        if (it != fdip_prefetcher.branch_record.end()) {
//            // Is a branch, push to predecode queue
//            fdip_prefetcher.decode_queue.emplace_back(ip, it->second.actual_target,
//                                                      it->second.branch_type, it->second.actual_taken);
//        }
//    }
}

void O3_CPU::l1i_prefetcher_cycle_operate() {
    // Handle predecode and btb prefetch
    for (auto &a : fdip_prefetcher.decode_queue)
        a.timer++;
    while (!fdip_prefetcher.decode_queue.empty() && fdip_prefetcher.decode_queue.front().timer > predecode_latency) {
        auto &a = fdip_prefetcher.decode_queue.front();
        prefetch_btb(a.ip, a.target, a.branch_type, a.taken, true);
        fdip_prefetcher.decode_queue.pop_front();
    }
    // Carry out prefetch and push to FTQ if allowed
    int num_prefetches = 12;
    int prefetch = 0;
    while (prefetch < L1I_PQ_SIZE && prefetch < num_prefetches) {
        // Judge whether runahead_instr_unique_id is in the range of IFETCH_BUFFER
        bool find_larger = false;
        for (auto & it : IFETCH_BUFFER) {
            if (it.instr_id >= fdip_prefetcher.runahead_instr_unique_id)
                find_larger = true;
        }
        if (!find_larger) return;
        // Go ahead for prediction and prefetch
        auto it = fdip_prefetcher.branch_record.find(fdip_prefetcher.runahead_ip);
        if (it != fdip_prefetcher.branch_record.end()) {
            // Is a branch
            if (it->second.btb_miss) {
                // BTB miss
            } else if (it->second.predict_taken) {
                // Prefetch target
                if (!prefetch_code_line(it->second.predict_target)) {
                    // Prefetch queue is full
                    return;
                }
                prefetch++;
                fdip_prefetcher.FTQ.emplace_back(fdip_prefetcher.runahead_ip, fdip_prefetcher.runahead_instr_unique_id);
                fdip_prefetcher.runahead_ip = it->second.predict_target;
                fdip_prefetcher.runahead_instr_unique_id++;
                // Taken! So directly return
                return;
            }
        }
        // Not a branch or btb miss or not taken
        // Judge whether it is a new block
        uint8_t offset = pt ? 1 : 4;
        if ((fdip_prefetcher.runahead_ip >> LOG2_BLOCK_SIZE) != ((fdip_prefetcher.runahead_ip + offset) >> LOG2_BLOCK_SIZE)) {
            if (!prefetch_code_line(fdip_prefetcher.runahead_ip + offset)) {
                // Prefetch queue is full
                return;
            }
            prefetch++;
        }
        if (it != fdip_prefetcher.branch_record.end()) {
            fdip_prefetcher.FTQ.emplace_back(fdip_prefetcher.runahead_ip, fdip_prefetcher.runahead_instr_unique_id);
        }
        fdip_prefetcher.runahead_ip += offset;
        fdip_prefetcher.runahead_instr_unique_id++;
    }
}

void O3_CPU::l1i_prefetcher_cache_fill(uint64_t v_addr, uint32_t set, uint32_t way, uint8_t prefetch,
                                       uint64_t evicted_v_addr) {

}

void O3_CPU::l1i_prefetcher_final_stats() {
    cout << "CPU " << cpu << " L1I FDIP final stats" << endl;
}

void reset_ftq(ooo_model_instr &instr) {
    fdip_prefetcher.runahead_instr_unique_id = instr.instr_id + 1;
    fdip_prefetcher.runahead_ip = instr.branch_target;
    if (fdip_prefetcher.runahead_ip == 0) {
        // Branch not taken
        uint8_t offset = pt ? 1 : 4;
        fdip_prefetcher.runahead_ip = instr.ip + offset;
    }
}

// TODO: Add a function that is called after the branch is resolved
void O3_CPU::l1i_prefetcher_resolved_branch_operate(ooo_model_instr &instr, bool decode_stage) {
    if (decode_stage) {
        // Only BRANCH_DIRECT_JUMP and BRANCH_DIRECT_CALL
        auto it = std::find(fdip_prefetcher.FTQ.begin(), fdip_prefetcher.FTQ.end(), std::make_pair(instr.ip, instr.instr_id));
        if (it != fdip_prefetcher.FTQ.end()) {
            instr.pfc_finished = true;
            fdip_prefetcher.FTQ.erase(it, fdip_prefetcher.FTQ.end());
            reset_ftq(instr);
        }
        return;
    } else if (instr.pfc_finished)
        return;

    // Execute stage
    if (fdip_prefetcher.FTQ.empty()) {
        reset_ftq(instr);
    } else if (instr.ip != fdip_prefetcher.FTQ.front().first || instr.branch_mispredicted_all) {
        // Clear FTQ and reset
        fdip_prefetcher.FTQ.clear();
        reset_ftq(instr);
    } else {
        fdip_prefetcher.FTQ.pop_front();
    }
}
