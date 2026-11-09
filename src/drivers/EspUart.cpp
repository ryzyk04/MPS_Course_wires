#include "drivers/Board.h"
#include "drivers/Ring.h"
#include "Config.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

namespace {

constexpr uint16_t BIT_TICKS = uint16_t((F_CPU / 8UL + Wires::Config::UART_BAUD / 2) /
                                         Wires::Config::UART_BAUD);
static_assert(BIT_TICKS >= 32 && BIT_TICKS < 16000, "Unsupported software UART baud rate");

Ring<Wires::Config::UART_BUFFER_SIZE> rx;
Ring<Wires::Config::UART_BUFFER_SIZE> tx;

volatile bool receiving = false;
volatile bool transmitting = false;
volatile bool receiveFault = false;
volatile uint32_t receivedAt = 0;
uint8_t receiveByte = 0;
uint8_t receiveBit = 0;
uint8_t transmitByte = 0;
uint8_t transmitBit = 0;

void startTransmit(uint8_t byte)
{
    transmitByte = byte;
    transmitBit = 0;
    transmitting = true;
    PORTB &= uint8_t(~_BV(PB3)); // Стартовый бит.
    OCR1A = uint16_t(TCNT1 + BIT_TICKS);
    TIFR1 = _BV(OCF1A);
    TIMSK1 |= _BV(OCIE1A);
}

} // namespace

ISR(PCINT0_vect)
{
    if (!receiving && !(PINB & _BV(PB2))) {
        receiving = true;
        receiveByte = 0;
        receiveBit = 0;
        PCMSK0 &= uint8_t(~_BV(PCINT2));
        // Первая выборка посередине первого бита данных, через 1.5 бита.
        OCR1B = uint16_t(TCNT1 + BIT_TICKS + BIT_TICKS / 2);
        TIFR1 = _BV(OCF1B);
        TIMSK1 |= _BV(OCIE1B);
    }
}

ISR(TIMER1_COMPB_vect)
{
    if (receiveBit < 8) {
        if (PINB & _BV(PB2)) {
            receiveByte |= uint8_t(1U << receiveBit);
        }
        ++receiveBit;
        OCR1B = uint16_t(OCR1B + BIT_TICKS);
        return;
    }

    // Стоповый бит проверяется отдельно. Сбой инвалидирует текущую строку.
    if (!(PINB & _BV(PB2)) || !rx.push(receiveByte)) {
        receiveFault = true;
    }
    receivedAt = Wires::Hardware::millis();
    receiving = false;
    TIMSK1 &= uint8_t(~_BV(OCIE1B));
    PCIFR = _BV(PCIF0);
    PCMSK0 |= _BV(PCINT2);
}

ISR(TIMER1_COMPA_vect)
{
    if (transmitBit < 8) {
        if (transmitByte & uint8_t(1U << transmitBit)) {
            PORTB |= _BV(PB3);
        } else {
            PORTB &= uint8_t(~_BV(PB3));
        }
        ++transmitBit;
        OCR1A = uint16_t(OCR1A + BIT_TICKS);
    } else if (transmitBit == 8) {
        PORTB |= _BV(PB3);
        ++transmitBit;
        OCR1A = uint16_t(OCR1A + BIT_TICKS);
    } else {
        uint8_t byte;
        if (tx.pop(byte)) {
            startTransmit(byte);
        } else {
            transmitting = false;
            TIMSK1 &= uint8_t(~_BV(OCIE1A));
        }
    }
}

namespace Wires {
namespace Hardware {

void beginEspUart()
{
    TCCR1A = 0;
    TCCR1B = _BV(CS11); // Свободный счётчик, 2 МГц.
    TCNT1 = 0;
    TIMSK1 = 0;
    PCIFR = _BV(PCIF0);
    PCMSK0 = _BV(PCINT2);
    PCICR |= _BV(PCIE0);
}

bool readEsp(uint8_t &byte)
{
    return rx.pop(byte);
}

void writeEsp(const char *text)
{
    while (*text) {
        bool queued;
        do {
            ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                queued = tx.push(uint8_t(*text));
                if (!transmitting) {
                    uint8_t byte;
                    if (tx.pop(byte)) {
                        startTransmit(byte);
                    }
                }
            }
        } while (!queued);
        ++text;
    }
}

bool takeReceiveFault()
{
    bool fault;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        fault = receiveFault;
        if (fault) {
            // Не оставляем старые переводы строк перед повреждённым хвостом.
            rx.clear();
            receiveFault = false;
        }
    }
    return fault;
}

bool espIdle()
{
    bool idle;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        idle = !receiving && !transmitting && tx.empty();
    }
    return idle;
}

bool espReceivePending()
{
    return receiving || !(PINB & _BV(PB2)) || (PCIFR & _BV(PCIF0));
}

uint32_t lastReceiveAt()
{
    uint32_t value;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        value = receivedAt;
    }
    return value;
}

} // namespace Hardware
} // namespace Wires
