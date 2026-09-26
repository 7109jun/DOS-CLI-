#!/usr/bin/env python3
"""DOS official Python plugin bridge."""
from __future__ import annotations

import runpy
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) >= 2 and sys.argv[1] == "--version":
        print("DOS Python Plugin 1.0")
        return 0
    if len(sys.argv) >= 3 and sys.argv[1] == "--exec":
        try:
            exec(compile(sys.argv[2], "<dos-python>", "exec"), {"__name__": "__main__"}, {})
            return 0
        except SystemExit as exc:
            value = exc.code
            return int(value) if isinstance(value, int) else 0
        except Exception as exc:
            print(f"DOS Python plugin error: {exc}", file=sys.stderr)
            return 1
    if len(sys.argv) < 3 or sys.argv[1] != "--run":
        print("usage: plugin.py --run script.py [args...] | --exec code", file=sys.stderr)
        return 2
    script = Path(sys.argv[2]).resolve()
    if not script.is_file():
        print(f"DOS Python plugin: script not found: {script}", file=sys.stderr)
        return 2
    sys.argv = [str(script), *sys.argv[3:]]
    try:
        runpy.run_path(str(script), run_name="__main__")
        return 0
    except SystemExit as exc:
        value = exc.code
        return int(value) if isinstance(value, int) else 0
    except Exception as exc:
        print(f"DOS Python plugin error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
