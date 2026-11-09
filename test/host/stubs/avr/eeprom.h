#pragma once

#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <stdexcept>
#include <vector>

#define EEMEM

namespace HostEeprom {
inline uint8_t *base = nullptr;
inline size_t length = 0;
inline int writesLeft = -1;

inline std::vector<uint8_t> image()
{
    return {base, base + length};
}

inline void restore(const std::vector<uint8_t> &data)
{
    std::memcpy(base, data.data(), data.size());
}

inline void update(uint8_t *address, uint8_t value)
{
    if (*address == value) { return; }
    if (writesLeft == 0) { throw std::runtime_error("simulated EEPROM power loss"); }
    if (writesLeft > 0) { --writesLeft; }
    *address = value;
}
}

inline void eeprom_read_block(void *destination, const void *source, size_t size)
{
    HostEeprom::base = const_cast<uint8_t *>(static_cast<const uint8_t *>(source));
    HostEeprom::length = size;
    std::memcpy(destination, source, size);
}

inline void eeprom_update_byte(uint8_t *address, uint8_t value)
{
    HostEeprom::update(address, value);
}

inline void eeprom_update_dword(uint32_t *address, uint32_t value)
{
    for (unsigned byte = 0; byte < 4; ++byte) {
        HostEeprom::update(reinterpret_cast<uint8_t *>(address) + byte,
                           uint8_t(value >> (8 * byte)));
    }
}
