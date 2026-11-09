#pragma once

#include <stdint.h>
#include <deque>
#include <vector>

#define _BV(bit) (1U << (bit))
#define PB0 0
#define PB1 1
#define PB2 2
#define PB3 3
#define U2X0 1
#define UDRE0 5
#define TXEN0 3
#define UCSZ00 1
#define UCSZ01 2
#define WGM01 1
#define CS00 0
#define CS01 1
#define OCF0A 1
#define OCIE0A 1
#define CS11 1
#define OCF1A 1
#define OCF1B 2
#define OCIE1A 1
#define OCIE1B 2
#define PCINT2 2
#define PCIF0 0
#define PCIE0 0
#define TWINT 7
#define TWEA 6
#define TWSTA 5
#define TWSTO 4
#define TWEN 2

inline uint8_t SREG = 0x80;
inline uint8_t DDRD = 0, PORTD = 0, PIND = 0;
inline uint8_t DDRB = 0, PORTB = 0, PINB = 0;
inline uint8_t UCSR0A = 0, UCSR0B = 0, UCSR0C = 0, UBRR0H = 0, UBRR0L = 0, UDR0 = 0;
inline uint8_t TCCR0A = 0, TCCR0B = 0, TCNT0 = 0, OCR0A = 0, TIMSK0 = 0;
inline uint8_t TCCR1A = 0, TCCR1B = 0, TIMSK1 = 0, PCMSK0 = 0, PCICR = 0;
inline uint16_t TCNT1 = 0, OCR1A = 0, OCR1B = 0;

struct FlagRegister {
    uint8_t value = 0;
    operator uint8_t() const { return value; }
    void operator=(uint8_t clear) { value &= uint8_t(~clear); }
    void raise(uint8_t flags) { value |= flags; }
};
inline FlagRegister TIFR0, TIFR1, PCIFR;

inline uint8_t TWSR = 0, TWDR = 0, TWBR = 0;
namespace HostTwi {
struct Step { uint8_t status; uint8_t data; };
inline std::deque<Step> steps;
inline std::vector<uint8_t> written;
inline bool stuck = false;
inline bool stopStuck = false;
inline unsigned stops = 0;
inline unsigned recoveries = 0;
inline void reset()
{
    steps.clear();
    written.clear();
    stuck = stopStuck = false;
    stops = recoveries = 0;
}
}

struct TwiControl {
    uint8_t value = 0;
    void operator=(uint8_t next)
    {
        if (!next) {
            ++HostTwi::recoveries;
        }
        value = next & uint8_t(~_BV(TWINT));
        if (next & _BV(TWSTO)) {
            ++HostTwi::stops;
            if (!HostTwi::stopStuck) { value &= uint8_t(~_BV(TWSTO)); }
        } else if (next & _BV(TWINT)) {
            HostTwi::written.push_back(TWDR);
            if (!HostTwi::stuck && !HostTwi::steps.empty()) {
                const auto step = HostTwi::steps.front();
                HostTwi::steps.pop_front();
                TWSR = step.status;
                TWDR = step.data;
                value |= _BV(TWINT);
            }
        }
    }
    operator uint8_t() const { return value; }
};
inline TwiControl TWCR;
