#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Wires {
namespace Hardware {

enum class Diagnostic : uint8_t {
    Startup, Ready, Solved, Failed, NodeOk, NodeMissing, NodeInvalid, NodeWrong
};

// ATmega328P, 16 МГц. GPIO, аппаратный USART для USB и диапазоны ID в EEPROM.
void beginBoard();
uint8_t openWires();
bool buttonPressed();
uint32_t nextButtonId();
void debug(Diagnostic event, uint8_t module = 0);
void debugRound(uint16_t seed, uint8_t difficulty, uint8_t targets);

// Timer0 даёт миллисекунды и микросекунды с учётом ожидающего прерывания.
void beginClock();
uint32_t millis();
uint32_t micros();

// Timer1 и PCINT0 обслуживают UART 9600 8N1 на PB2/PB3 (D10/D11).
void beginEspUart();
bool readEsp(uint8_t &byte);
void writeEsp(const char *text);
bool takeReceiveFault();
bool espIdle();
bool espReceivePending();
uint32_t lastReceiveAt();

// Синхронный TWI master. Любой сбой завершает транзакцию с ограничением времени.
void beginTwi();
bool writeTwi(uint8_t address, const uint8_t *data, uint8_t size);
bool readTwi(uint8_t address, uint8_t *data, uint8_t size);

// Возвращает false, если передачу пришлось отложить или прервать ради UART.
bool showPixels(const uint8_t *grb, uint8_t size);

} // namespace Hardware
} // namespace Wires
