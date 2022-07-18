
/*
 * This file implements a hot warm cold Branch Target Buffer (BTB) structure.
 * It uses a set-associative BTB to predict the targets of non-return branches,
 * and it uses a small Return Address Stack (RAS) to predict the target of
 * returns.
 * It uses Hot warm cold strategy to evict BTB entries.
 */

#include "ooo_cpu.h"
#include "../access_counter.h"
#include "../access_record.h"
#include "../accuracy.h"
#include <vector>
#include <unordered_map>
#include <cstdlib>
#include <sstream>
#include <utility>
#include <queue>
#include <set>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include "../prefetch_stream_buffer.h"

namespace fs = boost::filesystem;

using std::vector;
using std::unordered_map;
using std::pair;
using std::priority_queue;
using std::set;

extern uint8_t total_btb_ways;
extern uint64_t total_btb_entries; // 1K, 2K...
extern uint8_t train_total_btb_ways;
extern uint64_t train_total_btb_entries;
extern bool use_twig_prefetcher;

#define BASIC_BTB_SETS (total_btb_entries / total_btb_ways)
#define BASIC_BTB_WAYS total_btb_ways
#define BASIC_BTB_INDIRECT_SIZE 4096
#define BASIC_BTB_RAS_SIZE 32 // TODO: Different from the original value 64, need to rerun tests!
#define BASIC_BTB_CALL_INSTR_SIZE_TRACKERS 1024

#define SRRIP_COUNTER_SIZE 2
#define SRRIP_DISTANT_INTERVAL (1 << SRRIP_COUNTER_SIZE) - 1
#define SRRIP_LONG_INTERVAL (1 << SRRIP_COUNTER_SIZE) - 2

bool taken_only = false;

#define BTB_HOT_LOWER_BOUND @BTB_HOT_LOWER_BOUND@
#define BTB_COLD_UPPER_BOUND @BTB_COLD_UPPER_BOUND@
#define BTB_WARM_SPLIT @BTB_WARM_SPLIT@
#define BTB_IGNORE_ALL_HOT @BTB_IGNORE_ALL_HOT@

#define BTB_SORT_HIT_ACCESS @BTB_SORT_HIT_ACCESS@
#define BTB_USE_LRU @BTB_USE_LRU@
#define BTB_CONSIDER_WARM_TAKEN @BTB_CONSIDER_WARM_TAKEN@
#define BTB_CONSIDER_HOT_TAKEN @BTB_CONSIDER_HOT_TAKEN@
#define BTB_CONSIDER_KEEP @BTB_CONSIDER_KEEP@
#define BTB_CONSIDER_TAKEN_HISTORY @BTB_CONSIDER_TAKEN_HISTORY@

#define BTB_CURR_HOTTER @BTB_CURR_HOTTER@

bool ignore_all_hot = BTB_IGNORE_ALL_HOT;

bool sort_hit_access = BTB_SORT_HIT_ACCESS;
bool use_lru = BTB_USE_LRU;
bool consider_warm_taken = BTB_CONSIDER_WARM_TAKEN;
bool consider_hot_taken = BTB_CONSIDER_HOT_TAKEN;
bool consider_keep = BTB_CONSIDER_KEEP;
bool consider_taken_history = BTB_CONSIDER_TAKEN_HISTORY;

bool curr_hotter = BTB_CURR_HOTTER;

//AccessCounter access_counter(BASIC_BTB_SETS, BASIC_BTB_WAYS);
//CoverageAccuracy coverage_accuracy;
StreamBuffer stream_buffer(32);

enum class OptAccessRecordType {
    LONG,
    SHORT
};

struct BASIC_BTB_ENTRY {
    uint64_t ip_tag;
    uint64_t target;
    uint8_t always_taken = 1;
    uint64_t rrpv = SRRIP_LONG_INTERVAL;
    uint64_t lru = 0;
    deque<bool> taken_history;

    BASIC_BTB_ENTRY(uint64_t ip, uint64_t target) : ip_tag(ip), target(target) {
        taken_history.push_back(true);
    }

