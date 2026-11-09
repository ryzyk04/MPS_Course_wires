#pragma once

#include "Puzzle.h"

namespace Wires {

enum class State : uint8_t { WaitingForCommand, WaitingForWires, Playing, Solved, Failed };
enum class Result : uint8_t { None, Solved, Failed };

// Игровая логика получает снимок входов, но не обращается к регистрам AVR.
class Game {
public:
    bool start(uint16_t seed, Difficulty difficulty, uint32_t now, uint8_t openMask);
    Result update(uint32_t now, uint8_t openMask);

    const Puzzle &puzzle() const
    {
        return puzzle_;
    }
    State state() const
    {
        return state_;
    }
    uint8_t cutMask() const
    {
        return cutMask_;
    }

private:
    Puzzle puzzle_;
    State state_ = State::WaitingForCommand;
    uint8_t rawOpen_ = 0;
    uint8_t stableOpen_ = 0;
    uint8_t cutMask_ = 0;
    uint32_t changedAt_[5] = {};
    uint32_t closedSince_ = 0;
    bool allClosed_ = false;
};

} // namespace Wires
