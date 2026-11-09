#include "communication/Gateway.h"
#include "communication/Text.h"
#include "drivers/Board.h"
#include "Config.h"

#include <string.h>

namespace Wires {
namespace {

PuzzleBus::MasterConfig masterConfig()
{
    PuzzleBus::MasterConfig config;
    // Внешний планировщик сохраняет один адрес каждые BUS_POLL_MS.
    config.pollIntervalMs = 0;
    config.failuresBeforeOffline = Config::BUS_FAILURE_LIMIT;
    return config;
}

} // namespace

Gateway::Gateway() : master_(transport_, masterConfig())
{
    for (const Node &node : nodes_) {
        master_.addModule(node.address);
    }
    master_.setEventHandler(onEvent, this);
}

bool Gateway::Transport::write(uint8_t address, const uint8_t *data, size_t size)
{
    return size == PuzzleBus::FRAME_SIZE && Hardware::writeTwi(address, data, uint8_t(size));
}

size_t Gateway::Transport::read(uint8_t address, uint8_t *data, size_t capacity)
{
    if (capacity < PuzzleBus::FRAME_SIZE ||
        !Hardware::readTwi(address, data, PuzzleBus::FRAME_SIZE)) {
        return 0;
    }

    PuzzleBus::Frame frame;
    const uint8_t expected = address == 0x11 ? 2 : 3;
    // Эти адреса закреплены за конкретными платами. Не запускаем другой тип.
    if (PuzzleBus::decode(data, PuzzleBus::FRAME_SIZE, frame) != PuzzleBus::DecodeResult::Ok ||
        uint8_t(frame.status.type) != expected) {
        return 0;
    }
    return PuzzleBus::FRAME_SIZE;
}

void Gateway::send(const Node &node, const char *event, uint16_t count, uint64_t round)
{
    Text text;
    Hardware::writeEsp(text.append("G:").number(node.type).append(":").append(event)
        .append(":").number(round).append(":").number(count).line());
}

void Gateway::start(uint64_t round, uint16_t seed, Difficulty difficulty)
{
    PuzzleBus::RoundConfig requested;
    requested.id = round;
    requested.seed = seed;
    requested.difficulty = difficulty;
    if (!PuzzleBus::validRound(requested) || requested.id <= round_.id) {
        return;
    }

    round_ = requested;
    for (Node &node : nodes_) {
        node.scheduled = false;
        node.ready = node.solved = node.resultAcked = false;
        node.errors = node.ackedErrors = 0;
        node.errorSentAt = node.solvedSentAt = 0;
    }
    synchronize();
}

void Gateway::synchronize()
{
    if (!round_.id) {
        return;
    }

    // Команды ставятся только после update(), не внутри callback библиотеки.
    for (Node &node : nodes_) {
        const PuzzleBus::Module *module = master_.module(node.address);
        if (!module || !module->online) {
            continue;
        }
        const bool matches = PuzzleBus::sameRound(module->status.round, round_);
        if (!matches) {
            node.ready = false;
        }
        if (!node.scheduled || (!matches && !master_.commandPending(node.address))) {
            node.scheduled = master_.start(node.address, round_);
        }
    }
}

void Gateway::onEvent(const PuzzleBus::Event &event, void *context)
{
    static_cast<Gateway *>(context)->accept(event);
}

void Gateway::accept(const PuzzleBus::Event &event)
{
    for (Node &node : nodes_) {
        if (node.address != event.module.address) {
            continue;
        }

        if (event.kind == PuzzleBus::EventKind::Online) {
            send(node, "ONLINE", 0, event.module.status.round.id);
        } else if (event.kind == PuzzleBus::EventKind::Offline) {
            node.ready = false;
            send(node, "OFFLINE", 0, round_.id);
        }

        if (!round_.id || !PuzzleBus::sameRound(event.module.status.round, round_)) {
            return;
        }
        if (event.kind == PuzzleBus::EventKind::StatusReceived &&
            event.module.status.state != PuzzleBus::State::Waiting &&
            event.module.status.state != PuzzleBus::State::Preparing && !node.ready) {
            node.ready = true;
            send(node, "READY", 0, round_.id);
        }
        if (event.kind == PuzzleBus::EventKind::SmallErrors &&
            event.module.status.smallErrors > node.errors) {
            node.errors = event.module.status.smallErrors;
            node.errorSentAt = now_ - Config::UART_RETRY_MS;
        }
        if (event.kind == PuzzleBus::EventKind::Completed &&
            event.module.status.state == PuzzleBus::State::Solved) {
            node.solved = true;
            node.solvedSentAt = now_ - Config::UART_RETRY_MS;
        }
        return;
    }
}

void Gateway::acknowledge(uint8_t type, const char *event, uint64_t round, uint16_t count)
{
    if (!event || round != round_.id) {
        return;
    }

    for (Node &node : nodes_) {
        if (node.type != type) {
            continue;
        }
        if (!strcmp(event, "ERROR") && count <= node.errors && count > node.ackedErrors) {
            node.ackedErrors = count;
        }
        if (!strcmp(event, "SOLVED") && node.solved) {
            node.resultAcked = true;
        }
    }
}

void Gateway::update(uint32_t now)
{
    now_ = now;
    if (uint32_t(now - lastPoll_) >= Config::BUS_POLL_MS) {
        lastPoll_ = now;
        master_.update(now);
        synchronize();
    }

    if (!round_.id) {
        return;
    }
    for (Node &node : nodes_) {
        if (node.errors > node.ackedErrors &&
            uint32_t(now - node.errorSentAt) >= Config::UART_RETRY_MS) {
            send(node, "ERROR", node.errors, round_.id);
            node.errorSentAt = now;
        }
        if (node.solved && !node.resultAcked &&
            uint32_t(now - node.solvedSentAt) >= Config::UART_RETRY_MS) {
            send(node, "SOLVED", 0, round_.id);
            node.solvedSentAt = now;
        }
    }
}

} // namespace Wires
