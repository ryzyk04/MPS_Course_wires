#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include "uart_protocol.h"
#include "module_bus.h"

// Uno: WS2812B (8 шт.) DIN -> D8; питание 5V, общий GND.
// Провода слева направо: D3..D7 -> разрываемый провод -> GND.
// Лента: 1..5 — цвета проводов, 6 — выключен, 7 — статус, 8 — сложность.
// A0 оставить свободным для генерации случайной раскладки.
enum ColorId : uint8_t { COLOR_WHITE, COLOR_PINK, COLOR_RED, COLOR_ORANGE, COLOR_YELLOW, COLOR_GREEN, COLOR_CYAN, COLOR_BLUE, COLOR_VIOLET, COLOR_COUNT };
enum Difficulty : uint8_t { EASY, MEDIUM, HARD };

// Сид и сложность приходят с сервера через ESP32.
uint16_t gameSeed;
Difficulty gameDifficulty;
constexpr bool DEBUG_SOLUTION = false;
constexpr uint8_t STRIP_BRIGHTNESS = 40;  // 0 — выключена, 255 — максимум
constexpr uint8_t STATUS_BRIGHTNESS = 30;  // Яркость индикаторов 7 и 8: 0..255
constexpr uint8_t WIRE_PINS[5] = {3,4,5,6,7};
constexpr uint8_t PIN_BUTTON = 9, PIN_UART_RX = 10, PIN_UART_TX = 11;
constexpr uint32_t UART_BAUD = 9600, UART_RETRY_MS = 500;
SoftwareSerial espLink(PIN_UART_RX, PIN_UART_TX);
UartLineReader espLines;
bool buttonRaw = false, buttonStable = false;
uint32_t buttonChangedAt = 0, buttonEventId = 0, pendingButtonId = 0, buttonSentAt = 0;
bool buttonRetryLogged = false;
constexpr uint8_t STATUS_PIXEL = 6, DIFFICULTY_PIXEL = 7;  // Индексы с нуля
constexpr uint16_t DEBOUNCE_MS = 40, READY_MS = 150, BLINK_MS = 400;
constexpr uint16_t BUTTON_RELEASE_MS = 10;
struct Color { uint8_t r, g, b; };

const Color WHITE  = {255, 255, 255};
const Color PINK   = {255,   0,  51};
const Color RED    = {255,   0,   0};
const Color ORANGE = {255,  51,   0};
const Color YELLOW = {255, 180,   0};
const Color GREEN  = {  0, 255,   0};
const Color CYAN   = {  0, 200, 255};  // голубой
const Color BLUE   = {  0,   0, 255};
const Color VIOLET = {162,   0, 230};
const Color OFF    = {  0,   0,   0};

// Порядок палитры соответствует ColorId; OFF не участвует в выборе цвета.
const Color PALETTE[COLOR_COUNT] = {
  WHITE, PINK, RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, VIOLET
};
Adafruit_NeoPixel strip(8, 8, NEO_GRB + NEO_KHZ800);
// colors и phys перечисляют только активные провода слева направо.
// phys сохраняет исходную физическую позицию (0..4).
uint8_t colors[5], phys[5], count, digits[4];
uint8_t activeMask, targetMask, cutMask;
bool rawOpen[5], stableOpen[5];
uint32_t changedAt[5], closedSince;
enum State : uint8_t { WAITING_FOR_WIRES, PLAYING, SOLVED, FAILED, WAITING_FOR_COMMAND };
State state = WAITING_FOR_COMMAND;
bool allClosed = false;
uint64_t lastGenerationId = 0;
uint64_t pendingResultId = 0;
bool pendingVictory = false;
uint32_t resultSentAt = 0;
void beginNewPuzzle(uint16_t seed, Difficulty level);

// Temporary 5 V I2C gateway. A4=SDA, A5=SCL, 100 kHz.
struct GatewayNode {
  uint8_t address, type, failures;
  bool online, ready, solved, resultAcked;
  uint16_t errors, ackedErrors;
  uint32_t errorSentAt, solvedSentAt;
};
GatewayNode nodes[2] = {
  {ModuleBus::SIMON_ADDRESS, ModuleBus::SIMON, 0, false, false, false, false, 0, 0, 0, 0},
  {ModuleBus::LABYRINTH_ADDRESS, ModuleBus::LABYRINTH, 0, false, false, false, false, 0, 0, 0, 0}
};
uint32_t lastBusPoll = 0;
uint8_t nextBusNode = 0;

