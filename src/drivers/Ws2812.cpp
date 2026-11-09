#include "drivers/Board.h"
#include "Config.h"

#include <avr/io.h>
#include <avr/interrupt.h>

static_assert(F_CPU == 16000000UL, "WS2812 cycle timing requires 16 MHz");

namespace {

// 20 тактов на бит при 16 МГц. HIGH занимает 5 тактов для 0 и 11 для 1.
// Последние 8 тактов каждого байта используются для проверки RX и загрузки.
// При LOW на RX прерываем кадр максимум через один байт (около 10 мкс).
#define PIXEL_BIT(bit) \
    "out %[port], %[high]\n\t" \
    "nop\n\tnop\n\tnop\n\t" \
    "sbrs %[value], " #bit "\n\t" \
    "out %[port], %[low]\n\t" \
    "nop\n\tnop\n\tnop\n\tnop\n\tnop\n\t" \
    "out %[port], %[low]\n\t"

#define PIXEL_PADDING "nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\t"

bool transmit(const uint8_t *data, uint8_t remaining)
{
    uint8_t value;
    uint8_t success;
    const uint8_t low = PORTB & uint8_t(~_BV(PB0));
    const uint8_t high = low | _BV(PB0);

    asm volatile(
        "ld %[value], Z+\n\t"
        "1:\n\t"
        PIXEL_BIT(7) PIXEL_PADDING
        PIXEL_BIT(6) PIXEL_PADDING
        PIXEL_BIT(5) PIXEL_PADDING
        PIXEL_BIT(4) PIXEL_PADDING
        PIXEL_BIT(3) PIXEL_PADDING
        PIXEL_BIT(2) PIXEL_PADDING
        PIXEL_BIT(1) PIXEL_PADDING
        PIXEL_BIT(0)
        "sbis %[input], 2\n\t"
        "rjmp 3f\n\t"
        "dec %[remaining]\n\t"
        "breq 2f\n\t"
        "ld %[value], Z+\n\t"
        "rjmp 1b\n\t"
        "2: ldi %[success], 1\n\t"
        "rjmp 4f\n\t"
        "3: clr %[success]\n\t"
        "4:\n\t"
        : [data] "+&z" (data), [remaining] "+&r" (remaining),
          [value] "=&r" (value), [success] "=&d" (success)
        : [port] "I" (_SFR_IO_ADDR(PORTB)), [input] "I" (_SFR_IO_ADDR(PINB)),
          [high] "r" (high), [low] "r" (low)
        : "memory", "cc"
    );

    return success != 0;
}

#undef PIXEL_BIT
#undef PIXEL_PADDING

} // namespace

namespace Wires {
namespace Hardware {

bool showPixels(const uint8_t *data, uint8_t size)
{
    static uint32_t lastAttempt = 0;
    if (!data || !size || uint32_t(micros() - lastAttempt) < Config::LED_RESET_US ||
        !espIdle() || uint32_t(millis() - lastReceiveAt()) < Config::LED_UART_QUIET_MS) {
        return false;
    }

    const uint8_t saved = SREG;
    cli();

    if (!espIdle() || espReceivePending()) {
        SREG = saved;
        return false;
    }

    const bool success = transmit(data, size);
    PORTB &= uint8_t(~_BV(PB0));
    SREG = saved;
    lastAttempt = micros();
    return success;
}

} // namespace Hardware
} // namespace Wires
