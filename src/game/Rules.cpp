#include "game/Puzzle.h"

namespace Wires {
namespace {

// Правила выбирают первое подходящее условие. Маски относятся к физическим
// позициям, а индексы внутри правил — к активным проводам слева направо.
class Rules {
public:
    explicit Rules(const Puzzle &puzzle) : puzzle_(puzzle) {}

    uint8_t evaluate() const;

private:
    const Puzzle &puzzle_;

    uint8_t position(uint8_t index) const
    {
        return wireBit(puzzle_.positions[index]);
    }
    uint8_t pair(uint8_t first, uint8_t second) const
    {
        return position(first) | position(second);
    }
    uint8_t count(uint8_t color) const;
    uint8_t first(uint8_t color) const;
    uint8_t last(uint8_t color) const;
    uint8_t lastTemperature(bool warm) const;
    bool hasDigit(uint8_t digit) const;
    bool repeats() const;
};

bool warm(uint8_t color)
{
    return color == COLOR_PINK || color == COLOR_RED ||
           color == COLOR_ORANGE || color == COLOR_YELLOW;
}

bool cold(uint8_t color)
{
    return color == COLOR_GREEN || color == COLOR_CYAN ||
           color == COLOR_BLUE || color == COLOR_VIOLET;
}

bool prime(uint8_t digit)
{
    return digit == 2 || digit == 3 || digit == 5 || digit == 7;
}

uint8_t Rules::count(uint8_t color) const
{
    uint8_t result = 0;
    for (uint8_t index = 0; index < puzzle_.count; ++index) {
        result += puzzle_.colors[index] == color;
    }
    return result;
}

uint8_t Rules::first(uint8_t color) const
{
    for (uint8_t index = 0; index < puzzle_.count; ++index) {
        if (puzzle_.colors[index] == color) {
            return position(index);
        }
    }
    return 0;
}

uint8_t Rules::last(uint8_t color) const
{
    for (uint8_t index = puzzle_.count; index > 0; --index) {
        if (puzzle_.colors[index - 1] == color) {
            return position(index - 1);
        }
    }
    return 0;
}

uint8_t Rules::lastTemperature(bool wantWarm) const
{
    for (uint8_t index = puzzle_.count; index > 0; --index) {
        const uint8_t color = puzzle_.colors[index - 1];
        if (wantWarm ? warm(color) : cold(color)) {
            return position(index - 1);
        }
    }
    return 0;
}

bool Rules::hasDigit(uint8_t digit) const
{
    for (uint8_t value : puzzle_.digits) {
        if (value == digit) {
            return true;
        }
    }
    return false;
}

bool Rules::repeats() const
{
    for (uint8_t first = 0; first < 4; ++first) {
        for (uint8_t second = first + 1; second < 4; ++second) {
            if (puzzle_.digits[first] == puzzle_.digits[second]) {
                return true;
            }
        }
    }
    return false;
}

uint8_t Rules::evaluate() const
{
    const uint8_t *colors = puzzle_.colors;
    const uint8_t *digits = puzzle_.digits;
    const uint8_t size = puzzle_.count;
    const uint8_t firstDigit = digits[0];
    const uint8_t lastDigit = digits[3];
    const uint8_t sum = digits[0] + digits[1] + digits[2] + digits[3];
    const bool even = sum % 2 == 0;
    const bool lastEven = lastDigit % 2 == 0;
    const bool halfBigger = digits[0] + digits[1] > digits[2] + digits[3];

    uint8_t warmCount = 0;
    uint8_t coldCount = 0;
    uint8_t distinct = 0;
    for (uint8_t index = 0; index < size; ++index) {
        warmCount += warm(colors[index]);
        coldCount += cold(colors[index]);
    }
    for (uint8_t color = 0; color < COLOR_COUNT; ++color) {
        distinct += count(color) > 0;
    }

    // Лёгкая сложность.
    if (puzzle_.difficulty == EASY) {
        if (size == 3) {
            if (!count(COLOR_RED) && !lastEven) {
                return position(1);
            }
            if (colors[2] == COLOR_WHITE) {
                return position(2);
            }
            if (count(COLOR_BLUE) > 1) {
                return last(COLOR_BLUE);
            }
            if (hasDigit(0)) {
                return pair(0, 2);
            }
            return position(0);
        }

        if (size == 4) {
            if (count(COLOR_RED) > 1 && lastEven) {
                return last(COLOR_RED);
            }
            if (colors[0] == COLOR_YELLOW && !count(COLOR_GREEN)) {
                return pair(0, 3);
            }
            if (count(COLOR_BLUE) == 1) {
                return first(COLOR_BLUE);
            }
            if (firstDigit > lastDigit) {
                return position(2);
            }
            return position(1);
        }

        if (colors[4] == COLOR_VIOLET && !lastEven) {
            return position(3);
        }
        if (count(COLOR_WHITE) > 1) {
            return first(COLOR_WHITE) | last(COLOR_WHITE);
        }
        if (count(COLOR_RED) == 1 && count(COLOR_GREEN) > 1) {
            return first(COLOR_RED);
        }
        if (hasDigit(0)) {
            return pair(1, 4);
        }
        return position(2);
    }

    // Средняя сложность.
    if (puzzle_.difficulty == MEDIUM) {
        if (size == 3) {
            if (distinct == 1) {
                return position(1);
            }
            if (colors[0] == colors[2] && even) {
                return pair(0, 2);
            }
            if (!warmCount) {
                return position(2);
            }
            if (count(COLOR_PINK) > 1) {
                return first(COLOR_PINK);
            }
            if (cold(colors[1]) && firstDigit % 2 == 0) {
                return pair(1, 2);
            }
            if (lastDigit == 3 || lastDigit > 7) {
                return position(0);
            }
            return position(2);
        }

        if (size == 4) {
            if (warmCount == 2 && coldCount == 2) {
                return pair(1, 2);
            }
            if (colors[0] == colors[1]) {
                return position(0);
            }
            if (count(COLOR_RED) > 1 && sum > 18) {
                return last(COLOR_RED);
            }
            if (count(COLOR_VIOLET) == 1 && !lastEven) {
                return first(COLOR_VIOLET);
            }
            if (colors[3] == COLOR_WHITE || colors[3] == COLOR_PINK) {
                return pair(0, 3);
            }
            if (count(COLOR_GREEN) > 1) {
                return first(COLOR_GREEN) | last(COLOR_GREEN);
            }
            return position(0);
        }

        if (distinct == 5 && !even) {
            return position(2);
        }
        if (warmCount == 4) {
            for (uint8_t index = 0; index < size; ++index) {
                if (!warm(colors[index])) {
                    return position(index);
                }
            }
        }
        if (colors[0] == colors[4]) {
            return pair(1, 3);
        }
        if (lastDigit == 0 || lastDigit == 5) {
            return position(4);
        }
        if (!count(COLOR_RED) && !count(COLOR_PINK)) {
            return pair(0, 2);
        }
        if (coldCount > warmCount) {
            return lastTemperature(false);
        }
        return position(1);
    }

    // Сложная сложность, три активных провода.
    if (size == 3) {
        if (!(puzzle_.activeMask & wireBit(0)) &&
            !(puzzle_.activeMask & wireBit(4)) && even) {
            return position(1);
        }
        if (distinct == 1) {
            return pair(0, 2);
        }
        if (distinct == 2 && !repeats()) {
            for (uint8_t index = 0; index < size; ++index) {
                if (count(colors[index]) == 1) {
                    return position(index);
                }
            }
        }
        if (halfBigger && !warmCount) {
            return position(2);
        }
        if (count(COLOR_RED) == 1 && count(COLOR_PINK) == 1) {
            return first(COLOR_RED) | first(COLOR_PINK);
        }
        for (uint8_t index = 0; index < 4; ++index) {
            if (!(puzzle_.activeMask & wireBit(index)) &&
                !(puzzle_.activeMask & wireBit(index + 1))) {
                return position(2);
            }
        }
        if (count(COLOR_ORANGE) + count(COLOR_YELLOW) > 1) {
            for (uint8_t index = size; index > 0; --index) {
                if (colors[index - 1] == COLOR_ORANGE ||
                    colors[index - 1] == COLOR_YELLOW) {
                    return position(index - 1);
                }
            }
        }
        if (firstDigit == lastDigit) {
            return pair(0, 1);
        }
        if (coldCount == 1) {
            return lastTemperature(false);
        }
        return position(0);
    }

    // Сложная сложность, четыре активных провода.
    if (size == 4) {
        if (!(puzzle_.activeMask & wireBit(2)) && !lastEven) {
            return pair(1, 2);
        }
        if (even) {
            for (uint8_t index = 0; index < 3; ++index) {
                if (colors[index] == colors[index + 1]) {
                    return position(index);
                }
            }
        }
        if (distinct == 4 && !count(COLOR_WHITE)) {
            return position(3);
        }
        if (warmCount > coldCount && repeats()) {
            return lastTemperature(true);
        }
        if (count(COLOR_BLUE) > 1) {
            return first(COLOR_BLUE) | last(COLOR_BLUE);
        }
        if (colors[0] == colors[3]) {
            return position(1);
        }
        if (!coldCount && prime(firstDigit)) {
            return pair(2, 3);
        }
        if (distinct == 3) {
            for (uint8_t index = size; index > 0; --index) {
                if (count(colors[index - 1]) == 2) {
                    return position(index - 1);
                }
            }
        }
        for (uint8_t index = 0; index < 5; ++index) {
            if (!(puzzle_.activeMask & wireBit(index)) && hasDigit(index + 1)) {
                return position(3);
            }
        }
        return position(0);
    }

    // Сложная сложность, пять активных проводов.
    if (distinct == 2 && lastEven) {
        uint8_t result = 0;
        for (uint8_t index = 0; index < size; ++index) {
            if (count(colors[index]) <= 2) {
                result |= position(index);
            }
        }
        return result;
    }
    for (uint8_t index = 0; index < 3; ++index) {
        if (colors[index] == colors[index + 1] && colors[index] == colors[index + 2]) {
            return position(index + 1);
        }
    }
    if (colors[2] == COLOR_WHITE && !even) {
        return position(2);
    }
    if (warmCount == coldCount) {
        return last(COLOR_WHITE);
    }
    if (prime(firstDigit) && prime(lastDigit)) {
        return pair(0, 4);
    }
    if (colors[0] == colors[1] && colors[3] == colors[4]) {
        return position(2);
    }
    if (count(COLOR_YELLOW) == 1 && count(COLOR_ORANGE) == 1) {
        return first(COLOR_YELLOW) | first(COLOR_ORANGE);
    }
    if (halfBigger && coldCount) {
        return lastTemperature(false);
    }
    if (coldCount == 4) {
        for (uint8_t index = 0; index < size; ++index) {
            if (!cold(colors[index])) {
                return position(index);
            }
        }
    }
    return position(3);
}

} // namespace

uint8_t chooseTargets(const Puzzle &puzzle)
{
    if (puzzle.count < 3 || puzzle.count > 5 || puzzle.difficulty > HARD) {
        return 0;
    }

    uint8_t mask = 0;
    for (uint8_t index = 0; index < puzzle.count; ++index) {
        if (puzzle.positions[index] >= 5 || puzzle.colors[index] >= COLOR_COUNT) {
            return 0;
        }
        const uint8_t bit = wireBit(puzzle.positions[index]);
        if (mask & bit) {
            return 0;
        }
        mask |= bit;
    }
    if (mask != puzzle.activeMask) {
        return 0;
    }

    return Rules(puzzle).evaluate();
}

} // namespace Wires