void sendGatewayEvent(const GatewayNode &node, const char *event, uint64_t round, uint16_t value) {
  espLink.print(F("G:")); espLink.print(uint32_t(node.type)); espLink.print(':');
  espLink.print(event); espLink.print(':'); uartPrintId(espLink, round);
  espLink.print(':'); espLink.println(uint32_t(value));
}

void writeGatewayCommand(const GatewayNode &node, uint8_t operation) {
  ModuleBus::Frame frame;
  frame.type = node.type; frame.operation = operation;
  frame.round = lastGenerationId; frame.seed = gameSeed; frame.difficulty = gameDifficulty;
  uint8_t packet[ModuleBus::FRAME_SIZE]; ModuleBus::encode(frame, packet);
  Wire.beginTransmission(node.address); Wire.write(packet, sizeof(packet)); Wire.endTransmission();
}

void resetGatewayRound() {
  for (auto &node : nodes) {
    node.ready = node.solved = node.resultAcked = false;
    node.errors = node.ackedErrors = 0;
    node.errorSentAt = node.solvedSentAt = 0;
    if (node.online) writeGatewayCommand(node, ModuleBus::GENERATE);
  }
}

void updateGateway(uint32_t now) {
  if (now - lastBusPoll >= 25) {
    lastBusPoll = now;
    GatewayNode &node = nodes[nextBusNode]; nextBusNode ^= 1;
    uint8_t packet[ModuleBus::FRAME_SIZE], n = 0;
    Wire.requestFrom(node.address, uint8_t(sizeof(packet)));
    while (Wire.available()) {
      uint8_t value = Wire.read(); if (n < sizeof(packet)) packet[n++] = value;
    }
    ModuleBus::Frame frame;
    bool valid = ModuleBus::decode(packet, n, frame) && frame.operation == ModuleBus::STATUS && frame.type == node.type;
    if (!valid) {
      if (node.failures < 3) ++node.failures;
      if (node.failures == 3 && node.online) {
        node.online = false;
        sendGatewayEvent(node, "OFFLINE", lastGenerationId, 0);
      }
    } else {
      node.failures = 0;
      if (!node.online) { node.online = true; sendGatewayEvent(node, "ONLINE", frame.round, 0); }
      if (lastGenerationId) {
        if (frame.round != lastGenerationId || frame.seed != gameSeed || frame.difficulty != gameDifficulty) {
          node.ready = false;
          writeGatewayCommand(node, ModuleBus::GENERATE);
        } else {
          if (!node.ready) { node.ready = true; sendGatewayEvent(node, "READY", frame.round, 0); }
          if (frame.errors > node.errors) { node.errors = frame.errors; node.errorSentAt = now - UART_RETRY_MS; }
          bool solvedNow = frame.state == ModuleBus::SOLVED;
          if (solvedNow && !node.solved) node.solvedSentAt = now - UART_RETRY_MS;
          node.solved = solvedNow;
        }
      }
    }
  }
  if (!lastGenerationId) return;
  for (auto &node : nodes) {
    if (node.errors > node.ackedErrors && now - node.errorSentAt >= UART_RETRY_MS) {
      sendGatewayEvent(node, "ERROR", lastGenerationId, node.errors); node.errorSentAt = now;
    }
    if (node.solved && !node.resultAcked && now - node.solvedSentAt >= UART_RETRY_MS) {
      sendGatewayEvent(node, "SOLVED", lastGenerationId, 0); node.solvedSentAt = now;
    }
  }
}

void sendResultEvent(uint32_t now) {
  espLink.print(pendingVictory ? F("W:SOLVED:") : F("W:FAILED:"));
  uartPrintId(espLink, pendingResultId);
  espLink.println();
  resultSentAt = now;
}

void reportResult(bool victory, uint32_t now) {
  if (!lastGenerationId) return;
  pendingResultId = lastGenerationId;
  pendingVictory = victory;
  sendResultEvent(now);
}

void sendButtonEvent(uint32_t now) {
  espLink.print(F("W:BUTTON:"));
  espLink.println(pendingButtonId);
  buttonSentAt = now;
}

