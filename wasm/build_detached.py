#!/usr/bin/env python3
"""Rebuild the WASM build fully detached.

An incremental rebuild (one .cpp changed) still needs to relink a 15MB
binary, which outlasts a shell timeout, so mirror serve.py's detach.
"""
import os

WASM_DIR = "/Users/h/Documents/stk-code-wasm/wasm"
LOG = "/tmp/stk_build.log"

if os.fork() > 0:
    os._exit(0)

os.setsid()

if os.fork() > 0:
    os._exit(0)

os.chdir(WASM_DIR)
log = os.open(LOG, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
os.dup2(log, 1)
os.dup2(log, 2)
devnull = os.open(os.devnull, os.O_RDONLY)
os.dup2(devnull, 0)

os.execv("/bin/bash", ["bash", "./build.sh", "Release"])