    void add_to_taken_history(bool taken) {
        if (taken_history.size() >= 8) {
            taken_history.pop_front();
        }
        assert(taken_history.size() < 8);
        taken_history.push_back(taken);
    }

    int get_loop_judge() {
        // Return -1 if the last access is taken. Else, return num of takens before the last not taken
        if (taken_history.back()) return -1;
        int taken_count = 0;
        for (uint64_t i = 1; i < taken_history.size(); i++) {
            if (taken_history[taken_history.size() - 1 - i]) return taken_count;
            taken_count++;
        }
        return taken_count;
    }
};

enum class CacheType {
    HOT,
    WARM,
    COLD
};

class HotWarmCold {
    uint64_t total_sets;
    uint64_t total_ways;
    double hot_lower;
    double cold_upper;
    vector<double> category_boundary;
    vector<vector<unordered_map<uint64_t, BASIC_BTB_ENTRY>>> btb;
    unordered_map<uint64_t, double> branch_record;

    set<uint64_t> not_trained_branch_record;

    vector<uint64_t> general_evict_counter;
    vector<uint64_t> general_total_counter;

    uint64_t evict_counter[4] = {0};
    uint64_t total_counter[4] = {0};
    uint64_t hot_not_taken[2] = {0};

//    struct EvictCompareEntry {
//        vector<uint8_t> candidate_type; // 0-3 is for old branches in btb, and 4 is for curr branch
//        vector<bool> candidate_taken;
//        uint32_t hwc_choice; // 0-4: index of victim chosen by hwc
//        uint32_t opt_choice; // 0-4: index of victim chosen by opt
//
//        static uint32_t judge_index(vector<uint64_t> &candidate_ip, uint64_t target_ip) {
//            for (uint32_t i = 0; i < candidate_ip.size(); i++) {
//                if (target_ip == candidate_ip[i])
//                    return i;
//            }
//            assert(0);
//        }
//
//        EvictCompareEntry(unordered_map<uint64_t, BASIC_BTB_ENTRY> &btb_set,
//                          uint64_t curr_ip, uint64_t hwc_ip, uint64_t opt_ip,
//                          HotWarmCold *hwc, O3_CPU *ooo_cpu) {
//            vector<uint64_t> candidate_ip;
//            for (auto &it : btb_set) {
//                candidate_ip.push_back(it.first);
//            }
//            candidate_ip.push_back(curr_ip);
//            hwc_choice = judge_index(candidate_ip, hwc_ip);
//            opt_choice = judge_index(candidate_ip, opt_ip);
//            for (auto a : candidate_ip) {
//                candidate_type.push_back(hwc->judge_general_type(a));
//                candidate_taken.push_back(ooo_cpu->predict_branch(a, 0, 0, 0));
//            }
//        }
//    };

//    vector<EvictCompareEntry> evict_compare_record;

public:
    HotWarmCold(uint64_t sets, uint64_t ways, double hot_lower, double cold_upper, double warm_split) :
            total_sets(sets),
            total_ways(ways),
            hot_lower(hot_lower),
            cold_upper(cold_upper) {
        if (warm_split == 0 || warm_split == cold_upper) {
            category_boundary = {cold_upper, hot_lower};
        } else {
            category_boundary = {cold_upper, warm_split, hot_lower};
        }
        general_evict_counter.resize(num_category(), 0);
        general_total_counter.resize(num_category(), 0);
    }

//    friend ostream &operator<<(ostream &os, const EvictCompareEntry &entry);

    void init(uint64_t sets, uint64_t ways) {
        total_sets = sets;
        total_ways = ways;
        btb.resize(NUM_CPUS, vector<unordered_map<uint64_t, BASIC_BTB_ENTRY>>(sets));
//        basic_opt.init(sets, ways);
//        basic_opt.get_btb_pointer(&btb);
    }

    uint64_t num_category() {
        return category_boundary.size() + 1;
    }