void handleEspLine(const char *line) {
  uint32_t id;
  uint64_t generationId;
  uint64_t resultAck;
  uint16_t seed;
  uint8_t level;
  uint8_t module; char event[8]; uint64_t round; uint16_t count;
  if (ModuleBus::parseEvent(line, module, event, round, count, "E:GACK:")) {
    if (round == lastGenerationId) for (auto &node : nodes) if (node.type == module) {
      if (!strcmp(event,"ERROR") && count <= node.errors && count > node.ackedErrors) node.ackedErrors = count;
      if (!strcmp(event,"SOLVED")) node.resultAcked = true;
    }
  } else if (uartParseId(line, "E:ACK:", id)) {
    if (id == pendingButtonId) {
      pendingButtonId = 0;
      Serial.print(F("UART: ESP подтвердил #"));
      Serial.println(id);
    }
  } else if (uartParseId(line, "E:RESULTACK:", resultAck)) {
    if (resultAck == pendingResultId) {
      pendingResultId = 0;
      Serial.println(F("UART: ESP подтвердил результат игры"));
    }
  } else if (uartParseGeneration(line, generationId, seed, level)) {
    if (generationId != lastGenerationId) {
      beginNewPuzzle(seed, static_cast<Difficulty>(level));
      lastGenerationId = generationId;
      resetGatewayRound();
    }
    // Echo validated decimal digits: AVR Print has no uint64_t overload.
    espLink.print(F("W:GENERATED:"));
    const char *digit = line + strlen("E:GENERATE:");
    while (*digit && *digit != ':') espLink.write(*digit++);
    espLink.println();
  } else if (!strcmp(line, "E:PING")) {
    Serial.println(F("UART: PING от ESP, отправляю PONG"));
    espLink.println(F("W:PONG"));
  } else {
    Serial.print(F("UART: неизвестная строка от ESP: "));
    Serial.println(line);
  }
}

void updateEspLink(uint32_t now) {
  for (uint8_t n = 0; n < 64 && espLink.available(); ++n)
    espLines.feed(char(espLink.read()), handleEspLine);
  if (pendingButtonId && now - buttonSentAt >= UART_RETRY_MS) {
    if (!buttonRetryLogged) {
      Serial.print(F("UART: нет ACK от ESP, повторяю #"));
      Serial.println(pendingButtonId);
      buttonRetryLogged = true;
    }
    sendButtonEvent(now);
  }
  if (pendingResultId && now - resultSentAt >= UART_RETRY_MS) sendResultEvent(now);
}

void scanButton(uint32_t now) {
  bool pressed = digitalRead(PIN_BUTTON) == LOW;
  if (pressed != buttonRaw) { buttonRaw = pressed; buttonChangedAt = now; }
  // A short release must rearm the next press, independently of puzzle state.
  uint16_t debounce = buttonRaw ? DEBOUNCE_MS : BUTTON_RELEASE_MS;
  if (buttonRaw == buttonStable || now - buttonChangedAt < debounce) return;
  buttonStable = buttonRaw;
  if (!buttonStable) return;
  if (++buttonEventId == 0) ++buttonEventId;
  pendingButtonId = buttonEventId;
  buttonRetryLogged = false;
  sendButtonEvent(now);
  Serial.print(F("UART D9: нажатие #"));
  Serial.println(buttonEventId);
}

uint8_t wireBit(uint8_t i) { return uint8_t(1U << i); }
uint8_t one(uint8_t i) { return wireBit(phys[i]); }
uint8_t two(uint8_t i, uint8_t j) { return one(i) | one(j); }
bool warm(uint8_t c) { return c == COLOR_PINK || c == COLOR_RED || c == COLOR_ORANGE || c == COLOR_YELLOW; }
bool cold(uint8_t c) { return c == COLOR_GREEN || c == COLOR_CYAN || c == COLOR_BLUE || c == COLOR_VIOLET; }
bool prime(uint8_t d) { return d == 2 || d == 3 || d == 5 || d == 7; }
uint8_t number(uint8_t c) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < count; ++i) n += colors[i] == c;
  return n;
}
uint8_t firstOf(uint8_t c) {
  for (uint8_t i = 0; i < count; ++i) if (colors[i] == c) return one(i);
  return 0;
}
uint8_t lastOf(uint8_t c) {
  for (uint8_t i = count; i > 0; --i) if (colors[i-1] == c) return one(i-1);
  return 0;
}
uint8_t lastTemperature(bool wantWarm) {
  for (uint8_t i = count; i > 0; --i)
    if (wantWarm ? warm(colors[i-1]) : cold(colors[i-1])) return one(i-1);
  return 0;
}
bool hasDigit(uint8_t d) {
  for (uint8_t i = 0; i < 4; ++i) if (digits[i] == d) return true;
  return false;
}
bool repeats() {
  for (uint8_t i = 0; i < 4; ++i)
    for (uint8_t j = i+1; j < 4; ++j) if (digits[i] == digits[j]) return true;
  return false;
}

