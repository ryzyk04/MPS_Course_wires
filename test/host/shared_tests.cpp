#include "Test.h"
#include "MockBoard.h"
#include "communication/EspLink.h"
#include "Config.h"

#include <PuzzleBus/Node.h>
#include <cstring>

namespace {

size_t occurrences(const std::string &text, const std::string &needle)
{
    size_t count = 0, offset = 0;
    while ((offset = text.find(needle, offset)) != std::string::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

// Two real library nodes represent separate MCUs. The production gateway and
// EspLink run unchanged. Faults affect transport, not the protocol implementation.
struct System {
    PuzzleBus::PuzzleNode simon{PuzzleBus::PuzzleType::Simon};
    PuzzleBus::PuzzleNode labyrinth{PuzzleBus::PuzzleType::Labyrinth};
    Wires::Game game;
    Wires::Gateway gateway;
    Wires::EspLink link{game, gateway};
    uint32_t now = 0;
    bool missingSimon = false;
    bool corrupt = false;
    bool dropWrites = false;
    bool wrongType = false;
    unsigned transmissions = 0;

    System()
    {
        Mock::reset();
        link.begin(false, now);
        Mock::writeHook = [this](uint8_t address, const uint8_t *data, uint8_t size) {
            CHECK(size == PuzzleBus::FRAME_SIZE);
            ++transmissions;
            if (dropWrites) { return false; }
            PuzzleBus::Frame frame;
            CHECK(PuzzleBus::decode(data, size, frame) == PuzzleBus::DecodeResult::Ok);
            auto &node = address == 0x11 ? simon : labyrinth;
            const bool accepted = node.receive(frame);
            PuzzleBus::Command command;
            if (node.takeCommand(command) && command.operation == PuzzleBus::Operation::Generate) {
                CHECK(node.ready());
            }
            return accepted;
        };
        Mock::readHook = [this](uint8_t address, uint8_t *data, uint8_t size) {
            CHECK(size == PuzzleBus::FRAME_SIZE);
            if (missingSimon && address == 0x11) { return false; }
            PuzzleBus::Frame frame;
            frame.status = address == 0x11 ? simon.status() : labyrinth.status();
            if (wrongType && address == 0x11) {
                frame.status.type = PuzzleBus::PuzzleType::Labyrinth;
            }
            CHECK(PuzzleBus::encode(frame, data, size));
            if (corrupt) { data[size - 1] ^= 1; }
            return true;
        };
    }

    ~System() { Mock::readHook = {}; Mock::writeHook = {}; }

    void ticks(unsigned count)
    {
        for (unsigned i = 0; i < count; ++i) {
            now += Wires::Config::BUS_POLL_MS;
            gateway.update(now);
        }
    }

    void command(const std::string &line)
    {
        Mock::inject(line + "\n");
        link.update(now, 0, false);
    }
};

} // namespace

void sharedTests()
{
    run("shared bus discovery two nodes ready errors retries ACK and solved", [] {
        System system;
        system.ticks(2);
        CHECK(Mock::output.find("G:2:ONLINE:0:0\n") != std::string::npos);
        CHECK(Mock::output.find("G:3:ONLINE:0:0\n") != std::string::npos);
        system.command("E:GENERATE:4294967297:9999:2");
        system.ticks(4);
        CHECK(system.simon.status().round.id == 4294967297ULL);
        CHECK(system.labyrinth.status().state == PuzzleBus::State::Playing);
        CHECK(Mock::output.find("G:2:READY:4294967297:0\n") != std::string::npos);
        CHECK(Mock::output.find("G:3:READY:4294967297:0\n") != std::string::npos);

        system.simon.smallError(); system.simon.smallError();
        Mock::output.clear();
        system.ticks(2);
        CHECK(occurrences(Mock::output, "G:2:ERROR:4294967297:2\n") == 1);
        system.ticks(20); // Lost ACK causes retransmission, not a new counter.
        CHECK(occurrences(Mock::output, "G:2:ERROR:4294967297:2\n") == 2);
        system.command("E:GACK:2:ERROR:4294967296:2");
        system.command("E:GACK:2:ERROR:4294967297:3");
        system.command("E:GACK:2:ERROR:4294967297:2");
        Mock::output.clear();
        system.ticks(20);
        CHECK(Mock::output.find("ERROR") == std::string::npos);

        system.command("E:GACK:2:SOLVED:4294967297:0"); // Premature ACK is ignored.
        CHECK(system.simon.finish(true));
        system.ticks(2);
        CHECK(occurrences(Mock::output, "G:2:SOLVED:4294967297:0\n") == 1);
        system.ticks(20);
        CHECK(occurrences(Mock::output, "G:2:SOLVED:4294967297:0\n") == 2);
        system.command("E:GACK:2:SOLVED:4294967297:0");
        Mock::output.clear();
        system.ticks(20);
        CHECK(Mock::output.find("SOLVED") == std::string::npos);
    });

    run("shared bus lost commands CRC offline reconnect wrong type and reboot", [] {
        System system;
        system.ticks(2);
        system.dropWrites = true;
        system.command("E:GENERATE:1:0:0");
        system.ticks(6);
        CHECK(system.simon.status().state == PuzzleBus::State::Waiting);
        CHECK(system.transmissions >= 6);
        system.dropWrites = false;
        system.ticks(4);
        CHECK(system.simon.status().state == PuzzleBus::State::Playing);

        system.corrupt = true;
        Mock::output.clear();
        system.ticks(6);
        CHECK(Mock::output.find("G:2:OFFLINE:1:0\n") != std::string::npos);
        CHECK(Mock::output.find("G:3:OFFLINE:1:0\n") != std::string::npos);
        system.corrupt = false;
        system.ticks(4);
        CHECK(Mock::output.find("G:2:ONLINE:1:0\n") != std::string::npos);

        system.wrongType = true;
        Mock::output.clear();
        system.ticks(6);
        CHECK(Mock::output.find("G:2:OFFLINE:1:0\n") != std::string::npos);
        system.wrongType = false;
        system.simon = PuzzleBus::PuzzleNode(PuzzleBus::PuzzleType::Simon);
        system.ticks(6);
        CHECK(system.simon.status().round.id == 1);
        CHECK(system.simon.status().state == PuzzleBus::State::Playing);
        CHECK(Mock::output.find("G:2:ONLINE:0:0\n") != std::string::npos);
    });

    run("shared bus replacement while delivery uncertain converges to latest round", [] {
        System system;
        system.ticks(2);
        system.command("E:GENERATE:1:0:0");
        system.ticks(4);
        system.corrupt = true;
        system.command("E:GENERATE:2:42:1");
        system.ticks(2); // Writes arrive, but master cannot observe their outcome.
        CHECK(system.simon.status().round.id == 2);
        system.command("E:GENERATE:3:9999:2");
        system.corrupt = false;
        system.ticks(8);
        CHECK(system.simon.status().round.id == 3);
        CHECK(system.labyrinth.status().round.id == 3);
        CHECK(system.simon.status().round.seed == 9999);
        CHECK(system.simon.status().round.difficulty == 2);
        system.command("E:GENERATE:2:42:1"); // Old round cannot restart any game.
        CHECK(system.link.status().round.id == 3);
        system.command("E:GENERATE:3:0:0"); // Same ID, different parameters.
        CHECK(system.link.status().round.seed == 9999);
        CHECK(system.link.status().state == PuzzleBus::State::Playing);
        system.link.report(Wires::Result::Failed, system.now);
        CHECK(system.link.status().state == PuzzleBus::State::Failed);
        CHECK(system.link.status().bigErrors == 1);
        system.link.report(Wires::Result::Failed, system.now + 1);
        CHECK(system.link.status().bigErrors == 1);
    });

    run("shared bus polling and retry survive uint32 time wrap", [] {
        System system;
        system.now = UINT32_MAX - 75;
        system.ticks(2);
        system.command("E:GENERATE:18446744073709551615:0:0");
        system.ticks(6);
        CHECK(system.simon.status().round.id == UINT64_MAX);
        CHECK(system.labyrinth.status().round.id == UINT64_MAX);
        system.simon.smallError();
        system.ticks(2);
        CHECK(Mock::output.find("G:2:ERROR:18446744073709551615:1\n") != std::string::npos);
        system.missingSimon = true;
        system.ticks(6);
        CHECK(Mock::output.find("G:2:OFFLINE:18446744073709551615:0\n") != std::string::npos);
    });
}