    void update_lru(uint64_t ip, uint64_t cpu) {
        auto set = get_set_index(ip);
        uint64_t max = 0;
        for (auto &it: btb[cpu][set]) {
            if (max < it.second.lru) {
                max = it.second.lru;
            }
        }
        auto find_it = btb[cpu][set].find(ip);
        assert(find_it != btb[cpu][set].end());
        find_it->second.lru = max + 1;
    }

    uint64_t find_lru_min(uint64_t set, uint64_t cpu, vector<uint64_t> &candidates) {
        // candidates may contain the current ip, which is not in btb.
        // Use random number to determine whether choosing the current ip, if not, then use lru.
        auto random_index = rand() % candidates.size();
        auto random_it = btb[cpu][set].find(candidates[random_index]);
        if (random_it == btb[cpu][set].end()) {
            // Randomly choose current ip as the victim.
            return candidates[random_index];
        }
        uint64_t min = std::numeric_limits<uint64_t>::max();
        uint64_t ip_min = 0;
        for (auto &candidate_ip: candidates) {
            auto it = btb[cpu][set].find(candidate_ip);
            if (it == btb[cpu][set].end())
                continue;
            if (min > it->second.lru) {
                min = it->second.lru;
                ip_min = it->first;
            }
        }
        return ip_min;
    }

    uint64_t hex_string_to_u64(string tmp) {
        if (tmp.rfind("0x", 0) == 0) {
            //
        } else {
            tmp = "0x" + tmp;
        }
        istringstream iss(tmp);
        uint64_t value;
        iss >> std::hex >> value;
        return value;
    }

    void add_to_record(string &line, OptAccessRecordType access_record_type) {
        if (access_record_type == OptAccessRecordType::LONG) {
            istringstream line_stream(line);
            uint64_t ip, tmp, hit = 0, miss = 0;
            char trash;
            line_stream >> hex >> ip >> trash;
            assert(trash == ',');
            // Read target
            line_stream >> hex >> tmp >> trash;
            assert(trash == ',');
            // Read type
            line_stream >> hex >> tmp;
            while (line_stream >> trash) {
                assert(trash == ',');
                if (!(line_stream >> tmp))
                    assert(0);
                if (tmp == (uint64_t) RecordType::HIT)
                    hit++;
                else if (tmp == (uint64_t) RecordType::MISS_ONLY || tmp == (uint64_t) RecordType::MISS_INSERT)
                    miss++;
            }
            auto percentage = (double) hit / ((double) hit + (double) miss);
            branch_record.emplace(ip, percentage);
        } else {
            boost::trim_if(line, boost::is_any_of("\n"));
            boost::trim_if(line, boost::is_any_of(" "));
            vector<string> parsed;
            boost::split(parsed, line, boost::is_any_of(","), boost::token_compress_on);
            auto ip = hex_string_to_u64(parsed[0]);
            auto hit = hex_string_to_u64(parsed[1]);
            auto taken = hex_string_to_u64(parsed[2]);
            auto percentage = (double) hit / (double) taken;
            branch_record.emplace(ip, percentage);
        }
    }

    void init_record(string &trace_name, bool with_twig = false) {
        // TODO: Move original opt_access_record to the sub dir.
        auto short_name = O3_CPU::find_trace_short_name(trace_name, O3_CPU::NameKind::TRAIN);
        if (with_twig) {
            short_name = "twig_" + short_name;
        }
        fs::path opt_access_record_path = with_twig ?
                                          "/mnt/storage/shixins/champsim_pt/opt_access_record_predecoder" :
                                          "/mnt/storage/shixins/champsim_pt/opt_access_record";
        string sub_dir = "way" + std::to_string(train_total_btb_ways);
        if (train_total_btb_entries != 8 && train_total_btb_entries != 8192) {
            if (train_total_btb_entries % 1024 == 0) {
                sub_dir += ("_" + std::to_string(train_total_btb_entries / 1024) + "K");
            } else {
                sub_dir += ("_" + std::to_string(train_total_btb_entries) + "K");
            }
        }
        if (IFETCH_BUFFER_SIZE != 192) {
            sub_dir += ("_fdip" + std::to_string(IFETCH_BUFFER_SIZE));
        }
        auto filename = opt_access_record_path / sub_dir / (short_name + ".csv");
        cout << "Init opt access record (hit access) " << filename << endl;
        ifstream in(filename.c_str());
        assert(in);
        string line;
        getline(in, line);
        OptAccessRecordType access_record_type = OptAccessRecordType::LONG;
        if (line == "PC,Hit,Taken") {
            cout << "Read short access_record" << endl;
            access_record_type = OptAccessRecordType::SHORT;
        }
        while (getline(in, line))
            add_to_record(line, access_record_type);
        in.close();
        cout << "Finish init opt access record (hit access) " << filename << endl;
    }

