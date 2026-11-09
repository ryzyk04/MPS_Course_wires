#pragma once

#include <deque>
#include <map>
#include <vector>
#include <string>
#include <stdint.h>

namespace Mock {
inline std::deque<uint8_t> input;
inline std::string output;
inline bool fault = false;
inline uint32_t id = 100;
inline std::map<uint8_t, std::vector<uint8_t>> replies;
inline std::map<uint8_t, std::vector<uint8_t>> writes;
inline std::vector<uint8_t> pixels;
inline bool showSuccess = true;
inline unsigned showCalls = 0;

inline void reset()
{
    input.clear();
    output.clear();
    replies.clear();
    writes.clear();
    pixels.clear();
    fault = false;
    id = 100;
    showCalls = 0;
    showSuccess = true;
}

inline void inject(const std::string &line)
{
    input.insert(input.end(), line.begin(), line.end());
}
}
