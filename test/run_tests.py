"""Build and run native wires checks on a computer. Requires Python and GCC."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--group", choices=("all", "logic", "bus", "hardware"), default="all")
    options = parser.parse_args(arguments)
    root = Path(__file__).resolve().parents[1]
    out = root / ".pio" / "native-tests"
    out.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which("g++")
    if not compiler:
        parser.error("g++ is required")

    common = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
              "-I", str(root / "include"), "-I", str(root / "test" / "host")]

    def build_and_run(name, sources, extra_flags=(), run_arguments=()):
        executable = out / (name + (".exe" if sys.platform == "win32" else ""))
        subprocess.run([compiler, *common, *extra_flags,
                        *[str(root / source) for source in sources],
                        "-o", str(executable)], check=True, timeout=120)
        subprocess.run([str(executable), *run_arguments], check=True, timeout=120)

    if options.group != "hardware":
        sources = [
            "test/host/core_tests.cpp", "test/host/MockBoard.cpp",
            "src/game/Puzzle.cpp", "src/game/Rules.cpp", "src/game/Game.cpp",
            "src/game/Display.cpp", "src/communication/Text.cpp",
            "src/communication/Gateway.cpp", "src/communication/EspLink.cpp",
        ]
        build_and_run("core", sources, run_arguments=(options.group,))

    if options.group in ("all", "hardware"):
        flags = ("-DF_CPU=16000000UL", "-I", str(root / "test" / "host" / "stubs"))
        build_and_run("uart", [
            "test/host/uart_tests.cpp", "src/drivers/EspUart.cpp",
            "src/drivers/Clock.cpp", "src/drivers/Board.cpp",
        ], flags)
        build_and_run("twi", ["test/host/twi_tests.cpp", "src/drivers/Twi.cpp"], flags)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
