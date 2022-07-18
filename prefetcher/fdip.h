//
// Created by Shixin Song on 2021/6/25.
//

#ifndef CHAMPSIM_PT_FDIP_H
#define CHAMPSIM_PT_FDIP_H

#include "ooo_cpu.h"
#include <deque>
#include <unordered_map>
#include <utility>

using std::cout;
using std::endl;
using std::deque;
using std::unordered_map;
using std::pair;

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

    // This should be smaller than instr_unique_id due to size restriction of IFETCH_BUFFER
    // Different from pc / ip
    uint64_t runahead_instr_unique_id;
    uint64_t runahead_ip;

    void initialize(uint32_t cpu) {
        cout << "IFETCH_BUFFER_SIZE = " << IFETCH_BUFFER_SIZE << endl;
        cout << "CPU " << cpu << " L1I FDIP prefetcher" << endl;
    }

    void branch_operate(uint64_t ip, uint8_t branch_type, uint64_t predicted_branch_target,
                        bool predicted_branch_taken, bool always_taken,
                        uint64_t real_branch_target) {
        branch_record[ip] = Instr(branch_type, predicted_branch_target,
                                  predicted_branch_taken, always_taken,
                                  real_branch_target);
    }

    void cycle_operate(O3_CPU *ooo_cpu, bool pt) {
//        int num_prefetches = 12;
        int prefetch = 0;
        while (prefetch < L1I_PQ_SIZE) {
            // TODO: prefetch should be < 12 here!!! Rerun simulations!!!
            // Judge whether runahead_instr_unique_id is in the range of IFETCH_BUFFER
            bool find_larger = false;
            for (auto &it : ooo_cpu->IFETCH_BUFFER) {
                if (it.instr_id >= runahead_instr_unique_id)
                    find_larger = true;
            }
            if (!find_larger) return;
            // Go ahead for prediction and prefetch
            auto it = branch_record.find(runahead_ip);
            if (it != branch_record.end()) {
                // Is a branch
                if (it->second.btb_miss) {
                    // BTB miss
                    // TODO: Check whether the way to judge BTB miss is wrong!
                } else if (it->second.predict_taken) {
                    // TODO: Find out the reason why the assert is failed.
                    if (it->second.predict_target == 0) {
                        cout << "Branch type " << (int) it->second.branch_type << endl;
                        cout << "Real target: " << it->second.actual_target << " Taken: " << (int) it->second.actual_taken << endl;
                    }
                    assert(it->second.predict_target != 0);
                    // Prefetch target
                    if (!ooo_cpu->prefetch_code_line(it->second.predict_target)) {
                        // Prefetch queue is full
                        return;
                    }
                    prefetch++;
                    FTQ.emplace_back(runahead_ip, runahead_instr_unique_id);
                    runahead_ip = it->second.predict_target;
                    runahead_instr_unique_id++;
                    // Taken! So directly return
                    return;
                }
            }
            // Not a branch or btb miss or not taken
            // Judge whether it is a new block
            uint8_t offset = pt ? 1 : 4;
            if ((runahead_ip >> LOG2_BLOCK_SIZE) != ((runahead_ip + offset) >> LOG2_BLOCK_SIZE)) {
//            if (fdip_prefetcher.runahead_ip + offset == 0) {
//                cout << "runahead_ip: " << fdip_prefetcher.runahead_ip << " offset " << offset;
//            }
                if (!ooo_cpu->prefetch_code_line(runahead_ip + offset)) {
                    // Prefetch queue is full
                    return;
                }
                prefetch++;
            }
            if (it != branch_record.end()) {
                FTQ.emplace_back(runahead_ip, runahead_instr_unique_id);
            }
            runahead_ip += offset;
            runahead_instr_unique_id++;
        }
    }

    void final_stats(uint32_t cpu) {
        cout << "CPU " << cpu << " L1I FDIP final stats" << endl;
    }

    void reset_ftq(ooo_model_instr &instr, bool pt) {
        runahead_instr_unique_id = instr.instr_id + 1;
        runahead_ip = instr.branch_target;
        if (runahead_ip == 0) {
            // Branch not taken
            uint8_t offset = pt ? 1 : 4;
            runahead_ip = instr.ip + offset;
        }
    }

    void resolved_branch(O3_CPU *ooo_cpu, ooo_model_instr &instr, bool decode_stage, bool pt)  {
        if (decode_stage) {
            // Only BRANCH_DIRECT_JUMP and BRANCH_DIRECT_CALL
            auto it = std::find(FTQ.begin(), FTQ.end(), std::make_pair(instr.ip, instr.instr_id));
            if (it != FTQ.end()) {
                instr.pfc_finished = true;
                FTQ.erase(it, FTQ.end());
                reset_ftq(instr, pt);
            }
            return;
        } else if (instr.pfc_finished)
            return;

        // Execute stage
        if (FTQ.empty()) {
            reset_ftq(instr, pt);
        } else if (instr.ip != FTQ.front().first || instr.branch_mispredicted_all) {
            // Clear FTQ and reset
            FTQ.clear();
            reset_ftq(instr, pt);
        } else {
            FTQ.pop_front();
        }
    }
};


#endif //CHAMPSIM_PT_FDIP_H
