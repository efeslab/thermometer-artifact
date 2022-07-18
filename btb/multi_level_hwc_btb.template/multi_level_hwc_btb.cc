
/*
 * This file implements a multi-level opt Branch Target Buffer (BTB) structure.
 * It uses a set-associative BTB to predict the targets of non-return branches,
 * and it uses a small Return Address Stack (RAS) to predict the target of
 * returns.
 */

#include "ooo_cpu.h"
#include "../accuracy.h"
#include "../prefetch_stream_buffer.h"
#include "../branch_bias.h"
#include "../multi_level_btb.h"
#include "../access_record.h"
#include <set>
#include <queue>
#include <sstream>

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

#define HWC_BOUNDARY @HWC_BOUNDARY@;

//#define BASIC_BTB_SETS (total_btb_entries / total_btb_ways)
//#define BASIC_BTB_WAYS total_btb_ways
#define BASIC_BTB_INDIRECT_SIZE 4096
#define BASIC_BTB_RAS_SIZE 32
#define BASIC_BTB_CALL_INSTR_SIZE_TRACKERS 1024

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

//CoverageAccuracy coverage_accuracy;
StreamBuffer stream_buffer(32);
//BranchBias branch_bias;

vector<vector<uint64_t>> btb_level_info = {
        // Latency, total_sets, total_ways
        {0, 16, 1, (uint64_t) BTBType::NANO}, // nano BTB
        {1, 64, 1, (uint64_t) BTBType::MILI}, // mili BTB
        {2, 64, 2, (uint64_t) BTBType::L1}, // L1 BTB
        {3, 2048, 4, (uint64_t) BTBType::L2} // L2 BTB
};

struct HWCBTBEntry: public BTBEntry {
    uint64_t lru = 0;

    HWCBTBEntry() : BTBEntry() {}
    HWCBTBEntry(uint64_t ip, uint64_t target, uint8_t branch_type) : BTBEntry(ip, target, branch_type) {}
};

enum class CacheType {
    HOT,
    WARM,
    COLD
};

class HWCBTB: public BTB<HWCBTBEntry> {
    vector<double> category_boundary;
    unordered_map<uint64_t, double> branch_record;
    BTBType btb_type;

public:
    HWCBTB(uint64_t latency, uint64_t sets, uint64_t ways, BTBType btb_type) : BTB<HWCBTBEntry>(latency, sets, ways),
                                                                               btb_type(btb_type) {
        category_boundary = HWC_BOUNDARY;
    }

    uint64_t num_category() {
        return category_boundary.size() + 1;
    }