    double get_hit_access_ratio(uint64_t ip) {
        auto it = branch_record.find(ip);
        if (it == branch_record.end()) {
            not_trained_branch_record.insert(ip);
//            auto x = ((double) rand()) / (double) RAND_MAX;
//            if (x <= 0.25)
//                return 0.0;
//            else if (x <= 0.3)
//                return 0.6;
//            else
//                return 0.0;
//            return -1.0; // Don't care
//            return 0.9;
//            return 0.6;
//            return 0.0;
            return ((double) rand()) / (double) RAND_MAX;
        }
//        assert(it != branch_record.end());
        return it->second;
    }

    CacheType judge_hot_warm_cold(uint64_t ip) {
        assert(ip != 0);
        auto it = branch_record.find(ip);
        assert(it != branch_record.end());
        if (it->second >= hot_lower) // TODO: Change here to > to make it the same as my modified code.
            return CacheType::HOT;
        else if (it->second <= cold_upper)
            return CacheType::COLD;
        else
            return CacheType::WARM;
    }

    uint8_t judge_general_type(uint64_t ip) {
        assert(ip != 0);
        auto hit_access = get_hit_access_ratio(ip);
        for (uint64_t i = 0; i < category_boundary.size(); i++) {
            if (hit_access <= category_boundary[i]) {
                return i;
            }
        }
        return category_boundary.size();
    }

    uint64_t get_set_index(uint64_t ip) {
        return ((ip >> 2) % total_sets);
    }

    BASIC_BTB_ENTRY *find_btb_entry(uint64_t ip, uint64_t cpu) {
        auto set = get_set_index(ip);
        auto it = btb[cpu][set].find(ip);
        if (it == btb[cpu][set].end()) {
            return nullptr;
        } else {
            it->second.rrpv = 0;
            return &(it->second);
        }
    }

    uint64_t sort_choose_victim(uint64_t ip, uint64_t cpu, uint64_t set, O3_CPU *ooo_cpu) {
        // First is hit access ratio, second is ip.
        double min_hit_access = 1.0;
        uint64_t min_ip;
        if (consider_keep) {
            min_hit_access = get_hit_access_ratio(ip);
            min_ip = ip;
        }
        for (auto &it: btb[cpu][set]) {
            auto hit_access = get_hit_access_ratio(it.first);
            if (hit_access <= min_hit_access) {
                min_hit_access = hit_access;
                min_ip = it.first;
            }
        }
        return min_ip;
    }

    bool add_to_candidate(uint64_t ip, vector<vector<uint64_t>> &candidate, bool curr_ip = false) {
        auto hit_access = get_hit_access_ratio(ip);
        if (hit_access < 0)
            return false; // Don't care
        for (uint64_t i = 0; i < category_boundary.size(); i++) {
            if (hit_access <= category_boundary[i]) {
                if (curr_ip && i + 1 == category_boundary.size()) {
                    // warm for current ip
                    candidate[i + 1].push_back(ip);
                    return true;
                }
                candidate[i].push_back(ip);
                return true;
            }
        }
        if (curr_ip)
            return true;
        candidate[category_boundary.size()].push_back(ip);
        return true;
    }

