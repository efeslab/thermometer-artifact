//
// Created by shixinsong on 2/12/22.
//

#ifndef CHAMPSIM_PT_TWIG_PREFETCHER_H
#define CHAMPSIM_PT_TWIG_PREFETCHER_H

#include "ooo_cpu.h"
#include <vector>
#include <unordered_map>
#include <utility>
#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

namespace fs = boost::filesystem;


using std::unordered_map;
using std::vector;
using std::pair;
using std::string;
using std::cout;
using std::endl;


class TwigPrefetcher {
    unordered_map<uint64_t, vector<pair<uint64_t, uint64_t>>> prefetch_record;
    bool use_twig = false;

public:
    unordered_map<uint64_t, vector<pair<uint64_t, uint64_t>>> *
    init(string &trace_name, bool use_twig_prefetcher) {
        use_twig = use_twig_prefetcher;
        if (!use_twig) {
            return nullptr;
        }
        fs::path input_analysis_dir = "/mnt/storage/takh/git-repos/combine-all/data-analysis/input-analysis";
        auto footprint_path = input_analysis_dir / trace_name / "footprint.txt";

        std::vector<std::string> all_strings;
        read_full_file(footprint_path, all_strings);
        cout << "PGO prefetcher footprint size: " << all_strings.size() << endl;
        for (auto &line: all_strings) {
            boost::trim_if(line, boost::is_any_of("\n"));
            boost::trim_if(line, boost::is_any_of(" "));
            std::vector<std::string> parsed;
            boost::split(parsed, line, boost::is_any_of(" "), boost::token_compress_on);
            //std::cout<<parsed.size()<<' '<<j<<std::endl;
            //std::cout<<j<<' '<<line<<std::endl;
            if (parsed.size() < 3) {
                cout << "Wrong format of PGO prefetcher footprint file" << endl;
            }
            uint64_t function_start = strtoul(parsed[0].c_str(), NULL, 10);
            // uint64_t function_end = strtoul(parsed[1].c_str(), NULL, 10);
            uint64_t entry_count = strtoul(parsed[1].c_str(), NULL, 10);
//            uint64_t maximum_distance = (1 << MAX_DISTANCE_BITS) - 1;
            prefetch_record[function_start] = vector<pair<uint64_t, uint64_t>>();
            auto &all_entries = prefetch_record[function_start];
            if (2 * entry_count + 2 != parsed.size()) {
                cout << parsed.size() << endl;
                cout << entry_count << endl;
                cout << line << endl;
            }
            assert(2 * entry_count + 2 == parsed.size());
            for (int i = 0; i < entry_count; i++) {
                uint64_t pc = strtoul(parsed[2 + 2 * i].c_str(), NULL, 10);
                uint64_t target = strtoul(parsed[2 + 1 + 2 * i].c_str(), NULL, 10);
                all_entries.emplace_back(pc, target);
            }
            assert(prefetch_record[function_start].size() == entry_count);
            // TODO: Check whether we need this limitation of num of prefetched entries and contribution of prefetched entries.
//            if (ENABLE_MAX_PER_BBL) {
//                std::sort(all_entries.begin(), all_entries.end());
//                std::reverse(all_entries.begin(), all_entries.end());
//                if (entry_count > MAX_PER_BBL && MAX_PER_BBL >= 0) {
//                    entry_count = MAX_PER_BBL;
//                }
//            }
//            for (int i = 0; i < entry_count; i++) {
//                uint64_t pc = std::get<1>(all_entries[i]);
//                uint64_t target = std::get<2>(all_entries[i]);
//                uint64_t candidate_to_pc_distance = std::max(pc, function_start) - std::min(pc, function_start);
//                uint64_t pc_to_target_distance = std::max(pc, target) - std::min(pc, target);
//                uint64_t cover_count = std::get<0>(all_entries[i]);
//                uint64_t access_count = std::get<3>(all_entries[i]);
//                double prob = 1000.0 * cover_count / access_count;
//                if (prob >= FANOUT) {
//                    if (ENABLE_CONTRIBUTION) {
//                        if (candidate_to_pc_distance <= maximum_distance && pc_to_target_distance <= maximum_distance) {
//                            prefetch_footprint[function_start][pc] = target;
//                        }
//                    } else {
//                        prefetch_footprint[function_start][pc] = target;
//                    }
//                }
//            }
//            all_entries.clear();
        }
        return &prefetch_record;
    }

    static void read_full_file(fs::path &file_path, std::vector<std::string> &data_destination) {
        std::ifstream infile(file_path);
        assert(infile);
        std::string line;
        data_destination.clear();
        while (std::getline(infile, line)) {
            data_destination.push_back(line);
        }
        infile.close();
    }

    static bool filter_branch(uint8_t branch_type) {
        return branch_type == BRANCH_DIRECT_JUMP || branch_type == BRANCH_CONDITIONAL ||
               branch_type == BRANCH_DIRECT_CALL;
    }

    void prefetch(uint64_t ip, uint64_t target, uint64_t branch_type, O3_CPU *ooo_cpu) {
        if (!use_twig) {
            return;
        }
        if (target == 0) {
            return;
        }
        auto it = prefetch_record.find(ip);
        if (it != prefetch_record.end()) {
            assert(filter_branch(branch_type));
            for (auto &branch_pc_target: it->second) {
                ooo_cpu->prefetch_btb(
                        branch_pc_target.first,
                        branch_pc_target.second,
                        BRANCH_DIRECT_JUMP, // TODO: Check whether this is ok
                        true,
                        true
                );
            }
        }
    }
};


#endif //CHAMPSIM_PT_TWIG_PREFETCHER_H
