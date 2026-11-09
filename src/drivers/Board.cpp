#include "drivers/Board.h"
#include "Config.h"

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>

namespace {

struct Reservation {
    uint32_t limit;
    uint32_t inverse;
    uint8_t committed;
};
Reservation EEMEM reservations[2];
uint32_t nextId = 0;
uint16_t availableIds = 0;

void character(char value)
{
    while (!(UCSR0A & _BV(UDRE0))) {}
    UDR0 = value;
}

void flashText(const char *text)
{
    char value;
    while ((value = char(pgm_read_byte(text++)))) {
        character(value);
    }
}

void number(uint16_t value)
{
    char digits[6];
    uint8_t position = 5;
    digits[position] = 0;
    do {
        digits[--position] = char('0' + value % 10);
        value /= 10;
    } while (value);
    while (digits[position]) {
        character(digits[position++]);
    }
}

} // namespace

namespace Wires {
namespace Hardware {

void beginBoard()
{
    availableIds = 0;
    DDRD &= uint8_t(~0xF8); // D3..D7, замкнутый провод соединяет вход с GND.
    PORTD |= 0xF8;
    DDRB &= uint8_t(~(_BV(PB1) | _BV(PB2))); // D9 button, D10 UART RX.
    PORTB |= _BV(PB1) | _BV(PB2);
    DDRB |= _BV(PB0) | _BV(PB3); // D8 LEDs, D11 UART TX.
    PORTB = (PORTB & uint8_t(~_BV(PB0))) | _BV(PB3);

    UCSR0A = _BV(U2X0);
    UBRR0H = 0;
    UBRR0L = 16; // USB debug, 115200 8N1.
    UCSR0B = _BV(TXEN0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
}

uint8_t openWires()
{
    return uint8_t(PIND >> 3) & 0x1F;
}

bool buttonPressed()
{
    return !(PINB & _BV(PB1));
}

uint32_t nextButtonId()
{
    // Две записи с маркером последнего шага сохраняют резервирование при
    // отключении питания посередине обновления EEPROM.
    if (!availableIds) {
        Reservation records[2];
        eeprom_read_block(records, reservations, sizeof(records));
        bool valid[2];
        for (uint8_t index = 0; index < 2; ++index) {
            valid[index] = records[index].committed == 0xA5 &&
                (records[index].limit ^ records[index].inverse) == UINT32_MAX &&
                (records[index].limit & 255UL) == 0;
        }

        uint8_t active = 0;
        if (valid[1] && (!valid[0] ||
            (records[1].limit != records[0].limit &&
             uint32_t(records[1].limit - records[0].limit) < 0x80000000UL))) {
            active = 1;
        }
        uint32_t reserved = valid[active] ? records[active].limit : 0;
        if (reserved > UINT32_MAX - 256UL) {
            reserved = 0;
        }

        const uint8_t next = active ^ 1;
        eeprom_update_byte(&reservations[next].committed, 0);
        eeprom_update_dword(&reservations[next].limit, reserved + 256UL);
        eeprom_update_dword(&reservations[next].inverse, ~(reserved + 256UL));
        eeprom_update_byte(&reservations[next].committed, 0xA5);

        nextId = reserved;
        availableIds = 256;
    }
    --availableIds;
    return ++nextId;
}

void debug(Diagnostic event, uint8_t module)
{
    if (module) {
        flashText(PSTR("I2C "));
        number(module);
        flashText(PSTR(": "));
    }

    switch (event) {
        case Diagnostic::Startup:
            flashText(PSTR("WIRES AVR: RX=D10 TX=D11 9600; TWI=A4/A5\r\n"));
            break;
        case Diagnostic::Ready: flashText(PSTR("READY\r\n")); break;
        case Diagnostic::Solved: flashText(PSTR("SOLVED\r\n")); break;
        case Diagnostic::Failed: flashText(PSTR("FAILED\r\n")); break;
        case Diagnostic::NodeOk: flashText(PSTR("OK, valid STATUS\r\n")); break;
        case Diagnostic::NodeMissing: flashText(PSTR("NO RESPONSE\r\n")); break;
        case Diagnostic::NodeInvalid: flashText(PSTR("INVALID FRAME\r\n")); break;
        case Diagnostic::NodeWrong: flashText(PSTR("WRONG MODULE OR OPERATION\r\n")); break;
    }
}

void debugRound(uint16_t seed, uint8_t difficulty, uint8_t targets)
{
    flashText(PSTR("Seed: "));
    number(seed);
    flashText(PSTR(" level: "));
    number(difficulty);
    if (Config::DEBUG_SOLUTION) {
        flashText(PSTR(" targets: "));
        for (uint8_t position = 0; position < 5; ++position) {
            if (targets & (1U << position)) {
                number(position + 1);
                character(' ');
            }
        }
    }
    flashText(PSTR("\r\n"));
}

} // namespace Hardware
} // namespace Wires
