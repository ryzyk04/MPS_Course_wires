#include <stdint.h>
#include <vector>
#include <cstring>
#include <iostream>

#include "Test.h"
#include "drivers/Board.h"
#include "Config.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>

void PCINT0_vect();
void TIMER1_COMPA_vect();
void TIMER1_COMPB_vect();
void TIMER0_COMPA_vect();

using namespace Wires;

namespace {

void receive(uint8_t value, bool stop = true)
{
    PINB |= _BV(PB2);
    const uint16_t started = TCNT1;
    PINB &= uint8_t(~_BV(PB2));
    PCIFR.raise(_BV(PCIF0));
    PCINT0_vect();
    CHECK(OCR1B == uint16_t(started + 312));
    for (uint8_t bit = 0; bit < 8; ++bit) {
        if (value & (1U << bit)) { PINB |= _BV(PB2); }
        else { PINB &= uint8_t(~_BV(PB2)); }
        TCNT1 = OCR1B;
        TIMER1_COMPB_vect();
    }
    if (stop) { PINB |= _BV(PB2); }
    else { PINB &= uint8_t(~_BV(PB2)); }
    TCNT1 = OCR1B;
    TIMER1_COMPB_vect();
}

} // namespace

int main()
{
    try {
        PINB = _BV(PB1) | _BV(PB2);
        Hardware::beginBoard();
        Hardware::beginClock();
        Hardware::beginEspUart();

        run("GPIO and timer configuration preserve the original pinout", [] {
            CHECK((DDRD & 0xF8) == 0);
            CHECK((PORTD & 0xF8) == 0xF8);
            CHECK((DDRB & (_BV(PB0) | _BV(PB3))) == (_BV(PB0) | _BV(PB3)));
            CHECK(!(DDRB & (_BV(PB1) | _BV(PB2))));
            CHECK(OCR0A == 249);
            CHECK(PCMSK0 == _BV(PCINT2));
            CHECK(TCCR1B == _BV(CS11));
            PIND = 0xA8;
            CHECK(Hardware::openWires() == 21);
            PINB &= uint8_t(~_BV(PB1));
            CHECK(Hardware::buttonPressed());
            PINB |= _BV(PB1);
            CHECK(!Hardware::buttonPressed());
        });

        run("clock includes pending compare and restores interrupt state", [] {
            CHECK(Hardware::millis() == 0);
            for (int tick = 0; tick < 5; ++tick) { TIMER0_COMPA_vect(); }
            TCNT0 = 100;
            CHECK(Hardware::micros() == 5400);
            TIFR0.raise(_BV(OCF0A));
            CHECK(Hardware::micros() == 6400);
            TIFR0 = _BV(OCF0A);
            for (uint8_t state : {uint8_t(0), uint8_t(0x80)}) {
                SREG = state;
                CHECK(Hardware::millis() == 5);
                CHECK(SREG == state);
            }
            SREG = 0x80;
        });

        run("UART transmit start eight data bits stop and timer wrap", [] {
            TCNT1 = 65500;
            Hardware::writeEsp("A");
            CHECK(!(PORTB & _BV(PB3)));
            CHECK(!Hardware::espIdle());
            CHECK(OCR1A == uint16_t(65500 + 208));
            for (uint8_t bit = 0; bit < 8; ++bit) {
                TCNT1 = OCR1A;
                TIMER1_COMPA_vect();
                CHECK(bool(PORTB & _BV(PB3)) == bool(0x41 & (1U << bit)));
            }
            TCNT1 = OCR1A;
            TIMER1_COMPA_vect();
            CHECK(PORTB & _BV(PB3));
            CHECK(!Hardware::espIdle());
            TCNT1 = OCR1A;
            TIMER1_COMPA_vect();
            CHECK(Hardware::espIdle());
            CHECK(!(TIMSK1 & _BV(OCIE1A)));
        });

        run("UART receives all byte values and frames are sampled at 1.5 bits", [] {
            uint8_t byte;
            CHECK(!Hardware::readEsp(byte));
            for (unsigned value = 0; value <= 255; ++value) {
                TCNT1 = 65500;
                receive(uint8_t(value));
                CHECK(Hardware::readEsp(byte));
                CHECK(byte == value);
                CHECK(!Hardware::readEsp(byte));
                CHECK(!(TIMSK1 & _BV(OCIE1B)));
                CHECK(PCMSK0 & _BV(PCINT2));
            }
            CHECK(!Hardware::takeReceiveFault());
            CHECK(Hardware::lastReceiveAt() == Hardware::millis());
        });

        run("UART framing error and ring overflow invalidate buffered data", [] {
            receive('X', false);
            CHECK(Hardware::takeReceiveFault());
            CHECK(!Hardware::takeReceiveFault());
            uint8_t value;
            CHECK(!Hardware::readEsp(value));
            for (unsigned count = 0; count < Config::UART_BUFFER_SIZE; ++count) {
                receive(uint8_t(count));
            }
            CHECK(Hardware::takeReceiveFault());
            CHECK(!Hardware::readEsp(value));
            receive('Z');
            CHECK(Hardware::readEsp(value) && value == 'Z');
        });

        run("UART continues full duplex and start notification detects incoming traffic", [] {
            Hardware::writeEsp("Q");
            receive(0x5A);
            uint8_t value;
            CHECK(Hardware::readEsp(value) && value == 0x5A);
            for (unsigned tick = 0; tick < 10; ++tick) {
                TCNT1 = OCR1A;
                TIMER1_COMPA_vect();
            }
            CHECK(Hardware::espIdle());
            CHECK(!Hardware::espReceivePending());
            PINB &= uint8_t(~_BV(PB2));
            CHECK(Hardware::espReceivePending());
            PINB |= _BV(PB2);
        });

        run("EEPROM button IDs reserve blocks and survive interrupted writes", [] {
            Hardware::beginBoard();
            for (uint32_t expected = 1; expected <= 256; ++expected) {
                CHECK(Hardware::nextButtonId() == expected);
            }
            const auto baseline = HostEeprom::image();
            for (int writes = 0; writes <= 12; ++writes) {
                HostEeprom::restore(baseline);
                Hardware::beginBoard();
                HostEeprom::writesLeft = writes;
                uint32_t issued = 0;
                try { issued = Hardware::nextButtonId(); }
                catch (const std::runtime_error &) {}
                HostEeprom::writesLeft = -1;
                Hardware::beginBoard();
                const uint32_t next = Hardware::nextButtonId();
                CHECK(next >= 257);
                if (issued) { CHECK(next > issued); }
            }
        });

        run("EEPROM ID range wraps without issuing zero", [] {
            struct Slot { uint32_t limit, inverse; uint8_t committed; };
            Slot slots[2] = {
                {0xFFFFFF00UL, ~0xFFFFFF00UL, 0xA5},
                {0xFFFFFE00UL, ~0xFFFFFE00UL, 0xA5}
            };
            CHECK(sizeof(slots) == HostEeprom::length);
            std::memcpy(HostEeprom::base, slots, sizeof(slots));
            Hardware::beginBoard();
            CHECK(Hardware::nextButtonId() == 1);
            Hardware::beginBoard();
            CHECK(Hardware::nextButtonId() == 257);
        });

        std::cout << "Passed " << cases << " hardware groups, " << checks << " checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << "\n";
        return 1;
    }
}
