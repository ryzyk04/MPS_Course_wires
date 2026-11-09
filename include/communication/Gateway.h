#pragma once

#include <PuzzleBus/Master.h>
#include "game/Puzzle.h"

namespace Wires {

// Мост общего PuzzleMaster в существующий UART мастера ESP32.
class Gateway {
public:
    Gateway();
    void start(uint64_t round, uint16_t seed, Difficulty difficulty);
    void update(uint32_t now);
    void acknowledge(uint8_t type, const char *event, uint64_t round, uint16_t count);

private:
    class Transport : public PuzzleBus::Transport {
    public:
        bool write(uint8_t address, const uint8_t *data, size_t size) override;
        size_t read(uint8_t address, uint8_t *data, size_t capacity) override;
    };

    struct Node {
        uint8_t address;
        uint8_t type;
        bool scheduled = false;
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

    Transport transport_;
    PuzzleBus::PuzzleMaster master_;
    Node nodes_[2] = {Node(0x11, 2), Node(0x12, 3)};
    PuzzleBus::RoundConfig round_;
    uint32_t now_ = 0;
    uint32_t lastPoll_ = 0;

    static void onEvent(const PuzzleBus::Event &event, void *context);
    void accept(const PuzzleBus::Event &event);
    void synchronize();
    void send(const Node &node, const char *event, uint16_t count, uint64_t round);
};

} // namespace Wires
