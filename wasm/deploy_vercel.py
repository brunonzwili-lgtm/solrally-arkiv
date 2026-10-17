#!/usr/bin/env python3
"""Run `vercel deploy --prod --yes --archive=tgz` fully detached.

Double-fork + setsid so a timeout in the calling shell cannot kill the
upload (the same reason serve.py exists for the dev server).
"""
import os
import sys

ROOT = "/Users/h/Documents/stk-code-wasm"
LOG = "/tmp/vercel_deploy.log"

if os.fork() > 0:
    os._exit(0)

os.setsid()

if os.fork() > 0:
    os._exit(0)

os.chdir(ROOT)
log = os.open(LOG, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
os.dup2(log, 1)
os.dup2(log, 2)
devnull = os.open(os.devnull, os.O_RDONLY)
os.dup2(devnull, 0)

os.execvp("vercel", ["vercel", "deploy", "--prod", "--yes", "--archive=tgz"])