    void add_to_candidate(uint64_t ip, O3_CPU *ooo_cpu,
                          vector<uint64_t> &cold_candidate,
                          vector<uint64_t> &warm_not_taken_candidate,
                          vector<uint64_t> &warm_taken_candidate,
                          vector<uint64_t> &hot_candidate) {
        auto cache_type = judge_hot_warm_cold(ip);
        if (cache_type == CacheType::COLD) {
            cold_candidate.push_back(ip);
        } else if (cache_type == CacheType::WARM) {
            // TODO: For tage-sc-l the last three parameters is useless, if using other predictor, modify here.
            auto taken = ooo_cpu->predict_branch(ip, 0, 0, 0);
            if (consider_warm_taken && taken == 0)
                warm_not_taken_candidate.push_back(ip);
            else
                warm_taken_candidate.push_back(ip);
        } else {
            hot_candidate.push_back(ip);
        }
    }

    uint64_t choose_taken_history(vector<uint64_t> &candidate, uint64_t cpu, uint64_t set) {
        // Return 0 if no victim chosen by this method.
        if (!consider_taken_history) return 0;
        int min_length = 8;
        uint64_t victim_ip = 0;
        for (auto candidate_ip: candidate) {
            auto it = btb[cpu][set].find(candidate_ip);
            if (it == btb[cpu][set].end()) {
                // Current ip may be also in the candidate.
                continue;
            }
            auto loop_judge = it->second.get_loop_judge();
            if (loop_judge >= 0) {
                if (loop_judge < min_length) {
                    min_length = loop_judge;
                    victim_ip = candidate_ip;
                }
            }
        }
        return victim_ip;
    }

    uint64_t choose_victim_one_category(uint64_t cpu, uint64_t set, vector<uint64_t> &one_category, O3_CPU *ooo_cpu) {
        // TODO: Add a parameter judge_taken here to consider whether considering predicted taken or not.
        // If ooo_cpu != nullptr, predict whether taken or not and then determine the victim.
        // If ooo_cpu == nullptr, then we don't consider predict taken here.
        vector<uint64_t> not_taken_candidates;
        if (ooo_cpu != nullptr) {
            for (auto candidate: one_category) {
                if (ooo_cpu->predict_branch(candidate, 0, 0, 0) == 0) {
                    not_taken_candidates.push_back(candidate);
                }
            }
        }
        auto &final_candidates = not_taken_candidates.empty() ? one_category : not_taken_candidates;

        if (use_lru) {
            return find_lru_min(set, cpu, final_candidates);
        }
        auto taken_history_choice = choose_taken_history(final_candidates, cpu, set);
        if (taken_history_choice != 0) return taken_history_choice;
        return final_candidates[rand() % final_candidates.size()];
    }

    bool judge_consider_taken(uint64_t category_index) {
        if (consider_hot_taken && category_index == num_category() - 1)
            return true;
        if (consider_warm_taken && category_index > 0 && category_index < num_category() - 1)
            return true;
        return false;
    }

    uint64_t choose_victim_general(uint64_t ip, uint64_t cpu, uint64_t set, O3_CPU *ooo_cpu) {
        vector<vector<uint64_t>> candidate(num_category());
        vector<uint64_t> dont_care;
        for (auto &it: btb[cpu][set]) {
            if (!add_to_candidate(it.first, candidate)) {
                dont_care.push_back(it.first);
            }
        }
        if (consider_keep) {
            if (!add_to_candidate(ip, candidate, curr_hotter)) {
                dont_care.push_back(ip);
            }
        }
        for (uint64_t i = 0; i < candidate.size(); i++) {
            if (!candidate[i].empty()) {
                for (auto dont_care_ip: dont_care) {
                    assert(std::find(candidate[i].begin(), candidate[i].end(), dont_care_ip) == candidate[i].end());
                    candidate[i].push_back(dont_care_ip);
                }
                general_evict_counter[i]++;
                general_total_counter[i] += candidate[i].size();
                // Judge whether to predict taken with the last parameter.
                if (judge_consider_taken(i))
                    return choose_victim_one_category(cpu, set, candidate[i], ooo_cpu);
                else
                    return choose_victim_one_category(cpu, set, candidate[i], nullptr);
            }
        }
        assert(!dont_care.empty());
        // Judge whether to predict taken with the last parameter.
        return choose_victim_one_category(cpu, set, dont_care, nullptr);
    }

