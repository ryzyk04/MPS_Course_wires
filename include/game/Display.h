#pragma once

#include "Game.h"

namespace Wires {

class Display {
public:
    void update(const Game &game, uint32_t now);

private:
    uint8_t pixels_[24] = {};
    bool dirty_ = true;

    void pixel(uint8_t index, uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness);
};

} // namespace Wires
