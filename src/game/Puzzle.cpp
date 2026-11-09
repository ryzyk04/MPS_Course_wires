#include "game/Puzzle.h"

namespace Wires {
namespace {

// Та же математическая последовательность Park–Miller, что в AVR Libc random().
// Локальное состояние исключает влияние кнопки и предыдущих раундов на раскладку.
uint32_t nextRandom(uint32_t &state, uint8_t bound)
{
    state = uint32_t((uint64_t(state) * 16807UL) % 2147483647UL);
    return state % bound;
}

} // namespace

bool generatePuzzle(uint16_t seed, Difficulty difficulty, Puzzle &out)
{
    if (seed > 9999 || difficulty > HARD) {
        return false;
    }

    Puzzle puzzle;
    puzzle.seed = seed;
    puzzle.difficulty = difficulty;

    uint16_t digits = seed;
    for (int8_t index = 3; index >= 0; --index) {
        puzzle.digits[index] = uint8_t(digits % 10);
        digits /= 10;
    }

    uint32_t randomState = uint32_t(seed) + 1;
    uint8_t positions[5] = {0, 1, 2, 3, 4};

    for (uint8_t index = 4; index > 0; --index) {
        const uint8_t other = uint8_t(nextRandom(randomState, index + 1));
        const uint8_t previous = positions[index];
        positions[index] = positions[other];
        positions[other] = previous;
    }

    puzzle.count = uint8_t(3 + nextRandom(randomState, 3));
    for (uint8_t index = 0; index < puzzle.count; ++index) {
        puzzle.activeMask |= wireBit(positions[index]);
    }

    // Правила работают с активными проводами слева направо.
    uint8_t activeIndex = 0;
    for (uint8_t position = 0; position < 5; ++position) {
        if (puzzle.activeMask & wireBit(position)) {
            puzzle.positions[activeIndex] = position;
            puzzle.colors[activeIndex] = uint8_t(nextRandom(randomState, COLOR_COUNT));
            ++activeIndex;
        }
    }

    puzzle.targetMask = chooseTargets(puzzle);
    out = puzzle;
    return true;
}

} // namespace Wires
