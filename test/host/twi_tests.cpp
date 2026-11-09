#include <stdint.h>
#include <iostream>

#include "Test.h"
#include "Config.h"
#include "drivers/Board.h"
#include <avr/io.h>
#include <util/twi.h>

namespace {
uint32_t now = 0;
}

namespace Wires {
namespace Hardware {
uint32_t micros() { now += 10; return now; }
}
}

using namespace Wires;

int main()
{
    try {
        run("TWI configuration and invalid inputs", [] {
            HostTwi::reset();
            Hardware::beginTwi();
            CHECK(TWBR == 72);
            CHECK(TWSR == 0);
            uint8_t data = 0;
            CHECK(!Hardware::writeTwi(0x11, nullptr, 1));
            CHECK(!Hardware::writeTwi(0x11, &data, 0));
            CHECK(!Hardware::readTwi(0x11, nullptr, 1));
            CHECK(!Hardware::readTwi(0x11, &data, 0));
            CHECK(!Hardware::readTwi(7, &data, 1));
            CHECK(!Hardware::writeTwi(0x78, &data, 1));
            CHECK(HostTwi::written.empty());
            SREG = 0;
            CHECK(!Hardware::readTwi(0x11, &data, 1));
            CHECK(!Hardware::writeTwi(0x11, &data, 1));
            SREG = 0x80;
        });

        run("TWI transmits address and all packet bytes", [] {
            HostTwi::reset();
            HostTwi::steps = {{TW_START, 0}, {TW_MT_SLA_ACK, 0},
                              {TW_MT_DATA_ACK, 0}, {TW_MT_DATA_ACK, 0}};
            const uint8_t data[] = {0xB7, 0xFF};
            CHECK(Hardware::writeTwi(0x11, data, 2));
            CHECK(HostTwi::stops == 1);
            CHECK(HostTwi::steps.empty());
            CHECK(HostTwi::written[1] == 0x22);
            CHECK(HostTwi::written[2] == 0xB7);
            CHECK(HostTwi::written[3] == 0xFF);
        });

        run("TWI reads with final NACK and supports repeated START", [] {
            HostTwi::reset();
            HostTwi::steps = {{TW_REP_START, 0}, {TW_MR_SLA_ACK, 0},
                              {TW_MR_DATA_ACK, 0x12}, {TW_MR_DATA_NACK, 0x34}};
            uint8_t data[2] = {};
            CHECK(Hardware::readTwi(0x12, data, 2));
            CHECK(data[0] == 0x12 && data[1] == 0x34);
            CHECK(HostTwi::written[1] == 0x25);
            CHECK(!(uint8_t(TWCR) & _BV(TWEA)));
            CHECK(HostTwi::stops == 1);
        });

        run("TWI address NACK data NACK arbitration error and bus error fail", [] {
            for (uint8_t status : {uint8_t(0), uint8_t(0x20),
                                   uint8_t(0x30), uint8_t(0x38)}) {
                HostTwi::reset();
                HostTwi::steps = {{TW_START, 0}, {status, 0}};
                const uint8_t value = 1;
                CHECK(!Hardware::writeTwi(0x11, &value, 1));
                CHECK(HostTwi::recoveries > 0);
                CHECK(uint8_t(TWCR) == _BV(TWEN));
            }
        });

        run("TWI stuck interrupt and STOP have bounded time including wrap", [] {
            for (bool stopStuck : {false, true}) {
                HostTwi::reset();
                now = UINT32_MAX - 1000;
                HostTwi::stuck = !stopStuck;
                HostTwi::stopStuck = stopStuck;
                if (stopStuck) {
                    HostTwi::steps = {{TW_START, 0}, {TW_MT_SLA_ACK, 0},
                                      {TW_MT_DATA_ACK, 0}};
                }
                const uint32_t started = now;
                const uint8_t value = 1;
                CHECK(!Hardware::writeTwi(0x11, &value, 1));
                CHECK(uint32_t(now - started) >= Config::TWI_TIMEOUT_US);
                CHECK(uint32_t(now - started) <= Config::TWI_TIMEOUT_US + 30);
                CHECK(HostTwi::recoveries > 0);
            }
        });

        std::cout << "Passed " << cases << " TWI groups, " << checks << " checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << "\n";
        return 1;
    }
}
