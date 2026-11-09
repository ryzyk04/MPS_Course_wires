#pragma once

#include <stdint.h>

namespace Wires {

enum Difficulty : uint8_t { EASY, MEDIUM, HARD };

enum ColorId : uint8_t {
    COLOR_WHITE, COLOR_PINK, COLOR_RED, COLOR_ORANGE, COLOR_YELLOW,
    COLOR_GREEN, COLOR_CYAN, COLOR_BLUE, COLOR_VIOLET, COLOR_COUNT
};

struct Puzzle {
    uint16_t seed = 0;
    Difficulty difficulty = EASY;
    uint8_t digits[4] = {};
    uint8_t colors[5] = {};
    uint8_t positions[5] = {};
    uint8_t count = 0;
    uint8_t activeMask = 0;
    uint8_t targetMask = 0;
};

inline uint8_t wireBit(uint8_t position)
{
    return uint8_t(1U << position);
}

bool generatePuzzle(uint16_t seed, Difficulty difficulty, Puzzle &out);
uint8_t chooseTargets(const Puzzle &puzzle);

} // namespace Wires