// Маска физических проводов: строго первое подходящее правило руководства.
uint8_t chooseTargets() {
  const uint8_t first = digits[0], last = digits[3];
  const uint8_t sum = digits[0]+digits[1]+digits[2]+digits[3];
  const bool even = sum % 2 == 0, lastEven = last % 2 == 0;
  const bool halfBigger = digits[0]+digits[1] > digits[2]+digits[3];
  uint8_t w = 0, c = 0, distinct = 0;
  for (uint8_t i = 0; i < count; ++i) { w += warm(colors[i]); c += cold(colors[i]); }
  for (uint8_t i = 0; i < COLOR_COUNT; ++i) distinct += number(i) > 0;
  if (gameDifficulty == EASY) {
    if (count == 3) {
      if (!number(COLOR_RED) && !lastEven) return one(1);
      if (colors[2] == COLOR_WHITE) return one(2);
      if (number(COLOR_BLUE) > 1) return lastOf(COLOR_BLUE);
      if (hasDigit(0)) return two(0,2);
      return one(0);
    }
    if (count == 4) {
      if (number(COLOR_RED) > 1 && lastEven) return lastOf(COLOR_RED);
      if (colors[0] == COLOR_YELLOW && !number(COLOR_GREEN)) return two(0,3);
      if (number(COLOR_BLUE) == 1) return firstOf(COLOR_BLUE);
      if (first > last) return one(2);
      return one(1);
    }
    if (colors[4] == COLOR_VIOLET && !lastEven) return one(3);
    if (number(COLOR_WHITE) > 1) return firstOf(COLOR_WHITE) | lastOf(COLOR_WHITE);
    if (number(COLOR_RED) == 1 && number(COLOR_GREEN) > 1) return firstOf(COLOR_RED);
    if (hasDigit(0)) return two(1,4);
    return one(2);
  }
  if (gameDifficulty == MEDIUM) {
    if (count == 3) {
      if (distinct == 1) return one(1);
      if (colors[0] == colors[2] && even) return two(0,2);
      if (!w) return one(2);
      if (number(COLOR_PINK) > 1) return firstOf(COLOR_PINK);
      if (cold(colors[1]) && first % 2 == 0) return two(1,2);
      if (last == 3 || last > 7) return one(0);
      return one(2);
    }
    if (count == 4) {
      if (w == 2 && c == 2) return two(1,2);
      if (colors[0] == colors[1]) return one(0);
      if (number(COLOR_RED) > 1 && sum > 18) return lastOf(COLOR_RED);
      if (number(COLOR_VIOLET) == 1 && !lastEven) return firstOf(COLOR_VIOLET);
      if (colors[3] == COLOR_WHITE || colors[3] == COLOR_PINK) return two(0,3);
      if (number(COLOR_GREEN) > 1) return firstOf(COLOR_GREEN) | lastOf(COLOR_GREEN);
      return one(0);
    }
    if (distinct == 5 && !even) return one(2);
    if (w == 4) for (uint8_t i = 0; i < count; ++i) if (!warm(colors[i])) return one(i);
    if (colors[0] == colors[4]) return two(1,3);
    if (last == 0 || last == 5) return one(4);
    if (!number(COLOR_RED) && !number(COLOR_PINK)) return two(0,2);
    if (c > w) return lastTemperature(false);
    return one(1);
  }
  if (count == 3) {
    if (!(activeMask & wireBit(0)) && !(activeMask & wireBit(4)) && even) return one(1);
    if (distinct == 1) return two(0,2);
    if (distinct == 2 && !repeats())
      for (uint8_t i = 0; i < count; ++i) if (number(colors[i]) == 1) return one(i);
    if (halfBigger && !w) return one(2);
    if (number(COLOR_RED) == 1 && number(COLOR_PINK) == 1) return firstOf(COLOR_RED) | firstOf(COLOR_PINK);
    for (uint8_t i = 0; i < 4; ++i)
      if (!(activeMask & wireBit(i)) && !(activeMask & wireBit(i+1))) return one(2);
    if (number(COLOR_ORANGE)+number(COLOR_YELLOW) > 1)
      for (uint8_t i = count; i > 0; --i)
        if (colors[i-1] == COLOR_ORANGE || colors[i-1] == COLOR_YELLOW) return one(i-1);
    if (first == last) return two(0,1);
    if (c == 1) return lastTemperature(false);
    return one(0);
  }
  if (count == 4) {
    if (!(activeMask & wireBit(2)) && !lastEven) return two(1,2);
    if (even) for (uint8_t i = 0; i < 3; ++i)
      if (colors[i] == colors[i+1]) return one(i);
    if (distinct == 4 && !number(COLOR_WHITE)) return one(3);
    if (w > c && repeats()) return lastTemperature(true);
    if (number(COLOR_BLUE) > 1) return firstOf(COLOR_BLUE) | lastOf(COLOR_BLUE);
    if (colors[0] == colors[3]) return one(1);
    if (!c && prime(first)) return two(2,3);
    if (distinct == 3)
      for (uint8_t i = count; i > 0; --i) if (number(colors[i-1]) == 2) return one(i-1);
    for (uint8_t i = 0; i < 5; ++i)
      if (!(activeMask & wireBit(i)) && hasDigit(i+1)) return one(3);
    return one(0);
  }
  if (distinct == 2 && lastEven) {
    uint8_t result = 0;
    for (uint8_t i = 0; i < count; ++i) if (number(colors[i]) <= 2) result |= one(i);
    return result;
  }
  for (uint8_t i = 0; i < 3; ++i)
    if (colors[i] == colors[i+1] && colors[i] == colors[i+2]) return one(i+1);
  if (colors[2] == COLOR_WHITE && !even) return one(2);
  if (w == c) return lastOf(COLOR_WHITE);
  if (prime(first) && prime(last)) return two(0,4);
  if (colors[0] == colors[1] && colors[3] == colors[4]) return one(2);
  if (number(COLOR_YELLOW) == 1 && number(COLOR_ORANGE) == 1) return firstOf(COLOR_YELLOW) | firstOf(COLOR_ORANGE);
  if (halfBigger && c) return lastTemperature(false);
  if (c == 4) for (uint8_t i = 0; i < count; ++i) if (!cold(colors[i])) return one(i);
  return one(3);
}

