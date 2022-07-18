//
// Created by Shixin Song on 2021/7/28.
//

#ifndef CHAMPSIM_PT_PREFETCH_STREAM_BUFFER_H
#define CHAMPSIM_PT_PREFETCH_STREAM_BUFFER_H

#include <deque>
#include <utility>

using std::vector;
using std::pair;

class StreamBuffer {
    deque<pair<uint64_t, uint64_t>> buffer; // first is ip and second is target
    uint64_t capacity;

public:
    StreamBuffer(uint64_t capacity) : capacity(capacity) {}

    void prefetch(uint64_t ip, uint64_t target) {
        if (buffer.size() >= capacity) {
            buffer.pop_front();
        }
        assert(buffer.size() < capacity);
        buffer.emplace_back(ip, target);
        assert(target != 0);
    }

    uint64_t stream_buffer_predict(uint64_t ip) {
        for (auto &a : buffer) {
            if (a.first == ip) {
                return a.second;
            }
        }
        return 0;
    }

    uint64_t stream_buffer_update(uint64_t ip) {
        for (auto it = buffer.begin(); it != buffer.end(); it++) {
            if (it->first == ip) {
                auto target = it->second;
                buffer.erase(it);
                return target;
            }
        }
        return 0;
    }
};

#endif //CHAMPSIM_PT_PREFETCH_STREAM_BUFFER_H
