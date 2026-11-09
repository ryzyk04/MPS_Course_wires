#include <stdint.h>
#include <algorithm>
#include <cstring>
#include <iostream>

#include "Test.h"
#include "MockBoard.h"
#include "Config.h"
#include "game/Game.h"
#include "game/Display.h"
#include "communication/EspLink.h"
#include "communication/Text.h"
#include "drivers/Ring.h"
#include "TestBus.h"

using namespace Wires;

void sharedTests();

namespace {

void makeReady(Game &game, uint16_t seed = 42, Difficulty difficulty = EASY)
{
    CHECK(game.start(seed, difficulty, 0, 0));
    CHECK(game.update(0, 0) == Result::None);
    game.update(Config::READY_MS - 1, 0);
    CHECK(game.state() == State::WaitingForWires);
    game.update(Config::READY_MS, 0);
    CHECK(game.state() == State::Playing);
}

Result cut(Game &game, uint8_t mask, uint32_t now)
{
    game.update(now, mask);
    return game.update(now + Config::WIRE_DEBOUNCE_MS, mask);
}

uint8_t firstBit(uint8_t mask)
{
    return uint8_t(mask & uint8_t(-mask));
}

struct Fixture {
    Game game;
    Gateway gateway;
    EspLink link{game, gateway};

    Fixture()
    {
        Mock::reset();
        link.begin(false, 0);
    }

