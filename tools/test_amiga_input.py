#!/usr/bin/env python3
"""Compile the production Amiga input implementation against host register stubs."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADERS = ("proto/exec.h", "exec/interrupts.h", "exec/nodes.h",
           "resources/cia.h", "proto/cia.h", "hardware/cia.h")
with tempfile.TemporaryDirectory(prefix="revs-input-") as tmp:
    build = Path(tmp)
    for name in HEADERS:
        header = build / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text("/* Declarations supplied by amiga_input_mock.h. */\n")
    executable = build / "input-test"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                    "-I", tmp, "-include", str(ROOT / "tools/amiga_input_mock.h"),
                    str(ROOT / "tools/amiga_input_test.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
