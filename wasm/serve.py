#!/usr/bin/env python3
"""Start the STK wasm dev server fully detached.

Double-fork + setsid puts the server in its own session so that a timeout or
Ctrl-C in the calling shell cannot kill it with SIGTERM (which is what kept
tearing the server down mid-session).
"""
import os
import subprocess
import sys

SERVER = "/Users/h/Documents/stk-code-wasm/wasm/run_server.py"
PORT = sys.argv[1] if len(sys.argv) > 1 else "8000"
LOG = "/tmp/stk_server.log"

# Kill anything already holding the port.
subprocess.run(["pkill", "-f", "run_server.py"], check=False)

pid = os.fork()
if pid > 0:
    os.waitpid(pid, 0)
    sys.exit(0)

os.setsid()

pid = os.fork()
if pid > 0:
    os._exit(0)

os.chdir("/")
log = os.open(LOG, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
os.dup2(log, 1)
os.dup2(log, 2)
devnull = os.open(os.devnull, os.O_RDONLY)
os.dup2(devnull, 0)
os.execv(sys.executable, [sys.executable, SERVER, PORT])