    void update(uint64_t ip, uint64_t cpu) override {
        auto set = find_set_index(ip);
        uint64_t max = 0;
        for (auto &it : btb[cpu][set]) {
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
        for (auto &candidate_ip : candidates) {
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
        auto short_name = O3_CPU::find_trace_short_name(trace_name, O3_CPU::NameKind::TRAIN);
        fs::path opt_access_record_path = "/mnt/storage/shixins/champsim_pt/opt_access_record" + btb_type_to_suffix(btb_type);
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
//            not_trained_branch_record.insert(ip);
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

    uint64_t choose_victim_one_category(uint64_t cpu, uint64_t set, vector<uint64_t> &one_category, O3_CPU *ooo_cpu) {
        // TODO: Add a parameter judge_taken here to consider whether considering predicted taken or not.
        // If ooo_cpu != nullptr, predict whether taken or not and then determine the victim.
        // If ooo_cpu == nullptr, then we don't consider predict taken here.
        vector<uint64_t> not_taken_candidates;
        if (ooo_cpu != nullptr) {
            for (auto candidate : one_category) {
                if (ooo_cpu->predict_branch(candidate, 0, 0, 0) == 0) {
                    not_taken_candidates.push_back(candidate);
                }
            }
        }
        auto &final_candidates = not_taken_candidates.empty() ? one_category : not_taken_candidates;

        if (use_lru) {
            return find_lru_min(set, cpu, final_candidates);
        }
//        auto taken_history_choice = choose_taken_history(final_candidates, cpu, set);
//        if (taken_history_choice != 0) return taken_history_choice;
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
        for (auto &it : btb[cpu][set]) {
            add_to_candidate(it.first, candidate);
        }
        if (consider_keep) {
            add_to_candidate(ip, candidate, curr_hotter);
        }
        for (uint64_t i = 0; i < candidate.size(); i++) {
            if (!candidate[i].empty()) {
//                general_evict_counter[i]++;
//                general_total_counter[i] += candidate[i].size();
                // Judge whether to predict taken with the last parameter.
                if (judge_consider_taken(i))
                    return choose_victim_one_category(cpu, set, candidate[i], ooo_cpu);
                else
                    return choose_victim_one_category(cpu, set, candidate[i], nullptr);
            }
        }
    }

    BTBEntry insert(uint64_t ip, uint64_t target, uint8_t branch_type, uint64_t cpu, O3_CPU *ooo_cpu) override {
        BTBEntry victim;
        auto set = find_set_index(ip);
        assert(set < btb[cpu].size());
        auto it = btb[cpu][set].find(ip);
        assert(it == btb[cpu][set].end());
        if (btb[cpu][set].size() >= total_ways) {
            // Evict
            uint64_t victim_ip;
            victim_ip = choose_victim_general(ip, cpu, set, ooo_cpu);
            assert(victim_ip != 0);
            if (victim_ip == ip) {
//                cout << btb_type_to_suffix(btb_type) << " bypass " << std::hex << ip << endl;
                return victim;
            }
            victim = BTBEntry(victim_ip, btb[cpu][set][victim_ip].target, btb[cpu][set][victim_ip].branch_type);
            btb[cpu][set].erase(victim_ip);
//            cout << btb_type_to_suffix(btb_type) << " evict " << std::hex << victim_ip << endl;
        }
        assert(btb[cpu][set].size() < total_ways);
        btb[cpu][set].emplace(ip, HWCBTBEntry(ip, target, branch_type));
//        cout << btb_type_to_suffix(btb_type) << " insert " << std::hex << ip << endl;
        update(ip, cpu);
        return victim;
    }
};

class MultiLevelHWCBTB : public MultiLevelBTB<HWCBTBEntry, HWCBTB> {
public:
    explicit MultiLevelHWCBTB(vector<vector<uint64_t>> &level_info) : MultiLevelBTB() {
        for (auto &level : level_info) {
            btbs.emplace_back(level[0], level[1], level[2], (BTBType) level[3]);
        }
    }
};

MultiLevelHWCBTB multi_level_btb(btb_level_info);

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
    if(addr1 > addr2) {
        return addr1 - addr2;
    }

    return addr2 - addr1;
}

uint64_t basic_btb_indirect_hash(uint8_t cpu, uint64_t ip) {
    uint64_t hash = (ip >> 2) ^ (basic_btb_conditional_history[cpu]);
    return (hash & (BASIC_BTB_INDIRECT_SIZE-1));
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
    return (ip & (BASIC_BTB_CALL_INSTR_SIZE_TRACKERS-1));
}

uint64_t basic_btb_get_call_size(uint8_t cpu, uint64_t ip) {
    uint64_t size = basic_btb_call_instr_sizes[cpu][basic_btb_call_size_tracker_hash(ip)];

    return size;
}

void O3_CPU::initialize_btb() {
    std::cout << "Muli-level Basic BTB" << std::endl
              << " indirect buffer size: " << BASIC_BTB_INDIRECT_SIZE
              << " RAS size: " << BASIC_BTB_RAS_SIZE << std::endl;

//    open_btb_record("r", false);
//    multi_level_btb.read_record(btb_record, cpu);

//    coverage_accuracy.init(btb_record, BASIC_BTB_SETS, BASIC_BTB_WAYS);


//    branch_bias.init(total_btb_ways, total_btb_entries);
    multi_level_btb.init_record(trace_name);

    for (uint32_t i = 0; i < BASIC_BTB_INDIRECT_SIZE; i++) {
        basic_btb_indirect[cpu][i] = 0;
    }
    basic_btb_conditional_history[cpu] = 0;

    for (uint32_t i = 0; i < BASIC_BTB_RAS_SIZE; i++) {
        basic_btb_ras[cpu][i] = 0;
    }
    basic_btb_ras_index[cpu] = 0;
    for (uint32_t i=0; i<BASIC_BTB_CALL_INSTR_SIZE_TRACKERS; i++) {
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
        auto find_result = multi_level_btb.find_btb_entry(ip, cpu);
        if (latency != nullptr) {
            *latency = find_result.second;
        }

        if (find_result.first == nullptr) {
            // no prediction for this IP
            return std::make_pair(stream_buffer.stream_buffer_predict(ip), true);
        }

        return std::make_pair(find_result.first->target, find_result.first->always_taken);
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
//        branch_bias.access(ip, cpu, taken != 0);
        auto find_result = multi_level_btb.find_btb_entry(ip, cpu);

        if (find_result.first == nullptr) {
            stream_buffer.stream_buffer_update(ip);
            if ((branch_target != 0) && taken) {
                // no prediction for this entry so far, so allocate one
                multi_level_btb.insert(ip, branch_target, branch_type, cpu, this);
            }
        } else {
            // update an existing entry
            if (!taken) {
                find_result.first->always_taken = 0;
            } else {
                // Only update target on taken!!!
                find_result.first->target = branch_target;
                multi_level_btb.update(ip, cpu, this);
            }
        }
    }
}

void O3_CPU::prefetch_btb(uint64_t ip, uint64_t branch_target, uint8_t branch_type, bool taken, bool to_stream_buffer) {
    // TODO: Here we only prefetch for direct btb, since the hash for indirect branch may change later
    if (branch_type != BRANCH_RETURN &&
        branch_type != BRANCH_INDIRECT &&
        branch_type != BRANCH_INDIRECT_CALL) {
        auto find_result = multi_level_btb.find_btb_entry(ip, cpu);

        if (find_result.first == nullptr) {
            if ((branch_target != 0) && taken) {
                // no prediction for this entry so far, so allocate one
                if (to_stream_buffer) {
                    stream_buffer.prefetch(ip, branch_target);
                } else {
                    multi_level_btb.insert(ip, branch_target, branch_type, cpu, this);
                }
            }
        } else {
            // update an existing entry
            if (!taken) {
                find_result.first->always_taken = 0;
            } else {
                // Only update target on taken!!!
                find_result.first->target = branch_target;
                multi_level_btb.update(ip, cpu, this);
            }
        }
    }
}


void O3_CPU::btb_final_stats() {
//    cout << "BTB taken num: " << predicted_taken_branch_count
//         << " BTB miss num: " << btb_miss_taken_branch_count
//         << " BTB miss rate: " << ((double) btb_miss_taken_branch_count) / ((double) predicted_taken_branch_count)
//         << endl;
//    branch_bias.print_final_stats(trace_name, cpu);
//    print_final_stats(trace_name, program_name, BASIC_BTB_WAYS);
//    multi_level_btb.print_access_record(trace_name, cpu);
}