    uint64_t choose_victim(uint64_t ip, uint64_t cpu, uint64_t set, O3_CPU *ooo_cpu) {
        vector<uint64_t> cold_candidate;
        vector<uint64_t> warm_not_taken_candidate;
        vector<uint64_t> warm_taken_candidate;
        vector<uint64_t> hot_candidate;
        for (auto &it: btb[cpu][set]) {
            add_to_candidate(it.first, ooo_cpu,
                             cold_candidate, warm_not_taken_candidate, warm_taken_candidate, hot_candidate);
        }
        if (consider_keep) {
            add_to_candidate(ip, ooo_cpu,
                             cold_candidate, warm_not_taken_candidate, warm_taken_candidate, hot_candidate);
        }
        if (!cold_candidate.empty()) {
            evict_counter[0]++;
            total_counter[0] += cold_candidate.size();
            if (use_lru) {
                return find_lru_min(set, cpu, cold_candidate);
            }
            auto taken_history_choice = choose_taken_history(cold_candidate, cpu, set);
            if (taken_history_choice != 0) return taken_history_choice;
            return cold_candidate[rand() % cold_candidate.size()];
        } else if (!warm_not_taken_candidate.empty()) {
            evict_counter[1]++;
            total_counter[1] += warm_not_taken_candidate.size();
            if (use_lru) {
                return find_lru_min(set, cpu, warm_not_taken_candidate);
            }
            auto taken_history_choice = choose_taken_history(warm_not_taken_candidate, cpu, set);
            if (taken_history_choice != 0) return taken_history_choice;
            return warm_not_taken_candidate[rand() % warm_not_taken_candidate.size()];
        } else if (!warm_taken_candidate.empty()) {
            evict_counter[2]++;
            total_counter[2] += warm_taken_candidate.size();
            if (use_lru) {
                return find_lru_min(set, cpu, warm_taken_candidate);
            }
            auto taken_history_choice = choose_taken_history(warm_taken_candidate, cpu, set);
            if (taken_history_choice != 0) return taken_history_choice;
            return warm_taken_candidate[rand() % warm_taken_candidate.size()];
        } else if (ignore_all_hot) {
            evict_counter[3]++;
            return ip;
        } else {
            evict_counter[3]++;
            total_counter[3] += hot_candidate.size();
            if (use_lru) {
                return find_lru_min(set, cpu, hot_candidate);
            }
            // Hot not taken
            vector<uint64_t> not_taken;
            for (auto a: hot_candidate) {
                if (ooo_cpu->predict_branch(a, 0, 0, 0) == 0)
                    not_taken.push_back(a);
            }
            if (consider_hot_taken && !not_taken.empty()) {
                hot_not_taken[0]++;
                hot_not_taken[1] += not_taken.size();
                auto taken_history_choice = choose_taken_history(not_taken, cpu, set);
                if (taken_history_choice != 0) return taken_history_choice;
                return not_taken[rand() % not_taken.size()];
            }
            auto taken_history_choice = choose_taken_history(hot_candidate, cpu, set);
            if (taken_history_choice != 0) return taken_history_choice;
            return hot_candidate[rand() % hot_candidate.size()];
        }
    }

//    void add_to_opt_compare_record(uint64_t ip, uint64_t cpu, uint64_t set, uint64_t hwc_victim, O3_CPU *ooo_cpu) {
//        auto opt_victim = basic_opt.find_victim(ip, cpu);
//        evict_compare_record.emplace_back(btb[cpu][set], ip, hwc_victim, opt_victim, this, ooo_cpu);
//    }

