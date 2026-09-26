#!/usr/bin/env python3
from pathlib import Path
import random
import subprocess
import tempfile

DOS = Path(__file__).resolve().parents[1] / 'build-final' / 'dos'
TOKENS = [
    '{', '}', '(', ')', '[', ']', ',', '.', ':', '+', '-', '*', '/', '%',
    '&', '|', '^', '!', '=', '==', '!=', '<', '>', '<<', '>>', '?',
    'if', 'else', 'while', 'for', 'return', 'struct', 'function',
    'int', 'string', 'bool', 'var', 'true', 'false', 'null',
    'x', 'y', '0', '1', '2', '"x"'
]

random.seed(0xD05)
failures = 0
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    for i in range(100):
        count = random.randint(1, 40)
        source = ' '.join(random.choice(TOKENS) for _ in range(count)) + '\n'
        path = td / f'{i}.dos'
        path.write_text(source, encoding='utf-8')
        p = subprocess.run(
            [str(DOS), 'run', str(path)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=2,
        )
        if p.returncode < 0:
            failures += 1
            print(f'CRASH case={i} signal={-p.returncode}')
print(f'FUZZ_CASES=100')
print(f'CRASHES={failures}')
raise SystemExit(1 if failures else 0)
