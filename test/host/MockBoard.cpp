#include "MockBoard.h"
#include "drivers/Board.h"

namespace Wires {
namespace Hardware {

void beginBoard() {}
void beginClock() {}
void beginEspUart() {}
void beginTwi() {}
uint32_t millis() { return 0; }
uint32_t micros() { return 0; }
uint8_t openWires() { return 0; }
bool buttonPressed() { return false; }
uint32_t nextButtonId() { return ++Mock::id; }
void debug(Diagnostic, uint8_t) {}
void debugRound(uint16_t, uint8_t, uint8_t) {}
bool espIdle() { return true; }
bool espReceivePending() { return false; }
uint32_t lastReceiveAt() { return 0; }

bool readEsp(uint8_t &value)
{
    if (Mock::input.empty()) {
        return false;
    }
    value = Mock::input.front();
    Mock::input.pop_front();
    return true;
}

void writeEsp(const char *text)
{
    Mock::output += text;
}

bool takeReceiveFault()
{
    const bool fault = Mock::fault;
    Mock::fault = false;
    return fault;
}

bool writeTwi(uint8_t address, const uint8_t *data, uint8_t size)
{
    Mock::writes[address] = {data, data + size};
    if (Mock::writeHook) { return Mock::writeHook(address, data, size); }
    return true;
}

bool readTwi(uint8_t address, uint8_t *data, uint8_t size)
{
    if (Mock::readHook) { return Mock::readHook(address, data, size); }
    const auto found = Mock::replies.find(address);
    if (found == Mock::replies.end() || found->second.size() != size) {
        return false;
    }
    for (uint8_t index = 0; index < size; ++index) {
        data[index] = found->second[index];
    }
    return true;
}

bool showPixels(const uint8_t *data, uint8_t size)
{
    ++Mock::showCalls;
    Mock::pixels.assign(data, data + size);
    return Mock::showSuccess;
}

} // namespace Hardware
} // namespace Wires
