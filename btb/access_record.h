//
// Created by Shixin Song on 2021/6/24.
//

#ifndef CHAMPSIM_PT_ACCESS_RECORD_H
#define CHAMPSIM_PT_ACCESS_RECORD_H

#include "ooo_cpu.h"
#include <vector>
#include <map>
#include <fstream>
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

extern uint8_t total_btb_ways;
extern uint64_t total_btb_entries;

enum class RecordType {
    HIT,
    MISS_ONLY,
    MISS_INSERT,
    EVICT
};

enum class BTBType {
    NORMAL,
    CONDITIONAL,
    UNCONDITIONAL,
    PREDECODER,
    NANO,
    MILI,
    L1,
    L2
};

string btb_type_to_suffix(BTBType btbType) {
    if (btbType == BTBType::NORMAL) {
        return "";
    } else if (btbType == BTBType::CONDITIONAL) {
        return "_conditional";
    } else if (btbType == BTBType::UNCONDITIONAL) {
        return "_unconditional";
    } else if (btbType == BTBType::PREDECODER) {
        return "_predecoder";
    } else if (btbType == BTBType::NANO) {
        return "_nano";
    } else if (btbType == BTBType::MILI) {
        return "_mili";
    } else if (btbType == BTBType::L1) {
        return "_l1";
    } else if (btbType == BTBType::L2) {
        return "_l2";
    }
}

struct RecordEntry {
    uint64_t ip = 0;
    uint64_t target = 0;
    uint8_t branch_type = BRANCH_CONDITIONAL;
    vector<RecordType> record;

    RecordEntry() = default;

    RecordEntry(const RecordEntry &other) {
        ip = other.ip;
        target = other.target;
        branch_type = other.branch_type;
    }

    RecordEntry(uint64_t ip, uint64_t target, uint8_t branch_type) :
    ip(ip), target(target), branch_type(branch_type) {}
};

class AccessRecord {
    vector<map<uint64_t, RecordEntry>> branch_record;

public:
    BTBType btb_type;

    explicit AccessRecord(BTBType btb_type = BTBType::NORMAL) : btb_type(btb_type) {
        branch_record.resize(NUM_CPUS);
    }

    void access(uint64_t ip, uint64_t target, uint8_t branch_type, uint64_t cpu, bool hit, bool insert_on_miss) {
//        cout << "Before access" << endl;
        auto it = branch_record[cpu].find(ip);
        if (it == branch_record[cpu].end()) {
            branch_record[cpu].emplace_hint(it, ip, RecordEntry(ip, target, branch_type));
            it = branch_record[cpu].find(ip);
        }
        if (hit) {
            it->second.record.push_back(RecordType::HIT);
        } else if (insert_on_miss) {
            it->second.record.push_back(RecordType::MISS_INSERT);
        } else {
            it->second.record.push_back(RecordType::MISS_ONLY);
        }
//        cout << "After access" << endl;
    }

    void evict(uint64_t ip, uint64_t cpu) {
        auto it = branch_record[cpu].find(ip);
        assert(it != branch_record[cpu].end());
        it->second.record.push_back(RecordType::EVICT);
    }

    void print_final_stats(string &trace_name, uint64_t cpu, bool with_twig = false) {
        auto short_name = O3_CPU::find_trace_short_name(trace_name, O3_CPU::NameKind::TRAIN);
        if (with_twig) {
            short_name = "twig_" + short_name;
        }
        fs::path opt_access_record_path = ("/mnt/storage/shixins/champsim_pt/opt_access_record" + btb_type_to_suffix(btb_type));
        fs::create_directory(opt_access_record_path);
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
        fs::create_directory(opt_access_record_path / sub_dir);
        auto filename = opt_access_record_path / sub_dir / (short_name + ".csv");
        cout << "Open opt access record " << filename << endl;
        ofstream out(filename.c_str());
        assert(out.is_open());
        out << "PC,Target,Type,Access Record" << endl;
        for (auto &it : branch_record[cpu]) {
            out << std::hex << it.first;
            out << ",";
            out << std::hex << it.second.target;
            out << "," << (int) it.second.branch_type;
            for (auto a : it.second.record) {
                out << "," << (int) a;
            }
            out << endl;
        }
        out.close();
    }
};

#endif //CHAMPSIM_PT_ACCESS_RECORD_H
