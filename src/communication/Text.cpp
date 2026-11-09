#include "communication/Text.h"

namespace Wires {

Text &Text::append(const char *text)
{
    while (*text) {
        if (length_ >= sizeof(buffer_) - 2) {
            valid_ = false;
            break;
        }
        buffer_[length_++] = *text++;
    }
    return *this;
}

Text &Text::number(uint64_t value)
{
    char digits[21];
    uint8_t position = 20;
    digits[position] = 0;
    do {
        digits[--position] = char('0' + value % 10);
        value /= 10;
    } while (value);
    return append(digits + position);
}

const char *Text::line()
{
    if (!valid_) {
        return "";
    }
    buffer_[length_] = '\n';
    buffer_[length_ + 1] = 0;
    return buffer_;
}

} // namespace Wires