uint32_t pixelColor(const Color &rgb, uint8_t brightness) {
  return strip.Color(uint16_t(rgb.r) * brightness / 255,
                     uint16_t(rgb.g) * brightness / 255,
                     uint16_t(rgb.b) * brightness / 255);
}

void setStatus(const Color &rgb) {
  static uint32_t previous = 0xFFFFFFFFUL;
  uint32_t color = pixelColor(rgb, STATUS_BRIGHTNESS);
  if (color == previous) return;
  strip.setPixelColor(STATUS_PIXEL, color);
  strip.show();
  previous = color;
}

void generatePuzzle(uint16_t requestedSeed, Difficulty level) {
  gameSeed = requestedSeed;
  gameDifficulty = level;
  randomSeed(uint32_t(gameSeed) + 1);  // AVR randomSeed(0) ignores zero.
  uint16_t seed = gameSeed;
  for (int8_t i = 3; i >= 0; --i) { digits[i] = seed % 10; seed /= 10; }
  uint8_t positions[5] = {0,1,2,3,4};
  for (uint8_t i = 4; i > 0; --i) {
    uint8_t j = random(i+1), tmp = positions[i];
    positions[i] = positions[j]; positions[j] = tmp;
  }
  count = random(3,6);
  activeMask = 0;
  for (uint8_t i = 0; i < count; ++i) activeMask |= wireBit(positions[i]);
  uint8_t a = 0;
  for (uint8_t p = 0; p < 5; ++p) {
    if (!(activeMask & wireBit(p))) {
      strip.setPixelColor(p, pixelColor(OFF, STRIP_BRIGHTNESS));
      continue;
    }
    phys[a] = p;
    colors[a] = random(COLOR_COUNT);
    const Color &rgb = PALETTE[colors[a]];
    strip.setPixelColor(p, pixelColor(rgb, STRIP_BRIGHTNESS));
    ++a;
  }
  targetMask = chooseTargets();
  cutMask = 0;
  strip.setPixelColor(5, pixelColor(OFF, STATUS_BRIGHTNESS));
  const Color &difficultyColor = gameDifficulty == EASY ? GREEN : gameDifficulty == MEDIUM ? YELLOW : RED;
  strip.setPixelColor(DIFFICULTY_PIXEL, pixelColor(difficultyColor, STATUS_BRIGHTNESS));
  strip.show();
  Serial.print(F("Seed: "));
  for (uint8_t i = 0; i < 4; ++i) Serial.print(digits[i]);
  Serial.print(F("  Уровень: "));
  Serial.println(gameDifficulty == EASY ? F("лёгкий") :
                 gameDifficulty == MEDIUM ? F("средний") : F("сложный"));
  if (DEBUG_SOLUTION) {
    Serial.print(F("Cut physical positions: "));
    for (uint8_t p = 0; p < 5; ++p) if (targetMask & wireBit(p)) Serial.print(p+1);
    Serial.println();
  }
}

