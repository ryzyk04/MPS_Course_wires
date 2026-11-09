#include "game/Game.h"
#include "Config.h"

namespace Wires {

bool Game::start(uint16_t seed, Difficulty difficulty, uint32_t now, uint8_t openMask)
{
    Puzzle next;
    if (!generatePuzzle(seed, difficulty, next)) {
        return false;
    }

    puzzle_ = next;
    rawOpen_ = openMask & 0x1F;
    stableOpen_ = rawOpen_;
    cutMask_ = 0;
    for (uint8_t index = 0; index < 5; ++index) {
        changedAt_[index] = now;
    }
    allClosed_ = false;
    closedSince_ = now;
    state_ = State::WaitingForWires;
    return true;
}

Result Game::update(uint32_t now, uint8_t openMask)
{
    if (state_ == State::WaitingForCommand) {
        return Result::None;
    }

    openMask &= 0x1F;
    uint8_t newCuts = 0;
    for (uint8_t index = 0; index < 5; ++index) {
        const uint8_t bit = wireBit(index);
        if ((openMask & bit) != (rawOpen_ & bit)) {
            rawOpen_ ^= bit;
            changedAt_[index] = now;
        }

        if ((openMask & bit) != (stableOpen_ & bit) &&
            uint32_t(now - changedAt_[index]) >= Config::WIRE_DEBOUNCE_MS) {
            stableOpen_ ^= bit;
            if (openMask & bit) {
                newCuts |= bit;
            }
        }
    }

    if (state_ == State::WaitingForWires) {
        // Готовность требует непрерывного замыкания всех физических входов.
        if (openMask) {
            allClosed_ = false;
            return Result::None;
        }

        if (!allClosed_) {
            allClosed_ = true;
            closedSince_ = now;
        }
        if (uint32_t(now - closedSince_) >= Config::READY_MS) {
            state_ = State::Playing;
        }
        return Result::None;
    }

    if (state_ != State::Playing) {
        return Result::None;
    }

    cutMask_ |= newCuts & puzzle_.activeMask;
    if (cutMask_ & uint8_t(~puzzle_.targetMask)) {
        state_ = State::Failed;
        return Result::Failed;
    }

    if ((cutMask_ & puzzle_.targetMask) == puzzle_.targetMask) {
        // Лишний разрыв мог начаться позже правильного и ещё проходить фильтр.
        const uint8_t forbidden = puzzle_.activeMask & uint8_t(~puzzle_.targetMask);
        if (rawOpen_ & forbidden) {
            return Result::None;
        }
        state_ = State::Solved;
        return Result::Solved;
    }

    return Result::None;
}

} // namespace Wires
