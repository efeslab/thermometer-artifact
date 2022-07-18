
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

#define BASIC_BTB_SETS 384
#define BASIC_BTB_WAYS 4
#define UNCONDITIONAL_BTB_SETS 1280
#define UNCONDITIONAL_BTB_WAYS 4
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

struct BASIC_BTB_ENTRY {
    uint64_t ip_tag;
    uint64_t target;
    uint8_t always_taken = 1;
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

struct FOOTPRINT_BTB_ENTRY {
    uint64_t ip_tag;
    uint64_t target;
    uint8_t always_taken;
    uint64_t lru = 0;
    deque<bool> taken_history;

    uint8_t branch_type;
    unordered_map<uint64_t, vector<BASIC_BTB_ENTRY>> call_footprint;
    unordered_map<uint64_t, vector<BASIC_BTB_ENTRY>> return_footprint;

    explicit FOOTPRINT_BTB_ENTRY(uint64_t ip = 0, uint64_t target = 0, uint8_t always_taken = 1,
                                 uint8_t branch_type = BRANCH_DIRECT_CALL) :
            ip_tag(ip),
            target(target),
            always_taken(always_taken),
            branch_type(branch_type) {
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

    void cache_access(BASIC_BTB_ENTRY *access_entry, bool update_call = true) {
        // access_taken refers to taken or not!
        auto access_block = access_entry->ip_tag >> LOG2_BLOCK_SIZE;
        uint64_t original_block;
        unordered_map<uint64_t, vector<BASIC_BTB_ENTRY>> *footprint;
        if (update_call) {
            original_block = target >> LOG2_BLOCK_SIZE;
            footprint = &call_footprint;
        } else {
            original_block = ip_tag >> LOG2_BLOCK_SIZE;
            footprint = &return_footprint;
        }
        // Update footprint
        if (access_block <= original_block + 6 && access_block >= original_block - 2) {
            (*footprint)[access_block].push_back(*access_entry);
        }
    }

    void operate_prefetch(O3_CPU *o3_cpu, bool prefetch_call) {
        // This function does not actually prefetch instrs. It just add it to prefetch list.
        unordered_map<uint64_t, vector<BASIC_BTB_ENTRY>> *footprint;
        uint64_t key;
        if (prefetch_call) {
            key = ip_tag;
            footprint = &call_footprint;
        } else {
            key = target;
            footprint = &return_footprint;
        }
        for (auto &a: *footprint) {
            auto &region = o3_cpu->shotgun_prefetch_region[key];
            region.clear();
            for (auto &entry: a.second) {
                region.push_back(entry.ip_tag);
                // o3_cpu->prefetch_btb(entry.ip_tag, entry.target, entry.branch_type, entry.always_taken != 0);
            }
        }
        footprint->clear();
    }
};

class Shotgun {
private:
    FOOTPRINT_BTB_ENTRY *current_unconditional = nullptr;
    uint8_t branch_type = BRANCH_OTHER;

public:
    void update_current(O3_CPU *o3_cpu, FOOTPRINT_BTB_ENTRY *entry, uint8_t type) {
        current_unconditional = entry;
        branch_type = type;
        if (current_unconditional != nullptr)
            current_unconditional->operate_prefetch(o3_cpu, branch_type != BRANCH_RETURN);
    }

    void add_footprint(BASIC_BTB_ENTRY *access_entry) {
        if (current_unconditional != nullptr)
            current_unconditional->cache_access(access_entry, branch_type != BRANCH_RETURN);
    }
};

enum class CacheType {
    HOT,
    WARM,
    COLD
};

template<class T>
class HotWarmCold {
    uint64_t total_sets;
    uint64_t total_ways;
    double hot_lower;
    double cold_upper;
    vector<double> category_boundary;
    vector<vector<unordered_map<uint64_t, T>>> btb;
    unordered_map<uint64_t, double> branch_record;

    set<uint64_t> not_trained_branch_record;

    vector<uint64_t> general_evict_counter;
    vector<uint64_t> general_total_counter;

    uint64_t evict_counter[4] = {0};
    uint64_t total_counter[4] = {0};
    uint64_t hot_not_taken[2] = {0};

