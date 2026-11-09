#pragma once

#include <deque>
#include <map>
#include <vector>
#include <string>
#include <stdint.h>
#include <functional>

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
inline std::function<bool(uint8_t, const uint8_t *, uint8_t)> writeHook;
inline std::function<bool(uint8_t, uint8_t *, uint8_t)> readHook;

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
    writeHook = {};
    readHook = {};
}

inline void inject(const std::string &line)
{
    input.insert(input.end(), line.begin(), line.end());
}
}
