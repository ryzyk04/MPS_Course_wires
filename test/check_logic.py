"""Compatibility entry point for the game and rule checks."""
from run_tests import main

if __name__ == "__main__":
    raise SystemExit(main(["--group", "logic"]))
