#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Wires {

// Построитель одной строки без heap и зависимости от Print/Serial.
class Text {
public:
    Text &append(const char *text);
    Text &number(uint64_t value);
    const char *line();

private:
    char buffer_[48] = {};
    uint8_t length_ = 0;
    bool valid_ = true;
};

} // namespace Wires
