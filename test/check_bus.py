"""Compatibility entry point for the UART and gateway checks."""
from run_tests import main

if __name__ == "__main__":
    raise SystemExit(main(["--group", "bus"]))
