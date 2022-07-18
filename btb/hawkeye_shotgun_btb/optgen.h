//
// Created by Shixin Song on 2021/4/22.
//

#ifndef CHAMPSIM_PT_OPTGEN_H
#define CHAMPSIM_PT_OPTGEN_H

#include <iostream>

#include <math.h>
#include <set>
#include <vector>

#define OPTGEN_VECTOR_SIZE 128

struct ADDR_INFO
{
    uint64_t addr;
    uint32_t last_quanta;
    uint64_t PC;
    bool prefetched;
    uint32_t lru;

    void init(unsigned int curr_quanta)
    {
        last_quanta = 0;
        PC = 0;
        prefetched = false;
        lru = 0;
    }

    void update(unsigned int curr_quanta, uint64_t _pc, bool prediction)
    {
        last_quanta = curr_quanta;
        PC = _pc;
    }

    void mark_prefetch()
    {
        prefetched = true;
    }
};

struct OPTgen
{
    vector<unsigned int> liveness_history;

    uint64_t num_cache;
    uint64_t num_dont_cache;
    uint64_t access;

    uint64_t CACHE_SIZE;

    void init(uint64_t size)
    {
        num_cache = 0;
        num_dont_cache = 0;
        access = 0;
        CACHE_SIZE = size;
        liveness_history.resize(OPTGEN_VECTOR_SIZE, 0);
    }

    void add_access(uint64_t curr_quanta)
    {
        access++;
        liveness_history[curr_quanta] = 0;
    }

    void add_prefetch(uint64_t curr_quanta)
    {
        liveness_history[curr_quanta] = 0;
    }

    bool should_cache(uint64_t curr_quanta, uint64_t last_quanta)
    {
        bool is_cache = true;

        unsigned int i = last_quanta;
        while (i != curr_quanta)
        {
            if(liveness_history[i] >= CACHE_SIZE)
            {
                is_cache = false;
                break;
            }

            i = (i+1) % liveness_history.size();
        }


        //if ((is_cache) && (last_quanta != curr_quanta))
        if ((is_cache))
        {
            i = last_quanta;
            while (i != curr_quanta)
            {
                liveness_history[i]++;
                i = (i+1) % liveness_history.size();
            }
            assert(i == curr_quanta);
        }

        if (is_cache) num_cache++;
        else num_dont_cache++;

        return is_cache;
    }

    uint64_t get_num_opt_hits()
    {
        return num_cache;

        uint64_t num_opt_misses = access - num_cache;
        return num_opt_misses;
    }
};


#endif //CHAMPSIM_PT_OPTGEN_H