    string record_dir_suffix;

public:
    HotWarmCold(uint64_t sets, uint64_t ways, double hot_lower, double cold_upper, double warm_split,
                string &record_dir_suffix) :
            total_sets(sets),
            total_ways(ways),
            hot_lower(hot_lower),
            cold_upper(cold_upper),
            record_dir_suffix(record_dir_suffix) {
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
        btb.resize(NUM_CPUS, vector<unordered_map<uint64_t, T>>(sets));
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

    void add_to_record(string &line) {
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
    }

    void init_record(string &trace_name) {
        // TODO: Move original opt_access_record to the sub dir.
        auto short_name = O3_CPU::find_trace_short_name(trace_name, O3_CPU::NameKind::TRAIN);
        fs::path opt_access_record_path = "/mnt/storage/shixins/champsim_pt/opt_access_record" + record_dir_suffix;
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
        while (getline(in, line))
            add_to_record(line);
        in.close();
        cout << "Finish init opt access record (hit access) " << filename << endl;
    }

    double get_hit_access_ratio(uint64_t ip) {
        auto it = branch_record.find(ip);
        if (it == branch_record.end()) {
            not_trained_branch_record.insert(ip);
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

    T *find_btb_entry(uint64_t ip, uint64_t cpu) {
        auto set = get_set_index(ip);
        auto it = btb[cpu][set].find(ip);
        if (it == btb[cpu][set].end()) {
            return nullptr;
        } else {
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

    void add_to_candidate(uint64_t ip, vector<vector<uint64_t>> &candidate, bool curr_ip = false) {
        auto hit_access = get_hit_access_ratio(ip);
        for (uint64_t i = 0; i < category_boundary.size(); i++) {
            if (hit_access <= category_boundary[i]) {
                if (curr_ip && i + 1 == category_boundary.size()) {
                    // warm for current ip
                    candidate[i + 1].push_back(ip);
                    return;
                }
                candidate[i].push_back(ip);
                return;
            }
        }
        if (curr_ip)
            return;
        candidate[category_boundary.size()].push_back(ip);
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
        for (auto &it: btb[cpu][set]) {
            add_to_candidate(it.first, candidate);
        }
        if (consider_keep) {
            add_to_candidate(ip, candidate, curr_hotter);
        }
        for (uint64_t i = 0; i < candidate.size(); i++) {
            if (!candidate[i].empty()) {
                general_evict_counter[i]++;
                general_total_counter[i] += candidate[i].size();
                // Judge whether to predict taken with the last parameter.
                if (judge_consider_taken(i))
                    return choose_victim_one_category(cpu, set, candidate[i], ooo_cpu);
                else
                    return choose_victim_one_category(cpu, set, candidate[i], nullptr);
            }
        }
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
        btb[cpu][set].emplace(ip, T(ip, target));
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

Shotgun shotgun;
string cond_suffix = "_conditional";
string uncond_suffix = "_unconditional";
HotWarmCold<BASIC_BTB_ENTRY> conditional_hwc(BASIC_BTB_SETS, BASIC_BTB_WAYS,
                                             BTB_HOT_LOWER_BOUND, BTB_COLD_UPPER_BOUND, BTB_WARM_SPLIT,
                                             cond_suffix);
HotWarmCold<FOOTPRINT_BTB_ENTRY> unconditional_hwc(UNCONDITIONAL_BTB_SETS, UNCONDITIONAL_BTB_WAYS,
                                                   BTB_HOT_LOWER_BOUND, BTB_COLD_UPPER_BOUND, BTB_WARM_SPLIT,
                                                   uncond_suffix);

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

uint64_t basic_btb_set_index(uint64_t ip) {
    return ((ip >> 2) % BASIC_BTB_SETS);
}

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
    std::cout << "Hot warm cold Shotgun BTB sets: " << BASIC_BTB_SETS
              << " ways: " << BASIC_BTB_WAYS
              << " unconditional BTB sets: " << UNCONDITIONAL_BTB_SETS
              << " ways: " << UNCONDITIONAL_BTB_WAYS
              << " indirect buffer size: " << BASIC_BTB_INDIRECT_SIZE
              << " RAS size: " << BASIC_BTB_RAS_SIZE << std::endl;

    open_btb_record("r", true);

    conditional_hwc.init(BASIC_BTB_SETS, BASIC_BTB_WAYS);
    unconditional_hwc.init(UNCONDITIONAL_BTB_SETS, UNCONDITIONAL_BTB_WAYS);

    conditional_hwc.init_record(trace_name);
    unconditional_hwc.init_record(trace_name);


//    for (uint32_t i = 0; i < BASIC_BTB_SETS; i++) {
//        for (uint32_t j = 0; j < BASIC_BTB_WAYS; j++) {
//            basic_btb[cpu][i][j].ip_tag = 0;
//            basic_btb[cpu][i][j].target = 0;
//            basic_btb[cpu][i][j].always_taken = 0;
//            basic_btb[cpu][i][j].lru = 0;
//        }
//    }
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
    } else if (branch_type == BRANCH_CONDITIONAL) {
        // Access C-BTB
        auto btb_entry = conditional_hwc.find_btb_entry(ip, cpu);
        if (btb_entry == nullptr) {
            // no prediction for this IP
            return std::make_pair(stream_buffer.stream_buffer_predict(ip), true);
        }
        return std::make_pair(btb_entry->target, btb_entry->always_taken);
    } else {
        // Access U-BTB
        auto btb_entry = unconditional_hwc.find_btb_entry(ip, cpu);
        if (btb_entry == nullptr) {
            // no prediction for this IP
            return std::make_pair(0, true);
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
        return;
    }
    if (branch_type == BRANCH_CONDITIONAL) {
        basic_btb_conditional_history[cpu] <<= 1;
        if (taken) {
            basic_btb_conditional_history[cpu] |= 1;
        }
        // Update conditional_btb and add footprint
        auto btb_entry = conditional_hwc.find_btb_entry(ip, cpu);
        if (btb_entry == nullptr) {
            stream_buffer.stream_buffer_update(ip);
            // no prediction for this entry so far, so allocate one
            if (branch_target != 0 && taken) {
                conditional_hwc.insert(ip, branch_target, branch_type, cpu, this);
                auto repl_entry = conditional_hwc.find_btb_entry(ip, cpu);
                if (repl_entry != nullptr) {
                    // Add footprint
                    shotgun.add_footprint(repl_entry);
                }
            }
        } else {
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
            }
            btb_entry->add_to_taken_history(taken != 0);
            conditional_hwc.update_lru(ip, cpu);
            shotgun.add_footprint(btb_entry);
        }
    } else if (branch_type == BRANCH_RETURN) {
        // recalibrate call-return offset
        // if our return prediction got us into the right ball park, but not the
        // exactly correct byte target, then adjust our call instr size tracker
        uint64_t call_ip = pop_basic_btb_ras(cpu);
        uint64_t estimated_call_instr_size = basic_btb_abs_addr_dist(call_ip, branch_target);
        if (estimated_call_instr_size <= 10) {
            basic_btb_call_instr_sizes[cpu][basic_btb_call_size_tracker_hash(call_ip)] = estimated_call_instr_size;
        }
        // Predecode and update current
        auto btb_entry = unconditional_hwc.find_btb_entry(ip, cpu);
        shotgun.update_current(this, btb_entry, branch_type);
    } else {
        // BRANCH_DIRECT_JUMP or BRANCH_DIRECT_CALL
        auto btb_entry = unconditional_hwc.find_btb_entry(ip, cpu);
        if (btb_entry == nullptr) {
            // no prediction for this entry so far, so allocate one
            if (branch_target != 0 && taken) {
                unconditional_hwc.insert(ip, branch_target, branch_type, cpu, this);
                auto repl_entry = unconditional_hwc.find_btb_entry(ip, cpu);
                if (repl_entry != nullptr) {
                    shotgun.update_current(this, repl_entry, branch_type);
                }
            }
        } else {
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
            }
            btb_entry->branch_type = branch_type;
            // Prefetch conditional branches in the target region
//            btb_entry->add_to_taken_history(taken != 0);
            unconditional_hwc.update_lru(ip, cpu);
            shotgun.update_current(this, btb_entry, branch_type);
        }
    }
}

void O3_CPU::prefetch_btb(uint64_t ip, uint64_t branch_target, uint8_t branch_type, bool taken, bool to_stream_buffer) {
    // TODO: Here we only prefetch for direct btb, since the hash for indirect branch may change later
    if (branch_type == BRANCH_CONDITIONAL) {
//        basic_opt.timestamp++;
        auto btb_entry = conditional_hwc.find_btb_entry(ip, cpu);

        if (btb_entry == nullptr) {
            // no prediction for this entry so far, so allocate one
            if (branch_target != 0 && taken) {
                if (to_stream_buffer) {
                    stream_buffer.prefetch(ip, branch_target);
                } else {
                    conditional_hwc.insert(ip, branch_target, branch_type, cpu, this);
                }
            }
        } else {
            if (!taken) {
                btb_entry->always_taken = 0;
            } else {
                btb_entry->target = branch_target;
            }
            conditional_hwc.update_lru(ip, cpu);
        }
    }
}

void O3_CPU::btb_final_stats() {

}

