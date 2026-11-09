#include <avr/interrupt.h>

#include "drivers/Board.h"
#include "game/Game.h"
#include "game/Display.h"
#include "communication/Gateway.h"
#include "communication/EspLink.h"

namespace {

Wires::Game game;
Wires::Display display;
Wires::Gateway gateway;
Wires::EspLink link(game, gateway);

} // namespace

int main()
{
    using namespace Wires;

    Hardware::beginBoard();
    Hardware::beginClock();
    Hardware::beginEspUart();
    Hardware::beginTwi();
    sei();

    link.begin(Hardware::buttonPressed(), Hardware::millis());
    Hardware::debug(Hardware::Diagnostic::Startup);

    for (;;) {
        const uint32_t now = Hardware::millis();
        const uint8_t openMask = Hardware::openWires();
        link.update(now, openMask, Hardware::buttonPressed());

        const State before = game.state();
        const Result result = game.update(now, openMask);
        if (before == State::WaitingForWires && game.state() == State::Playing) {
            Hardware::debug(Hardware::Diagnostic::Ready);
        }
        if (result != Result::None) {
            Hardware::debug(result == Result::Solved ? Hardware::Diagnostic::Solved :
                                                       Hardware::Diagnostic::Failed);
            link.report(result, now);
        }

        gateway.update(now);
        display.update(game, now);
    }
}
