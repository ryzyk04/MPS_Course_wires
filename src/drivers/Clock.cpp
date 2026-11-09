#include "drivers/Board.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

static_assert(F_CPU == 16000000UL, "Clock and LED timings require 16 MHz");

namespace {
volatile uint32_t ticks = 0;
}

ISR(TIMER0_COMPA_vect)
{
    ++ticks;
}

namespace Wires {
namespace Hardware {

void beginClock()
{
    TCCR0A = _BV(WGM01);
    TCCR0B = _BV(CS01) | _BV(CS00); // 16 MHz / 64 / 250 = 1 kHz.
    TCNT0 = 0;
    OCR0A = 249;
    TIFR0 = _BV(OCF0A);
    TIMSK0 = _BV(OCIE0A);
}

uint32_t millis()
{
    uint32_t value;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        value = ticks;
    }
    return value;
}

uint32_t micros()
{
    uint32_t milliseconds;
    uint8_t fraction;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        milliseconds = ticks;
        fraction = TCNT0;
        if (TIFR0 & _BV(OCF0A)) {
            ++milliseconds;
            fraction = TCNT0;
        }
    }
    return milliseconds * 1000UL + uint32_t(fraction) * 4;
}

} // namespace Hardware
} // namespace Wires
