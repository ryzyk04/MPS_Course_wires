#pragma once

#include "game/Game.h"
#include "Gateway.h"
#include "uart_protocol.h"

namespace Wires {

// Разбор строк, управление раундом и подтверждаемые события UART.
class EspLink {
public:
    EspLink(Game &game, Gateway &gateway)
        : game_(game), gateway_(gateway)
    {
    }

    void begin(bool buttonPressed, uint32_t now);
    void update(uint32_t now, uint8_t openMask, bool buttonPressed);
    void report(Result result, uint32_t now);

    uint64_t round() const
    {
        return round_;
    }

private:
    Game &game_;
    Gateway &gateway_;
    UartLineReader reader_;
    uint64_t round_ = 0;
    uint64_t pendingResult_ = 0;
    bool victory_ = false;
    uint32_t resultSentAt_ = 0;
    uint32_t pendingButton_ = 0;
    uint32_t buttonSentAt_ = 0;
    uint32_t buttonChangedAt_ = 0;
    bool buttonRaw_ = false;
    bool buttonStable_ = false;

    void handle(const char *line, uint32_t now, uint8_t openMask);
    void button(bool pressed, uint32_t now);
    void sendResult(uint32_t now);
    void sendButton(uint32_t now);
};

} // namespace Wires
