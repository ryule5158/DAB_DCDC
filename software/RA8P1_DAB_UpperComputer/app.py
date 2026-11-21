#!/usr/bin/env python3
"""Entry point for the RA8P1 DAB demonstration host."""

from __future__ import annotations

import argparse
import sys
import tkinter as tk
import unittest
from pathlib import Path


APP_ROOT = Path(__file__).resolve().parent


def run_self_test() -> int:
    suite = unittest.defaultTestLoader.discover(str(APP_ROOT / "tests"), pattern="test_*.py")
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="run protocol/command-plan tests and exit")
    args = parser.parse_args()
    if args.self_test:
        return run_self_test()

    from dab_console.ui import DABHostApp

    root = tk.Tk()
    DABHostApp(root, APP_ROOT)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