void beginNewPuzzle(uint16_t seed, Difficulty level) {
  pendingResultId = 0;
  uint32_t now = millis();
  for (uint8_t i = 0; i < 5; ++i) {
    rawOpen[i] = stableOpen[i] = digitalRead(WIRE_PINS[i]) == HIGH;
    changedAt[i] = now;
  }
  allClosed = false;
  closedSince = now;
  state = WAITING_FOR_WIRES;
  generatePuzzle(seed, level);
  setStatus(YELLOW);
  Serial.println(F("Задача получена от ESP. Подключите все пять проводов."));
}

void scanWires(uint32_t now) {
  if (state == WAITING_FOR_COMMAND) return;
  uint8_t newCuts = 0;
  bool anyOpen = false;
  for (uint8_t i = 0; i < 5; ++i) {
    bool open = digitalRead(WIRE_PINS[i]) == HIGH;
    anyOpen |= open;
    if (open != rawOpen[i]) { rawOpen[i] = open; changedAt[i] = now; }
    if (open != stableOpen[i] && now-changedAt[i] >= DEBOUNCE_MS) {
      stableOpen[i] = open;
      if (open) newCuts |= wireBit(i);
    }
  }
  if (state == WAITING_FOR_WIRES) {
    if (anyOpen) { allClosed = false; return; }
    if (!allClosed) { allClosed = true; closedSince = now; }
    if (now-closedSince >= READY_MS) {
      state = PLAYING;
      Serial.println(F("READY"));
    }
    return;
  }
  if (state != PLAYING) return;
  // Ошибка имеет приоритет над победой при нескольких разрывах за один опрос.
  cutMask |= newCuts & activeMask;
  if (cutMask & uint8_t(~targetMask)) {
    state = FAILED;
    Serial.println(F("FAILED"));
    reportResult(false, now);
  } else if ((cutMask & targetMask) == targetMask) {
    // Подождать, если лишний разрыв ещё проходит антидребезг.
    for (uint8_t i = 0; i < 5; ++i)
      if ((activeMask & wireBit(i)) && !(targetMask & wireBit(i)) && rawOpen[i]) return;
    state = SOLVED;
    Serial.println(F("SOLVED"));
    reportResult(true, now);
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(); Wire.setClock(100000); Wire.setWireTimeout(2500, true);
  espLink.begin(UART_BAUD);
  Serial.println(F("UART WIRES v1: RX=D10 TX=D11 9600; кнопка=D9"));
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  buttonRaw = buttonStable = digitalRead(PIN_BUTTON) == LOW;
  buttonChangedAt = millis();
  for (uint8_t i = 0; i < 5; ++i) {
    pinMode(WIRE_PINS[i], INPUT_PULLUP);
    rawOpen[i] = stableOpen[i] = digitalRead(WIRE_PINS[i]) == HIGH;
    changedAt[i] = millis();
  }
  uint32_t entropy = micros();
  for (uint8_t i = 0; i < 16; ++i) entropy = entropy*31 + analogRead(A0) + micros();
  randomSeed(entropy);
  strip.begin();
  // Яркость задаётся для каждого пикселя отдельно в pixelColor().
  strip.setBrightness(255);
  strip.clear();
  strip.show();
  // A fresh event ID range avoids confusing a Uno restart with a retransmission.
  buttonEventId = uint32_t(random(1, 2147483647L));
  Serial.println(F("WAITING_FOR_COMMAND: жду новую задачу с веба через ESP."));
}

void loop() {
  uint32_t now = millis();
  updateEspLink(now);
  scanButton(now);
  scanWires(now);
  updateGateway(now);
  if (state == WAITING_FOR_COMMAND) return;  // Кнопка и UART работают и без задачи.
  if (state == SOLVED) setStatus(GREEN);
  else if (state == FAILED) setStatus(RED);
  else {
    bool on = (now / BLINK_MS) % 2 == 0;
    setStatus(on ? YELLOW : OFF);  // Жёлтое мигание во время решения.
  }
}
