#!/usr/bin/env python3
"""Headless model + test-resource protocol; does not launch GTA or operate UI."""
from pathlib import Path
import json
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="neon-native-ui-") as temp:
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("C++17 compiler is required")
    binary = Path(temp) / "model"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-IClient/sdk", "-IClient/game_sa",
                    "Tests/native-ui/model.cpp", "-o", str(binary)], cwd=root, check=True)
    subprocess.run([str(binary)], cwd=root, check=True)
lua = shutil.which("lua5.1") or shutil.which("lua")
if not lua:
    raise SystemExit("Lua is required")
subprocess.run([lua, "Tests/native-ui/resource_protocol.lua"], cwd=root, check=True)
for name in ("native-ui-test/client.lua", "native-ui-test/server.lua", "native-ui-test-peer/client.lua"):
    subprocess.run([lua, "-e", "assert(loadfile(" + json.dumps("test-resources/" + name) + "))"], cwd=root, check=True)
print("Native UI headless checks PASS; visual/native lifecycle checks remain manual.")
