//
// Created by shixinsong on 2/11/22.
//

#ifndef CHAMPSIM_PT_TWIG_PROFILE_H
#define CHAMPSIM_PT_TWIG_PROFILE_H

#include "ooo_cpu.h"
#include <unordered_map>
#include <vector>
#include <map>
#include <fstream>
#include <iostream>
#include <boost/filesystem.hpp>
#include <zlib.h>
namespace fs = boost::filesystem;

using std::string;
using std::cout;
using std::endl;
using std::unordered_map;

extern uint8_t total_btb_ways;
extern uint64_t total_btb_entries;


enum class BTBOperation {
    LOOKUP,
    UPDATE,
};


struct BranchEntry {
    uint64_t ip;
    uint64_t target;
    uint8_t branch_type;
    uint8_t miss_type;

    BranchEntry(uint64_t ip, uint64_t target, uint64_t predicted_target, uint8_t branch_type) :
    ip(ip), target(target), branch_type(branch_type) {
        // Note: only care about actual taken branch
        if (predicted_target == 0) {
            // miss
            miss_type = 1;
        } else if (predicted_target != target) {
            // wrong
            miss_type = 2;
        } else {
            // hit
            miss_type = 0;
        }
    }

    [[nodiscard]] uint8_t get_miss_or_hit() const {
        if (miss_type == 0)
            return 0;
        else
            return 1;
    }

    [[nodiscard]] string get_branch_type_name() const {
        if (branch_type == BRANCH_DIRECT_JUMP) {
            return "CF_BR";
        } else if (branch_type == BRANCH_CONDITIONAL) {
            return "CF_CBR";
        } else if (branch_type == BRANCH_DIRECT_CALL) {
            return "CF_CALL";
        } else {
            cout << "Invalid branch type in BTB" << endl;
            assert(0);
        }
    }

    void write_log(BTBOperation btb_operation, uint64_t cycle_count, gzFile output_file) {
        if (btb_operation == BTBOperation::LOOKUP) {
            gzprintf(
                    output_file,
                    "BTB-Lookup: %llu %llu %s %llu %llu %d\n",
                    cycle_count, 0, get_branch_type_name().c_str(), ip, target, miss_type
                    );
        } else {
            gzprintf(
                    output_file,
                    "BTB-Update: %llu %llu %s %llu %llu %d\n",
                    cycle_count, 0, get_branch_type_name().c_str(), ip, target, get_miss_or_hit()
            );
        }
    }
};


class TwigRecord {
    // TODO: only consider single cpu
    unordered_map<uint64_t, BranchEntry> lookup_history;
    gzFile output_trace = nullptr;
    bool gen = false;

public:
    ~TwigRecord() {
        cout << "Num of updated instructions: " << lookup_history.size() << endl;
        if (output_trace != nullptr) {
            gzclose(output_trace);
        }
    }

    void init(string &short_name, bool generate_twig_trace) {
        gen = generate_twig_trace;
        if (!gen) {
            return;
        }
        fs::path twig_record_dir = ("/mnt/storage/shixins/champsim_pt/twig_record");
//        fs::path twig_record_dir = ("pt_traces/twig_record");
        fs::create_directory(twig_record_dir);
        string sub_dir = "way" + std::to_string(total_btb_ways);
        if (total_btb_entries != 8 && total_btb_entries != 8192) {
            if (total_btb_entries % 1024 == 0) {
                sub_dir += ("_" + std::to_string(total_btb_entries / 1024) + "K");
            } else {
                sub_dir += ("_" + std::to_string(total_btb_entries) + "K");
            }
        }
        if (IFETCH_BUFFER_SIZE != 192) {
            sub_dir += ("_fdip" + std::to_string(IFETCH_BUFFER_SIZE));
        }
        fs::create_directory(twig_record_dir / sub_dir);
        auto filename = twig_record_dir / sub_dir / (short_name + ".gz");
        output_trace = gzopen(filename.c_str(), "wb");
        if (output_trace == nullptr) {
            cout << "Cannot open twig record file to write " << output_trace << endl;
            assert(0);
        }
    }

    static bool filter_branch(uint8_t branch_type) {
        return branch_type == BRANCH_DIRECT_JUMP || branch_type == BRANCH_CONDITIONAL || branch_type == BRANCH_DIRECT_CALL;
    }


    void lookup(uint64_t instr_id, uint64_t ip, uint64_t target, uint64_t predicted_target, uint8_t branch_type, uint64_t cycle_count) {
        if (!gen) {
            return;
        }
        if (target == 0 || !filter_branch(branch_type)) {
            return;
        }
        auto new_branch = BranchEntry(ip, target, predicted_target, branch_type);
        new_branch.write_log(BTBOperation::LOOKUP, cycle_count, output_trace);
        lookup_history.emplace(instr_id, new_branch);
    }

    void update(uint64_t instr_id, uint64_t ip, uint64_t target, uint8_t branch_type, uint64_t cycle_count) {
        if (!gen) {
            return;
        }
        if (target == 0 || !filter_branch(branch_type)) {
            return;
        }
        auto it = lookup_history.find(instr_id);
        assert(it != lookup_history.end());
        assert(ip == it->second.ip);
        it->second.write_log(BTBOperation::UPDATE, cycle_count, output_trace);
        lookup_history.erase(it);
    }
};






#endif //CHAMPSIM_PT_TWIG_PROFILE_H
