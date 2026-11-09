#include "drivers/Board.h"
#include "Config.h"

#include <avr/io.h>
#include <util/twi.h>

namespace {

uint32_t transactionStarted = 0;

bool expired()
{
    return uint32_t(Wires::Hardware::micros() - transactionStarted) >=
           Wires::Config::TWI_TIMEOUT_US;
}

bool wait()
{
    while (!(TWCR & _BV(TWINT))) {
        if (expired()) {
            return false;
        }
    }
    return !expired();
}

void recover()
{
    TWCR = 0;
    TWCR = _BV(TWEN);
}

bool stop()
{
    TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWSTO);
    while (TWCR & _BV(TWSTO)) {
        if (expired()) {
            recover();
            return false;
        }
    }
    return true;
}

bool address(uint8_t target, bool read)
{
    TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWSTA);
    if (!wait() || (TW_STATUS != TW_START && TW_STATUS != TW_REP_START)) {
        return false;
    }

    TWDR = uint8_t((target << 1) | uint8_t(read));
    TWCR = _BV(TWINT) | _BV(TWEN);
    return wait() && TW_STATUS == (read ? TW_MR_SLA_ACK : TW_MT_SLA_ACK);
}

} // namespace

namespace Wires {
namespace Hardware {

void beginTwi()
{
    static_assert(Config::TWI_SPEED_HZ >= 31000 &&
                  Config::TWI_SPEED_HZ <= 400000, "Unsupported TWI frequency");
    static_assert(Config::TWI_TIMEOUT_US > 0, "TWI timeout must be positive");
    TWSR = 0;
    TWBR = uint8_t((F_CPU / Config::TWI_SPEED_HZ - 16) / 2);
    TWCR = _BV(TWEN);
}

bool writeTwi(uint8_t target, const uint8_t *data, uint8_t size)
{
    if (!data || !size || target < 8 || target > 0x77 || !(SREG & 0x80)) {
        return false;
    }

    transactionStarted = micros();
    bool success = address(target, false);
    for (uint8_t index = 0; success && index < size; ++index) {
        TWDR = data[index];
        TWCR = _BV(TWINT) | _BV(TWEN);
        success = wait() && TW_STATUS == TW_MT_DATA_ACK;
    }

    const bool stopped = stop();
    if (!success || !stopped) {
        recover();
    }
    return success && stopped;
}

bool readTwi(uint8_t target, uint8_t *data, uint8_t size)
{
    if (!data || !size || target < 8 || target > 0x77 || !(SREG & 0x80)) {
        return false;
    }

    transactionStarted = micros();
    bool success = address(target, true);
    for (uint8_t index = 0; success && index < size; ++index) {
        const bool last = index == size - 1;
        TWCR = _BV(TWINT) | _BV(TWEN) | (last ? 0 : _BV(TWEA));
        success = wait() && TW_STATUS == (last ? TW_MR_DATA_NACK : TW_MR_DATA_ACK);
        if (success) {
            data[index] = TWDR;
        }
    }

    const bool stopped = stop();
    if (!success || !stopped) {
        recover();
    }
    return success && stopped;
}

} // namespace Hardware
} // namespace Wires