    void command(const std::string &line, uint32_t now = 0)
    {
        Mock::inject(line + "\n");
        link.update(now, 0, false);
    }
};

void logicTests()
{
    run("generation matches AVR random sequence and preserves output on invalid input", [] {
        struct Expected {
            uint16_t seed;
            uint8_t count, mask;
            uint8_t colors[5];
        };
        const Expected expected[] = {
            {0, 4, 27, {8, 6, 8, 8, 0}},
            {1, 4, 15, {7, 3, 6, 6, 0}},
            {42, 3, 28, {2, 4, 0, 0, 0}},
            {1234, 5, 31, {7, 8, 6, 6, 0}},
            {9999, 5, 31, {6, 4, 6, 2, 6}}
        };
        for (const Expected &entry : expected) {
            Puzzle puzzle;
            CHECK(generatePuzzle(entry.seed, EASY, puzzle));
            CHECK(puzzle.count == entry.count);
            CHECK(puzzle.activeMask == entry.mask);
            for (uint8_t index = 0; index < entry.count; ++index) {
                CHECK(puzzle.colors[index] == entry.colors[index]);
            }
            const Puzzle before = puzzle;
            CHECK(!generatePuzzle(10000, EASY, puzzle));
            CHECK(!generatePuzzle(0, Difficulty(255), puzzle));
            CHECK(std::memcmp(&puzzle, &before, sizeof(Puzzle)) == 0);
        }
    });

    run("all 30000 seed and difficulty combinations are deterministic", [] {
        for (uint8_t difficulty = 0; difficulty < 3; ++difficulty) {
            for (uint16_t seed = 0; seed <= 9999; ++seed) {
                Puzzle first, second;
                CHECK(generatePuzzle(seed, Difficulty(difficulty), first));
                CHECK(generatePuzzle(seed, Difficulty(difficulty), second));
                CHECK(first.count >= 3 && first.count <= 5);
                CHECK(first.activeMask == second.activeMask);
                CHECK(first.targetMask == second.targetMask);
                CHECK(first.targetMask != 0);
                CHECK(!(first.targetMask & uint8_t(~first.activeMask)));
                CHECK(__builtin_popcount(unsigned(first.targetMask)) <= 2);
                for (uint8_t index = 0; index < first.count; ++index) {
                    CHECK(first.colors[index] == second.colors[index]);
                }
            }
        }
    });

    run("2082024 layouts match fingerprints captured from the original rules", [] {
        struct Expected { uint8_t difficulty; uint16_t seed; uint64_t hash; };
        const Expected expected[] = {
            {0,0,14553892786399554001ULL}, {0,1234,16850913193013655747ULL},
            {0,9876,994132309882401443ULL}, {0,2357,6994243124478585070ULL},
            {0,4173,8346988581162835846ULL}, {0,6098,14553892786399554001ULL},
            {0,1111,6994243124478585070ULL}, {1,0,14242276687577165817ULL},
            {1,1234,15660193863003659089ULL}, {1,9876,9791349094001917241ULL},
            {1,2357,15986289176551118085ULL}, {1,4173,5718536825725166889ULL},
            {1,6098,17821627381138963549ULL}, {1,1111,4896155768670276749ULL},
            {2,0,16875183680783941791ULL}, {2,1234,11669036222040726737ULL},
            {2,9876,6679910096474310853ULL}, {2,2357,13387601340201012788ULL},
            {2,4173,13436726337059673804ULL}, {2,6098,6369464388478908195ULL},
            {2,1111,4545311114893593734ULL}
        };
        for (const Expected &entry : expected) {
            Puzzle puzzle;
            puzzle.seed = entry.seed;
            puzzle.difficulty = Difficulty(entry.difficulty);
            uint16_t seed = entry.seed;
            for (int index = 3; index >= 0; --index) {
                puzzle.digits[index] = seed % 10;
                seed /= 10;
            }
            uint64_t hash = 1469598103934665603ULL;
            uint32_t cases = 0;
            for (uint8_t mask = 1; mask < 32; ++mask) {
                const uint8_t count = uint8_t(__builtin_popcount(unsigned(mask)));
                if (count < 3) {
                    continue;
                }
                puzzle.activeMask = mask;
                puzzle.count = count;
                uint8_t active = 0;
                for (uint8_t position = 0; position < 5; ++position) {
                    if (mask & wireBit(position)) {
                        puzzle.positions[active++] = position;
                    }
                }
                uint32_t layouts = 1;
                for (uint8_t index = 0; index < count; ++index) { layouts *= COLOR_COUNT; }
                for (uint32_t layout = 0; layout < layouts; ++layout) {
                    uint32_t value = layout;
                    for (uint8_t index = 0; index < count; ++index) {
                        puzzle.colors[index] = uint8_t(value % COLOR_COUNT);
                        value /= COLOR_COUNT;
                    }
                    const uint8_t result = chooseTargets(puzzle);
                    CHECK(result != 0 && !(result & uint8_t(~mask)));
                    CHECK(__builtin_popcount(unsigned(result)) <= 2);
                    hash ^= result;
                    hash *= 1099511628211ULL;
                    ++cases;
                }
            }
            CHECK(cases == 99144);
            CHECK(hash == entry.hash);
        }
    });

    run("game startup readiness debounce outcome priority and terminal states", [] {
        Game game;
        CHECK(game.state() == State::WaitingForCommand);
        CHECK(game.update(1000, 31) == Result::None);
        CHECK(game.start(42, EASY, 0, 31));
        game.update(1000, 31);
        CHECK(game.state() == State::WaitingForWires);
        game.update(1100, 0);
        game.update(1249, 0);
        CHECK(game.state() == State::WaitingForWires);
        game.update(1250, 0);
        CHECK(game.state() == State::Playing);

        const uint8_t targets = game.puzzle().targetMask;
        CHECK(cut(game, targets, 1400) == Result::Solved);
        CHECK(game.update(2000, 31) == Result::None);
        CHECK(game.state() == State::Solved);

        makeReady(game);
        const uint8_t forbidden = firstBit(game.puzzle().activeMask &
                                          uint8_t(~game.puzzle().targetMask));
        CHECK(forbidden != 0);
        CHECK(cut(game, forbidden | game.puzzle().targetMask, 300) == Result::Failed);
        CHECK(game.update(1000, 0) == Result::None);
        CHECK(game.state() == State::Failed);
    });

    run("game bounce inactive cuts and pending forbidden cut", [] {
        Game game;
        makeReady(game);
        game.update(200, game.puzzle().targetMask);
        game.update(239, 0);
        game.update(300, 0);
        CHECK(game.cutMask() == 0);

        const uint8_t inactive = uint8_t(~game.puzzle().activeMask) & 31;
        CHECK(cut(game, inactive, 400) == Result::None);
        CHECK(game.cutMask() == 0);

        const uint8_t forbidden = firstBit(game.puzzle().activeMask &
                                          uint8_t(~game.puzzle().targetMask));
        game.update(500, game.puzzle().targetMask);
        game.update(530, game.puzzle().targetMask | forbidden);
        CHECK(game.update(540, game.puzzle().targetMask | forbidden) == Result::None);
        CHECK(game.update(570, game.puzzle().targetMask | forbidden) == Result::Failed);
    });

    run("game two-wire solutions work in either order and time wraps", [] {
        uint16_t seed = 0;
        Puzzle puzzle;
        do {
            CHECK(generatePuzzle(seed++, EASY, puzzle));
        } while (__builtin_popcount(unsigned(puzzle.targetMask)) != 2);

        for (bool reverse : {false, true}) {
            Game game;
            makeReady(game, seed - 1);
            const uint8_t first = firstBit(game.puzzle().targetMask);
            const uint8_t second = game.puzzle().targetMask ^ first;
            CHECK(cut(game, reverse ? second : first, 200) == Result::None);
            CHECK(cut(game, first | second, 300) == Result::Solved);
        }

        Game game;
        CHECK(game.start(42, EASY, UINT32_MAX - 74, 0));
        game.update(UINT32_MAX - 74, 0);
        game.update(74, 0);
        CHECK(game.state() == State::WaitingForWires);
        game.update(75, 0);
        CHECK(game.state() == State::Playing);
        CHECK(cut(game, game.puzzle().targetMask, UINT32_MAX - 19) == Result::Solved);
    });
}

void busTests()
{
    run("UART split lines overflow parser fault and ping", [] {
        Fixture fixture;
        Mock::inject("E:PI");
        fixture.link.update(0, 0, false);
        CHECK(Mock::output.empty());
        Mock::inject("NG\r\n");
        fixture.link.update(1, 0, false);
        CHECK(Mock::output == "W:PONG\n");
        Mock::output.clear();
        Mock::inject(std::string(60, 'X') + "E:PING\n");
        fixture.link.update(2, 0, false);
        fixture.link.update(3, 0, false);
        CHECK(Mock::output.empty());
        fixture.command("E:PING");
        CHECK(Mock::output == "W:PONG\n");
        Mock::output.clear();
        Mock::inject(std::string("E:PING\0junk\n", 12));
        fixture.link.update(4, 0, false);
        CHECK(Mock::output.empty());
        Mock::fault = true;
        Mock::inject("E:PING\nE:PING\n");
        fixture.link.update(4, 0, false);
        CHECK(Mock::output == "W:PONG\n");
    });

    run("UART generation full width round repeated ID and malformed inputs", [] {
        Fixture fixture;
        fixture.command("E:GENERATE:18446744073709551615:9999:2");
        CHECK(fixture.link.round() == UINT64_MAX);
        CHECK(fixture.game.puzzle().seed == 9999);
        CHECK(Mock::output == "W:GENERATED:18446744073709551615\n");
        fixture.game.update(0, 0);
        fixture.game.update(150, 0);
        fixture.command("E:GENERATE:18446744073709551615:9999:2");
        CHECK(fixture.game.state() == State::Playing);
        for (const char *bad : {"E:GENERATE:0:42:0", "E:GENERATE:-1:42:0",
             "E:GENERATE:18446744073709551616:42:0", "E:GENERATE:12x:42:0",
             "E:GENERATE:5:10000:0", "E:GENERATE:5:42:3", "E:GENERATE:5:42",
             "E:GENERATE:5:42:0:1"}) {
            Mock::output.clear();
            fixture.command(bad);
            CHECK(Mock::output.empty());
            CHECK(fixture.link.round() == UINT64_MAX);
        }
    });

    run("UART button debounce release hold retry ACK and time wrap", [] {
        Fixture fixture;
        fixture.link.update(100, 0, true);
        fixture.link.update(139, 0, true);
        CHECK(Mock::output.empty());
        fixture.link.update(140, 0, true);
        CHECK(Mock::output == "W:BUTTON:101\n");
        fixture.link.update(639, 0, true);
        CHECK(Mock::output == "W:BUTTON:101\n");
        fixture.link.update(640, 0, true);
        CHECK(Mock::output == "W:BUTTON:101\nW:BUTTON:101\n");
        fixture.command("E:ACK:100", 641);
        fixture.command("E:ACK:101", 642);
        Mock::output.clear();
        fixture.link.update(1500, 0, false);
        fixture.link.update(1510, 0, false);
        fixture.link.update(1520, 0, true);
        fixture.link.update(1560, 0, true);
        CHECK(Mock::output == "W:BUTTON:102\n");

        fixture.command("E:ACK:102");
        fixture.link.update(UINT32_MAX - 40, 0, false);
        fixture.link.update(UINT32_MAX - 30, 0, false);
        fixture.link.update(UINT32_MAX - 19, 0, true);
        Mock::output.clear();
        fixture.link.update(19, 0, true);
        CHECK(Mock::output.empty());
        fixture.link.update(20, 0, true);
        CHECK(Mock::output == "W:BUTTON:103\n");
    });

    run("UART result retry ACK and new generation cancel pending result", [] {
        Fixture fixture;
        fixture.command("E:GENERATE:501:42:0");
        Mock::output.clear();
        fixture.link.report(Result::Solved, 100);
        CHECK(Mock::output == "W:SOLVED:501\n");
        fixture.link.update(599, 0, false);
        CHECK(Mock::output == "W:SOLVED:501\n");
        fixture.link.update(600, 0, false);
        CHECK(Mock::output == "W:SOLVED:501\nW:SOLVED:501\n");
        fixture.command("E:RESULTACK:500", 601);
        fixture.command("E:RESULTACK:501", 602);
        Mock::output.clear();
        fixture.link.update(1500, 0, false);
        CHECK(Mock::output.empty());
        fixture.link.report(Result::Failed, 1501);
        CHECK(Mock::output.empty());
        CHECK(fixture.link.status().state == PuzzleBus::State::Solved);
        fixture.command("E:GENERATE:502:0:2", 1502);
        Mock::output.clear();
        fixture.link.update(3000, 0, false);
        CHECK(Mock::output.empty());
    });

    run("gateway discovery relay cumulative errors ACK solved and disconnection", [] {
        Fixture fixture;
        TestBus::Frame status;
        status.type = TestBus::SIMON;
        uint8_t packet[TestBus::FRAME_SIZE];
        TestBus::encode(status, packet);
        Mock::replies[0x11] = {packet, packet + sizeof(packet)};
        fixture.gateway.update(25);
        CHECK(Mock::output == "G:2:ONLINE:0:0\n");

        fixture.command("E:GENERATE:4294967297:42:2", 30);
        fixture.gateway.update(50);
        fixture.gateway.update(75);
        CHECK(Mock::writes.count(0x11) == 1);
        TestBus::Frame generate;
        const auto &sent = Mock::writes[0x11];
        CHECK(TestBus::decode(sent.data(), sent.size(), generate));
        CHECK(generate.round == 4294967297ULL);
        CHECK(generate.operation == TestBus::GENERATE);

        status.round = 4294967297ULL;
        status.seed = 42;
        status.difficulty = 2;
        status.state = TestBus::PLAYING;
        status.errors = 3;
        TestBus::encode(status, packet);
        Mock::replies[0x11] = {packet, packet + sizeof(packet)};
        Mock::output.clear();
        fixture.gateway.update(100);
        fixture.gateway.update(125);
        CHECK(Mock::output.find("G:2:READY:4294967297:0\n") != std::string::npos);
        CHECK(Mock::output.find("G:2:ERROR:4294967297:3\n") != std::string::npos);
        fixture.command("E:GACK:2:ERROR:4294967297:3", 80);
        Mock::output.clear();
        fixture.gateway.update(575);
        CHECK(Mock::output.find("ERROR") == std::string::npos);

        status.state = TestBus::SOLVED;
        TestBus::encode(status, packet);
        Mock::replies[0x11] = {packet, packet + sizeof(packet)};
        fixture.gateway.update(600);
        CHECK(Mock::output.find("G:2:SOLVED:4294967297:0\n") != std::string::npos);
        fixture.command("E:GACK:2:SOLVED:4294967297:0", 601);
        Mock::output.clear();
        Mock::replies.clear();
        for (uint32_t now = 625; now <= 850; now += 25) {
            fixture.gateway.update(now);
        }
        CHECK(Mock::output.find("G:2:OFFLINE:4294967297:0\n") != std::string::npos);
        CHECK(Mock::output.find("SOLVED") == std::string::npos);
    });

    run("shared frame CRC bit corruption and exact size", [] {
        TestBus::Frame frame;
        frame.type = TestBus::LABYRINTH;
        frame.operation = TestBus::GENERATE;
        frame.round = UINT64_MAX;
        frame.seed = 9999;
        frame.difficulty = 2;
        uint8_t packet[TestBus::FRAME_SIZE];
        TestBus::encode(frame, packet);
        TestBus::Frame decoded;
        CHECK(TestBus::decode(packet, sizeof(packet), decoded));
        CHECK(decoded.round == UINT64_MAX);
        CHECK(!TestBus::decode(packet, sizeof(packet) - 1, decoded));
        for (uint8_t byte = 0; byte < sizeof(packet); ++byte) {
            for (uint8_t bit = 0; bit < 8; ++bit) {
                packet[byte] ^= uint8_t(1U << bit);
                CHECK(!TestBus::decode(packet, sizeof(packet), decoded));
                packet[byte] ^= uint8_t(1U << bit);
            }
        }
    });

    run("bounded text formatting ring wrap full empty and display retries", [] {
        Text text;
        CHECK(std::string(text.append("ID:").number(UINT64_MAX).line()) ==
              "ID:18446744073709551615\n");
        Text longText;
        CHECK(std::string(longText.append(std::string(47, 'x').c_str()).line()).empty());

        Ring<4> ring;
        uint8_t value = 42;
        CHECK(!ring.pop(value) && value == 42);
        for (unsigned iteration = 0; iteration < 1000; ++iteration) {
            CHECK(ring.push(1) && ring.push(2) && ring.push(3));
            CHECK(!ring.push(4));
            CHECK(ring.pop(value) && value == 1);
            CHECK(ring.pop(value) && value == 2);
            CHECK(ring.pop(value) && value == 3);
            CHECK(ring.empty());
        }

        Mock::reset();
        Game game;
        Display display;
        Mock::showSuccess = false;
        display.update(game, 0);
        CHECK(Mock::showCalls == 1);
        Mock::showSuccess = true;
        display.update(game, 1);
        display.update(game, 2);
        CHECK(Mock::showCalls == 2);
        CHECK(Mock::pixels == std::vector<uint8_t>(24, 0));
        makeReady(game);
        display.update(game, 200);
        CHECK(Mock::pixels.size() == 24);
        CHECK(Mock::pixels[Config::DIFFICULTY_PIXEL * 3] == Config::STATUS_BRIGHTNESS);
    });
}

} // namespace

int main(int argc, char **argv)
{
    try {
        const std::string group = argc > 1 ? argv[1] : "all";
        if (group == "all" || group == "logic") { logicTests(); }
        if (group == "all" || group == "bus") { busTests(); sharedTests(); }
        std::cout << "Passed " << cases << " groups, " << checks << " checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << "\n";
        return 1;
    }
}
