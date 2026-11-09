#include "communication/EspLink.h"
#include "communication/Text.h"
#include "drivers/Board.h"
#include "module_bus.h"
#include "Config.h"

#include <string.h>

namespace Wires {

void EspLink::begin(bool pressed, uint32_t now)
{
    buttonRaw_ = buttonStable_ = pressed;
    buttonChangedAt_ = now;
}

void EspLink::sendButton(uint32_t now)
{
    Text text;
    Hardware::writeEsp(text.append("W:BUTTON:").number(pendingButton_).line());
    buttonSentAt_ = now;
}

void EspLink::sendResult(uint32_t now)
{
    Text text;
    Hardware::writeEsp(text.append(victory_ ? "W:SOLVED:" : "W:FAILED:")
        .number(pendingResult_).line());
    resultSentAt_ = now;
}

void EspLink::report(Result result, uint32_t now)
{
    if (!round_ || result == Result::None) {
        return;
    }
    pendingResult_ = round_;
    victory_ = result == Result::Solved;
    sendResult(now);
}

void EspLink::button(bool pressed, uint32_t now)
{
    if (pressed != buttonRaw_) {
        buttonRaw_ = pressed;
        buttonChangedAt_ = now;
    }

    const uint16_t debounce = pressed ? Config::BUTTON_PRESS_MS : Config::BUTTON_RELEASE_MS;
    if (buttonRaw_ == buttonStable_ || uint32_t(now - buttonChangedAt_) < debounce) {
        return;
    }
    buttonStable_ = buttonRaw_;
    if (buttonStable_) {
        pendingButton_ = Hardware::nextButtonId();
        sendButton(now);
    }
}

void EspLink::handle(const char *line, uint32_t now, uint8_t openMask)
{
    uint32_t buttonId;
    uint64_t resultId;
    uint64_t generation;
    uint16_t seed;
    uint8_t level;
    uint8_t type;
    char event[8];
    uint64_t eventRound;
    uint16_t count;

    if (ModuleBus::parseEvent(line, type, event, eventRound, count, "E:GACK:")) {
        gateway_.acknowledge(type, event, eventRound, count);
    } else if (uartParseId(line, "E:ACK:", buttonId)) {
        if (pendingButton_ == buttonId) {
            pendingButton_ = 0;
        }
    } else if (uartParseId(line, "E:RESULTACK:", resultId)) {
        if (pendingResult_ == resultId) {
            pendingResult_ = 0;
        }
    } else if (uartParseGeneration(line, generation, seed, level)) {
        if (generation != round_) {
            if (!game_.start(seed, static_cast<Difficulty>(level), now, openMask)) {
                return;
            }
            round_ = generation;
            pendingResult_ = 0;
            gateway_.start(round_, seed, static_cast<Difficulty>(level));
            Hardware::debugRound(seed, level, game_.puzzle().targetMask);
        }
        Text text;
        Hardware::writeEsp(text.append("W:GENERATED:").number(generation).line());
    } else if (!strcmp(line, "E:PING")) {
        Hardware::writeEsp("W:PONG\n");
    }
}

void EspLink::update(uint32_t now, uint8_t openMask, bool pressed)
{
    if (Hardware::takeReceiveFault()) {
        // После потери байта исключаем разбор хвоста повреждённой строки.
        reader_.length = 0;
        reader_.overflow = true;
    }

    uint8_t byte;
    for (uint8_t received = 0; received < 64 && Hardware::readEsp(byte); ++received) {
        reader_.feed(char(byte), [this, now, openMask](const char *line) {
            handle(line, now, openMask);
        });
    }

    button(pressed, now);
    if (pendingButton_ && uint32_t(now - buttonSentAt_) >= Config::UART_RETRY_MS) {
        sendButton(now);
    }
    if (pendingResult_ && uint32_t(now - resultSentAt_) >= Config::UART_RETRY_MS) {
        sendResult(now);
    }
}

} // namespace Wires
