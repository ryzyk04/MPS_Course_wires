#pragma once

#include <stdint.h>

namespace Wires {
namespace Config {

// Игровые задержки и яркость.
constexpr uint16_t WIRE_DEBOUNCE_MS = 40;
constexpr uint16_t BUTTON_PRESS_MS = 40;
constexpr uint16_t BUTTON_RELEASE_MS = 10;
constexpr uint16_t READY_MS = 150;
constexpr uint16_t BLINK_MS = 400;
constexpr uint8_t STRIP_BRIGHTNESS = 40;
constexpr uint8_t STATUS_BRIGHTNESS = 30;
constexpr bool DEBUG_SOLUTION = false;

// Совместимость с существующим ESP32 и двумя ведомыми модулями.
constexpr uint32_t UART_BAUD = 9600;
constexpr uint16_t UART_RETRY_MS = 500;
constexpr uint16_t BUS_POLL_MS = 25;
constexpr uint8_t BUS_FAILURE_LIMIT = 3;
constexpr uint32_t TWI_SPEED_HZ = 100000;
constexpr uint16_t TWI_TIMEOUT_US = 2500;

// Лента передаётся после затишья UART. Входящий старт прерывает кадр ленты.
constexpr uint16_t LED_UART_QUIET_MS = 2;
constexpr uint16_t LED_RESET_US = 300;

constexpr uint8_t STATUS_PIXEL = 6;
constexpr uint8_t DIFFICULTY_PIXEL = 7;
constexpr uint8_t UART_BUFFER_SIZE = 128;

static_assert(BLINK_MS > 0 && UART_RETRY_MS > 0 && BUS_POLL_MS > 0,
              "Blink, retry and polling intervals must be positive");
static_assert(UART_BAUD > 0, "UART baud rate must be positive");
static_assert(STATUS_PIXEL >= 6 && STATUS_PIXEL < 8 &&
              DIFFICULTY_PIXEL >= 6 && DIFFICULTY_PIXEL < 8 &&
              STATUS_PIXEL != DIFFICULTY_PIXEL,
              "Status and difficulty must use distinct pixels 7 and 8");

} // namespace Config
} // namespace Wires
