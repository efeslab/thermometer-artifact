
/*
 * This file implements a basic Branch Target Buffer (BTB) structure.
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
extern bool use_twig_prefetcher;

#define HWC_BOUNDARY @HWC_BOUNDARY@;

#define BASIC_BTB_SETS (2048 * 4 / total_btb_ways)
#define BASIC_BTB_WAYS total_btb_ways
#define BASIC_BTB_INDIRECT_SIZE 4096
#define BASIC_BTB_RAS_SIZE 32 // TODO: Different from the original value 64, need to rerun tests!
#define BASIC_BTB_CALL_INSTR_SIZE_TRACKERS 1024

#define SRRIP_COUNTER_SIZE 2
#define SRRIP_DISTANT_INTERVAL (1 << SRRIP_COUNTER_SIZE) - 1
#define SRRIP_LONG_INTERVAL (1 << SRRIP_COUNTER_SIZE) - 2

bool taken_only = false;


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
CoverageAccuracy coverage_accuracy;
StreamBuffer stream_buffer(32);

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

template<class T>
class Opt {
private:
    vector<vector<unordered_map<uint64_t, set<uint64_t>>>> future_accesses;
    vector<vector<unordered_map<uint64_t, set<uint64_t>>>> future_prefetches;
    vector<vector<unordered_map<uint64_t, T>>> *current_btb = nullptr;
    uint64_t total_sets;
    uint64_t total_ways;

    uint64_t last_timestamp = 0;
public:
    uint64_t timestamp = 0;

    Opt(uint64_t total_sets, uint64_t total_ways) :
            total_sets(total_sets),
            total_ways(total_ways) {}

    void init(uint64_t sets, uint64_t ways) {
        total_sets = sets;
        total_ways = ways;
        future_accesses.resize(NUM_CPUS, vector<unordered_map<uint64_t, set<uint64_t>>>(total_sets));
        future_prefetches.resize(NUM_CPUS, vector<unordered_map<uint64_t, set<uint64_t>>>(total_sets));
    }

    void get_btb_pointer(vector<vector<unordered_map<uint64_t, T>>> *btb) {
        current_btb = btb;
    }

    void read_record(FILE *demand_record, uint64_t cpu) {
        assert(demand_record != nullptr);
        uint64_t ip, counter;
        int type = 0;
        while (true) {
            if (fscanf(demand_record, "%llu %llu", &ip, &counter) == EOF) break;
            type = 0;
            vector<vector<unordered_map<uint64_t, set<uint64_t>>>> *future;
            if (type == 0) {
                future = &future_accesses;
            } else {
                future = &future_prefetches;
            }
            auto set_index = find_set_index(ip);
            auto it = (*future)[cpu][set_index].find(ip);
            if (it == (*future)[cpu][set_index].end()) {
                (*future)[cpu][set_index].emplace_hint(it, ip, set<uint64_t>());
            }
            (*future)[cpu][set_index][ip].insert(counter);
        }
        last_timestamp = counter;
        cout << "The last timestamp: " << last_timestamp << endl;
        rewind(demand_record);
    }

    uint64_t find_set_index(uint64_t ip) {
        return ((ip >> 2) & (total_sets - 1));
    }

    T *find_btb_entry(uint64_t ip, uint64_t cpu) {
        auto set = find_set_index(ip);
        auto it = (*current_btb)[cpu][set].find(ip);
        return it == (*current_btb)[cpu][set].end() ? nullptr : &(it->second);
    }

    uint64_t find_victim(uint64_t ip, uint64_t cpu) {
        assert(current_btb != nullptr);
        auto set = find_set_index(ip);
        auto time = timestamp - 1; // Get the current timestamp (we've +1 before the prediction)
        assert((*current_btb)[cpu][set].size() <= total_ways);
        if ((*current_btb)[cpu][set].size() >= total_ways) {
//            if (future_accesses[cpu][set][ip].find(time) == future_accesses[cpu][set][ip].end()) {
//                assert(time > last_timestamp);
//            }
            // Evict a btb entry if it is full
            auto current_it = future_accesses[cpu][set][ip].upper_bound(time);
            if (current_it == future_accesses[cpu][set][ip].end()) {
                // There is no point caching the key
                // cout << "Error: cannot find the corresponding timestamp " << timestamp << endl;
                return ip;
            }
            // First, find the one among the current set will be prefetched furthest
            bool prefetch_found = false;
            pair<uint64_t, uint64_t> prefetch_candidate;
            for (auto &entry: (*current_btb)[cpu][set]) {
                auto it = future_prefetches[cpu][set][entry.first].upper_bound(time);
                if (it != future_prefetches[cpu][set][entry.first].end()) {
                    // Find the prefetch
                    // Check whether the prefetch is before the next load of this line
                    auto demand_it = future_accesses[cpu][set][entry.first].upper_bound(time);
                    if (demand_it != future_accesses[cpu][set][entry.first].end() && (*it) > (*demand_it)) {
                        // Demand load is presend and prefetch comes after the first load, so ignore
                        continue;
                    }
                    if (prefetch_found) {
                        if (prefetch_candidate.first < *it) {
                            prefetch_candidate = make_pair(*it, entry.first);
                        }
                    } else {
                        prefetch_found = true;
                        prefetch_candidate = make_pair(*it, entry.first);
                    }
                }
            }
            if (prefetch_found) {
                return prefetch_candidate.second;
            } else {
                // Find victim in future demand access
                pair<uint64_t, uint64_t> candidate;
                candidate.first = *current_it;
                candidate.second = ip;
                for (const auto &entry: (*current_btb)[cpu][set]) {
                    auto it = future_accesses[cpu][set][entry.first].upper_bound(time);
                    if (it == future_accesses[cpu][set][entry.first].end()) {
                        candidate.first = last_timestamp + 1;
                        candidate.second = entry.first;
                    } else if (candidate.first < *it) {
                        candidate.first = *it;
                        candidate.second = entry.first;
                    }
                }
                return candidate.second;
            }
        }
        assert(0);
    }

};

class HotWarmCold {
    uint64_t total_sets;
    uint64_t total_ways;
    vector<double> category_boundary;
    vector<vector<unordered_map<uint64_t, BASIC_BTB_ENTRY>>> btb;
    unordered_map<uint64_t, double> branch_record;

    set<uint64_t> not_trained_branch_record;

    vector<uint64_t> general_evict_counter;
    vector<uint64_t> general_total_counter;

    struct EvictCompareEntry {
        vector<uint8_t> candidate_type; // 0-3 is for old branches in btb, and 4 is for curr branch
        vector<bool> candidate_taken;
        uint32_t hwc_choice; // 0-4: index of victim chosen by hwc
        uint32_t opt_choice; // 0-4: index of victim chosen by opt

        static uint32_t judge_index(vector<uint64_t> &candidate_ip, uint64_t target_ip) {
            for (uint32_t i = 0; i < candidate_ip.size(); i++) {
                if (target_ip == candidate_ip[i])
                    return i;
            }
            assert(0);
        }

        EvictCompareEntry(unordered_map<uint64_t, BASIC_BTB_ENTRY> &btb_set,
                          uint64_t curr_ip, uint64_t hwc_ip, uint64_t opt_ip,
                          HotWarmCold *hwc, O3_CPU *ooo_cpu) {
            vector<uint64_t> candidate_ip;
            for (auto &it: btb_set) {
                candidate_ip.push_back(it.first);
            }
            candidate_ip.push_back(curr_ip);
            hwc_choice = judge_index(candidate_ip, hwc_ip);
            opt_choice = judge_index(candidate_ip, opt_ip);
            for (auto a: candidate_ip) {
                candidate_type.push_back(hwc->judge_general_type(a));
                candidate_taken.push_back(ooo_cpu->predict_branch(a, 0, 0, 0));
            }
        }
    };

    vector<EvictCompareEntry> evict_compare_record;

public:
    Opt<BASIC_BTB_ENTRY> basic_opt;

    HotWarmCold(uint64_t sets, uint64_t ways) :
            total_sets(sets),
            total_ways(ways),
            basic_opt(total_sets, total_ways) {
        category_boundary = HWC_BOUNDARY;
        general_evict_counter.resize(num_category(), 0);
        general_total_counter.resize(num_category(), 0);
    }

    friend ostream &operator<<(ostream &os, const EvictCompareEntry &entry);

    void init(uint64_t sets, uint64_t ways) {
        total_sets = sets;
        total_ways = ways;
        btb.resize(NUM_CPUS, vector<unordered_map<uint64_t, BASIC_BTB_ENTRY>>(sets));
        basic_opt.init(sets, ways);
        basic_opt.get_btb_pointer(&btb);
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

    void init_record(string &trace_name, bool with_twig = false) {
        // TODO: Move original opt_access_record to the sub dir.
        auto short_name = O3_CPU::find_trace_short_name(trace_name, O3_CPU::NameKind::TRAIN);
        if (with_twig) {
            short_name = "twig_" + short_name;
        }
        fs::path opt_access_record_path = "/mnt/storage/shixins/champsim_pt/opt_access_record";
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
    }

    double get_hit_access_ratio(uint64_t ip) {
        auto it = branch_record.find(ip);
        if (it == branch_record.end()) {
            not_trained_branch_record.insert(ip);
            return 0.0;
//            return ((double) rand()) / (double) RAND_MAX;
        }
//        assert(it != branch_record.end());
        return it->second;
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
        return ((ip >> 2) & (total_sets - 1));
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

    void add_to_candidate(uint64_t ip, vector<vector<uint64_t>> &candidate, bool curr_ip = false) {
        auto hit_access = get_hit_access_ratio(ip);
        for (uint64_t i = 0; i < category_boundary.size(); i++) {
            if (hit_access <= category_boundary[i]) {
                if (curr_ip && i != 0 && hit_access > 0.5) {
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

    void add_to_opt_compare_record(uint64_t ip, uint64_t cpu, uint64_t set, uint64_t hwc_victim, O3_CPU *ooo_cpu) {
        auto opt_victim = basic_opt.find_victim(ip, cpu);
        evict_compare_record.emplace_back(btb[cpu][set], ip, hwc_victim, opt_victim, this, ooo_cpu);
    }

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
//            else {
//                victim_ip = choose_victim(ip, cpu, set, ooo_cpu);
//            }
            add_to_opt_compare_record(ip, cpu, set, victim_ip, ooo_cpu);
            assert(victim_ip != 0);
            coverage_accuracy.get_reuse_distance(victim_ip, basic_opt.timestamp - 1, victim_ip == ip);
            if (victim_ip == ip) return;
            btb[cpu][set].erase(victim_ip);
//            access_counter.evict(victim_ip, cpu);
        }
        assert(btb[cpu][set].size() < total_ways);
        btb[cpu][set].emplace(ip, BASIC_BTB_ENTRY(ip, target));
        update_lru(ip, cpu);
//        access_counter.insert(ip, cpu, target, branch_type);
    }

    void print_final_stats(string &trace_name, string &program_name) {
        // TODO: Fix the dirty fix here
//        string type[4] = {"cold", "warm not taken", "warm taken", "hot"};
//        for (size_t i = 0; i < 4; i++) {
//            cout << "Evict " << type[i] << ": " << evict_counter[i]
//                 << "\t Average candidate: " << (double) total_counter[i] / (double) evict_counter[i] << endl;
//        }
//        cout << "Evict hot not taken " << hot_not_taken[0] << " from " << hot_not_taken[1] << endl;
        for (uint64_t i = 0; i < general_evict_counter.size(); i++) {
            cout << "Evict the " << i << "th category: " << general_evict_counter[i]
                 << "\t Average candidate: " << (double) general_total_counter[i] / (double) general_evict_counter[i]
                 << endl;
        }

        // Print compare with opt
        cout << "Start print compare with opt with size = " << evict_compare_record.size() << endl;
        string dir = "/mnt/storage/shixins/champsim_pt/hwc_opt_compare_raw_data/hwc_opt_" + program_name +
                     std::to_string(total_btb_ways);
        fs::create_directory(dir);
        auto short_name = O3_CPU::find_trace_short_name(trace_name, O3_CPU::NameKind::TRACE_TRAIN);
        string filename = dir + "/" + short_name + ".csv";
        cout << "Output hwc opt compare to " << filename << endl;
        ofstream out(filename.c_str());
        assert(out.is_open());
        out << "0,0,1,1,2,2,3,3,4,4,hwc choice,opt choice" << endl;
        for (auto &it: evict_compare_record) {
            out << it << endl;
        }
        out.close();
        cout << "End print compare with opt" << endl;
        fs::path not_trained_dir = "/mnt/storage/shixins/champsim_pt/input_generalization/not_trained_branches";
        fs::create_directory(not_trained_dir);
        auto program_not_trained_dir = not_trained_dir / (program_name + std::to_string(total_btb_ways));
        fs::create_directory(program_not_trained_dir);
        auto not_trained_file = program_not_trained_dir / (short_name + ".txt");
        cout << "Print not trained branches to " << not_trained_file << " with total num: "
             << not_trained_branch_record.size() << endl;
        ofstream not_trained_out(not_trained_file.c_str());
        for (auto a: not_trained_branch_record) {
            not_trained_out << a << endl;
        }
        not_trained_out.close();
        cout << "End print not trained branches" << endl;
    }
};

ostream &operator<<(ostream &os, const HotWarmCold::EvictCompareEntry &entry) {
    if (entry.candidate_type.size() != entry.candidate_taken.size()) {
        cout << "Wrong size" << endl;
    }
    assert(entry.candidate_type.size() == entry.candidate_taken.size());
    for (uint32_t i = 0; i < entry.candidate_type.size(); i++) {
        os << (int) entry.candidate_type[i] << "," << (int) entry.candidate_taken[i] << ",";
    }
    os << (int) entry.hwc_choice << "," << (int) entry.opt_choice;
    return os;
}

HotWarmCold hot_warm_cold_btb(BASIC_BTB_SETS, BASIC_BTB_WAYS);

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

uint64_t basic_btb_set_index(uint64_t ip) { return ((ip >> 2) & (BASIC_BTB_SETS - 1)); }

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
    std::cout << "Hot warm cold new BTB sets: " << BASIC_BTB_SETS
              << " ways: " << (int) BASIC_BTB_WAYS
              << " indirect buffer size: " << BASIC_BTB_INDIRECT_SIZE
              << " RAS size: " << BASIC_BTB_RAS_SIZE << std::endl;

    open_btb_record("r", false);

    coverage_accuracy.init(btb_record, BASIC_BTB_SETS, BASIC_BTB_WAYS);

    hot_warm_cold_btb.init(BASIC_BTB_SETS, BASIC_BTB_WAYS);

    hot_warm_cold_btb.basic_opt.read_record(btb_record, cpu);

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
        hot_warm_cold_btb.basic_opt.timestamp++;
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
    hot_warm_cold_btb.print_final_stats(trace_name, program_name);
    coverage_accuracy.print_final_stats(trace_name, program_name, BASIC_BTB_WAYS);
//    access_counter.print_final_stats(cpu);
}