    void insert(uint64_t ip, uint64_t target, uint8_t branch_type, uint64_t cpu, O3_CPU *ooo_cpu) {
        auto set = get_set_index(ip);
        assert(set < btb[cpu].size());
        auto it = btb[cpu][set].find(ip);
        assert(it == btb[cpu][set].end());
        if (btb[cpu][set].size() >= total_ways) {
            // Evict
            uint64_t victim_ip;
            if (sort_hit_access) {
                victim_ip = sort_choose_victim(ip, cpu, set, ooo_cpu);
            } else {
                victim_ip = choose_victim_general(ip, cpu, set, ooo_cpu);
            }
//            add_to_opt_compare_record(ip, cpu, set, victim_ip, ooo_cpu);
            assert(victim_ip != 0);
//            coverage_accuracy.get_reuse_distance(victim_ip, basic_opt.timestamp - 1, victim_ip == ip);
            if (victim_ip == ip) return;
            btb[cpu][set].erase(victim_ip);
//            access_counter.evict(victim_ip, cpu);
        }
        assert(btb[cpu][set].size() < total_ways);
        btb[cpu][set].emplace(ip, BASIC_BTB_ENTRY(ip, target));
        update_lru(ip, cpu);
//        access_counter.insert(ip, cpu, target, branch_type);
    }

//    void print_final_stats(string &trace_name, string &program_name) {
//        for (uint64_t i = 0; i < general_evict_counter.size(); i++) {
//            cout << "Evict the " << i << "th category: " << general_evict_counter[i]
//                 << "\t Average candidate: " << (double) general_total_counter[i] / (double) general_evict_counter[i]
//                 << endl;
//        }
//
//        // Print compare with opt
//        cout << "Start print compare with opt with size = " << evict_compare_record.size() << endl;
//        string dir = "/mnt/storage/shixins/champsim_pt/hwc_opt_compare_raw_data/hwc_opt_" + program_name +
//                     std::to_string(total_btb_ways);
//        fs::create_directory(dir);
//        auto short_name = O3_CPU::find_trace_short_name(trace_name, O3_CPU::NameKind::TRACE_TRAIN);
//        string filename = dir + "/" + short_name + ".csv";
//        cout << "Output hwc opt compare to " << filename << endl;
//        ofstream out(filename.c_str());
//        out << "0,0,1,1,2,2,3,3,4,4,hwc choice,opt choice" << endl;
//        for (auto &it : evict_compare_record) {
//            out << it << endl;
//        }
//        out.close();
//        cout << "End print compare with opt" << endl;
//        fs::path not_trained_dir = "/mnt/storage/shixins/champsim_pt/input_generalization/not_trained_branches";
//        fs::create_directory(not_trained_dir);
//        auto program_not_trained_dir = not_trained_dir / (program_name + std::to_string(total_btb_ways));
//        fs::create_directory(program_not_trained_dir);
//        auto not_trained_file = program_not_trained_dir / (short_name + ".txt");
//        cout << "Print not trained branches to " << not_trained_file << " with total num: "
//             << not_trained_branch_record.size() << endl;
//        ofstream not_trained_out(not_trained_file.c_str());
//        for (auto a : not_trained_branch_record) {
//            not_trained_out << a << endl;
//        }
//        not_trained_out.close();
//        cout << "End print not trained branches" << endl;
//    }
};

//ostream &operator<<(ostream &os, const HotWarmCold::EvictCompareEntry &entry) {
//    if (entry.candidate_type.size() != entry.candidate_taken.size()) {
//        cout << "Wrong size" << endl;
//    }
//    assert(entry.candidate_type.size() == entry.candidate_taken.size());
//    for (uint32_t i = 0; i < entry.candidate_type.size(); i++) {
//        os << (int) entry.candidate_type[i] << "," << (int) entry.candidate_taken[i] << ",";
//    }
//    os << (int) entry.hwc_choice << "," << (int) entry.opt_choice;
//    return os;
//}

HotWarmCold hot_warm_cold_btb(BASIC_BTB_SETS,
                              BASIC_BTB_WAYS, BTB_HOT_LOWER_BOUND, BTB_COLD_UPPER_BOUND, BTB_WARM_SPLIT);

