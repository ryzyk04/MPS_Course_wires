#include "game/Display.h"
#include "drivers/Board.h"
#include "Config.h"

namespace Wires {
namespace {

struct Color { uint8_t red, green, blue; };
const Color PALETTE[COLOR_COUNT] = {
    {255,255,255}, {255,0,51}, {255,0,0}, {255,180,0}, {255,255,0},
    {0,255,0}, {0,200,255}, {0,0,255}, {162,0,230}
};

} // namespace

void Display::pixel(uint8_t index, uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness)
{
    const uint8_t grb[3] = {
        uint8_t(uint16_t(green) * brightness / 255),
        uint8_t(uint16_t(red) * brightness / 255),
        uint8_t(uint16_t(blue) * brightness / 255)
    };

    for (uint8_t channel = 0; channel < 3; ++channel) {
        uint8_t &value = pixels_[index * 3 + channel];
        if (value != grb[channel]) {
            value = grb[channel];
            dirty_ = true;
        }
    }
}

void Display::update(const Game &game, uint32_t now)
{
    if (game.state() != State::WaitingForCommand) {
        const Puzzle &puzzle = game.puzzle();
        uint8_t active = 0;
        for (uint8_t position = 0; position < 5; ++position) {
            if (puzzle.activeMask & wireBit(position)) {
                const Color &color = PALETTE[puzzle.colors[active++]];
                pixel(position, color.red, color.green, color.blue, Config::STRIP_BRIGHTNESS);
            } else {
                pixel(position, 0, 0, 0, 0);
            }
        }

        pixel(5, 0, 0, 0, 0);
        const Color difficulty = puzzle.difficulty == EASY ? PALETTE[COLOR_GREEN] :
                                 puzzle.difficulty == MEDIUM ? PALETTE[COLOR_YELLOW] :
                                 PALETTE[COLOR_RED];
        pixel(Config::DIFFICULTY_PIXEL, difficulty.red, difficulty.green,
              difficulty.blue, Config::STATUS_BRIGHTNESS);

        if (game.state() == State::Solved) {
            pixel(Config::STATUS_PIXEL, 0, 255, 0, Config::STATUS_BRIGHTNESS);
        } else if (game.state() == State::Failed) {
            pixel(Config::STATUS_PIXEL, 255, 0, 0, Config::STATUS_BRIGHTNESS);
        } else {
            const bool on = (now / Config::BLINK_MS) % 2 == 0;
            pixel(Config::STATUS_PIXEL, 255, 255, 0, on ? Config::STATUS_BRIGHTNESS : 0);
        }
    }

    if (dirty_ && Hardware::showPixels(pixels_, sizeof(pixels_))) {
        dirty_ = false;
    }
}

} // namespace Wires
