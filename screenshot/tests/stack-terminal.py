#!/usr/bin/python3
"""Task-owned Konsole child: prints a selection and records input without executing it."""
import os
import pathlib
import sys
import tty
print('konsole-selected', flush=True)
tty.setraw(0)
path = pathlib.Path(sys.argv[1])
path.with_suffix('.ready').write_text('ready')
with path.open('wb', buffering=0) as output:
    while True:
        data = os.read(0, 4096)
        if not data: break
        output.write(data)
