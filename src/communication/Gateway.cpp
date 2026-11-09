#include "communication/Gateway.h"
#include "communication/Text.h"
#include "drivers/Board.h"
#include "module_bus.h"
#include "Config.h"

#include <string.h>

namespace Wires {

void Gateway::send(const Node &node, const char *event, uint16_t count, uint64_t round)
{
    Text text;
    Hardware::writeEsp(text.append("G:").number(node.type).append(":").append(event)
        .append(":").number(round).append(":").number(count).line());
}

void Gateway::generate(const Node &node)
{
    ModuleBus::Frame frame;
    frame.type = node.type;
    frame.operation = ModuleBus::GENERATE;
    frame.round = round_;
    frame.seed = seed_;
    frame.difficulty = difficulty_;
    uint8_t packet[ModuleBus::FRAME_SIZE];
    ModuleBus::encode(frame, packet);
    Hardware::writeTwi(node.address, packet, sizeof(packet));
}

void Gateway::start(uint64_t round, uint16_t seed, Difficulty difficulty)
{
    round_ = round;
    seed_ = seed;
    difficulty_ = difficulty;

    for (Node &node : nodes_) {
        node.ready = false;
        node.solved = false;
        node.resultAcked = false;
        node.errors = 0;
        node.ackedErrors = 0;
        node.errorSentAt = 0;
        node.solvedSentAt = 0;
        if (node.online) {
            generate(node);
        }
    }
}

void Gateway::acknowledge(uint8_t type, const char *event, uint64_t round, uint16_t count)
{
    if (round != round_) {
        return;
    }

    for (Node &node : nodes_) {
        if (node.type != type) {
            continue;
        }
        if (!strcmp(event, "ERROR") && count <= node.errors && count > node.ackedErrors) {
            node.ackedErrors = count;
        }
        if (!strcmp(event, "SOLVED")) {
            node.resultAcked = true;
        }
    }
}

void Gateway::poll(Node &node, uint32_t now)
{
    uint8_t packet[ModuleBus::FRAME_SIZE];
    const bool received = Hardware::readTwi(node.address, packet, sizeof(packet));
    ModuleBus::Frame frame;
    const bool decoded = received && ModuleBus::decode(packet, sizeof(packet), frame);
    const bool valid = decoded && frame.operation == ModuleBus::STATUS && frame.type == node.type;
    const uint8_t diagnostic = !received ? 1 : !decoded ? 2 : !valid ? 3 : 0;

    if (diagnostic != node.diagnostic) {
        node.diagnostic = diagnostic;
        const Hardware::Diagnostic messages[] = {
            Hardware::Diagnostic::NodeOk, Hardware::Diagnostic::NodeMissing,
            Hardware::Diagnostic::NodeInvalid, Hardware::Diagnostic::NodeWrong
        };
        Hardware::debug(messages[diagnostic], node.type);
    }

    if (!valid) {
        if (node.failures < Config::BUS_FAILURE_LIMIT) {
            ++node.failures;
        }
        if (node.online && node.failures >= Config::BUS_FAILURE_LIMIT) {
            node.online = false;
            send(node, "OFFLINE", 0, round_);
        }
        return;
    }

    node.failures = 0;
    if (!node.online) {
        node.online = true;
        send(node, "ONLINE", 0, frame.round);
    }

    if (!round_) {
        return;
    }
    if (frame.round != round_ || frame.seed != seed_ || frame.difficulty != difficulty_) {
        node.ready = false;
        generate(node);
        return;
    }

    if (!node.ready) {
        node.ready = true;
        send(node, "READY", 0, frame.round);
    }
    if (frame.errors > node.errors) {
        node.errors = frame.errors;
        node.errorSentAt = now - Config::UART_RETRY_MS;
    }

    const bool solved = frame.state == ModuleBus::SOLVED;
    if (solved && !node.solved) {
        node.solvedSentAt = now - Config::UART_RETRY_MS;
    }
    node.solved = solved;
}

void Gateway::update(uint32_t now)
{
    if (uint32_t(now - lastPoll_) >= Config::BUS_POLL_MS) {
        lastPoll_ = now;
        poll(nodes_[nextNode_], now);
        nextNode_ ^= 1;
    }

    if (!round_) {
        return;
    }
    for (Node &node : nodes_) {
        if (node.errors > node.ackedErrors &&
            uint32_t(now - node.errorSentAt) >= Config::UART_RETRY_MS) {
            send(node, "ERROR", node.errors, round_);
            node.errorSentAt = now;
        }
        if (node.solved && !node.resultAcked &&
            uint32_t(now - node.solvedSentAt) >= Config::UART_RETRY_MS) {
            send(node, "SOLVED", 0, round_);
            node.solvedSentAt = now;
        }
    }
}

} // namespace Wires
