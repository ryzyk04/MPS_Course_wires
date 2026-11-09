#pragma once

#include <stdint.h>
#include "game/Puzzle.h"

namespace Wires {

// Временный мост существующего 20-байтового протокола в UART ESP32.
class Gateway {
public:
    void start(uint64_t round, uint16_t seed, Difficulty difficulty);
    void update(uint32_t now);
    void acknowledge(uint8_t type, const char *event, uint64_t round, uint16_t count);

private:
    struct Node {
        uint8_t address;
        uint8_t type;
        uint8_t failures = 0;
        uint8_t diagnostic = 255;
        bool online = false;
        bool ready = false;
        bool solved = false;
        bool resultAcked = false;
        uint16_t errors = 0;
        uint16_t ackedErrors = 0;
        uint32_t errorSentAt = 0;
        uint32_t solvedSentAt = 0;

        Node(uint8_t busAddress, uint8_t puzzleType)
            : address(busAddress), type(puzzleType) {}
    };

    Node nodes_[2] = {Node(0x11, 2), Node(0x12, 3)};
    uint64_t round_ = 0;
    uint16_t seed_ = 0;
    Difficulty difficulty_ = EASY;
    uint32_t lastPoll_ = 0;
    uint8_t nextNode_ = 0;

    void poll(Node &node, uint32_t now);
    void generate(const Node &node);
    void send(const Node &node, const char *event, uint16_t count, uint64_t round);
};

} // namespace Wires