//BASIC_BTB_ENTRY basic_btb[NUM_CPUS][BASIC_BTB_SETS][BASIC_BTB_WAYS];
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

uint64_t basic_btb_set_index(uint64_t ip) { return ((ip >> 2) % BASIC_BTB_SETS); }

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
    std::cout << "Hot warm cold BTB sets: " << BASIC_BTB_SETS
              << " ways: " << (int) BASIC_BTB_WAYS
              << " indirect buffer size: " << BASIC_BTB_INDIRECT_SIZE
              << " RAS size: " << BASIC_BTB_RAS_SIZE << std::endl;

    open_btb_record("r", false);

//    coverage_accuracy.init(btb_record, BASIC_BTB_SETS, BASIC_BTB_WAYS);

    hot_warm_cold_btb.init(BASIC_BTB_SETS, BASIC_BTB_WAYS);

//    hot_warm_cold_btb.basic_opt.read_record(btb_record, cpu);

    hot_warm_cold_btb.init_record(trace_name, use_twig_prefetcher);

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
//        hot_warm_cold_btb.basic_opt.timestamp++;
        auto btb_entry = hot_warm_cold_btb.find_btb_entry(ip, cpu);

        if (btb_entry == nullptr) {
            // no prediction for this IP
            return std::make_pair(stream_buffer.stream_buffer_predict(ip), true);
        }

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
        auto btb_entry = hot_warm_cold_btb.find_btb_entry(ip, cpu);

        if (btb_entry == NULL) {
            stream_buffer.stream_buffer_update(ip);
            if ((branch_target != 0) && taken) {
                // no prediction for this entry so far, so allocate one
                hot_warm_cold_btb.insert(ip, branch_target, branch_type, cpu, this);
            }
        } else {
            if (taken_only) {
                if (taken)
                    btb_entry->rrpv = 0;
            } else {
                // On re-reference update rrpv to 0
                btb_entry->rrpv = 0;
            }
            // update an existing entry
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
            }
            btb_entry->add_to_taken_history(taken != 0);
            hot_warm_cold_btb.update_lru(ip, cpu);
//            access_counter.access(ip, cpu);
        }
    }
}

void O3_CPU::prefetch_btb(uint64_t ip, uint64_t branch_target, uint8_t branch_type, bool taken, bool to_stream_buffer) {
    // TODO: Here we only prefetch for direct btb, since the hash for indirect branch may change later
    if (branch_type != BRANCH_RETURN &&
        branch_type != BRANCH_INDIRECT &&
        branch_type != BRANCH_INDIRECT_CALL) {
        auto btb_entry = hot_warm_cold_btb.find_btb_entry(ip, cpu);

        if (btb_entry == NULL) {
            if ((branch_target != 0) && taken) {
                if (to_stream_buffer) {
                    stream_buffer.prefetch(ip, branch_target);
                } else {
                    // no prediction for this entry so far, so allocate one
                    hot_warm_cold_btb.insert(ip, branch_target, branch_type, cpu, this);
                }
            }
        } else {
            // TODO: Consider whether to update taken history here. Currently not consider this part.
            if (taken_only) {
                if (taken)
                    btb_entry->rrpv = 0;
            } else {
                // On re-reference update rrpv to 0
                btb_entry->rrpv = 0;
            }
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
            }
            hot_warm_cold_btb.update_lru(ip, cpu);
//            access_counter.access(ip, cpu);
        }
    }
}

void O3_CPU::btb_final_stats() {
    // TODO: Add print later
    cout << "BTB taken num: " << predicted_taken_branch_count
         << " BTB miss num: " << btb_miss_taken_branch_count
         << " BTB miss rate: " << ((double) btb_miss_taken_branch_count) / ((double) predicted_taken_branch_count)
         << endl;
//    hot_warm_cold_btb.print_final_stats(trace_name, program_name);
//    coverage_accuracy.print_final_stats(trace_name, program_name, BASIC_BTB_WAYS);
//    access_counter.print_final_stats(cpu);
}
