#!/usr/bin/env python3

##
# @file run_evb.py
# @details Run event builder for FRIB-E2SAR workflow.
# @note Deprecated 3/7/25: use run_combined.py instead.
#

import os
import shlex
import signal
import subprocess
import sys
import time

# SIGINT is propagated to subprocesses, just exit:
def handler(signum, frame):
    signame = signal.Signals(signum).name
    print(f"\nSignal handler called with signal {signame} ({signum})")
    sys.exit(0)

signal.signal(signal.SIGINT, handler)

# NSCLDAQ needed to create evb sources:
daqbin = os.getenv("DAQBIN")
if daqbin is None:
    print("DAQBIN must be defined!")
    sys.exit(1)
else:
    print(f"Using DAQBIN: {daqbin}")
    
##
# Event building:
#

path = os.path.dirname(os.path.realpath(__file__))
cmd = f"{path}/setup_evb.sh"
args = shlex.split(cmd)
print(f"Running evtbuild command: {cmd}")

proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

while proc.poll() == None:
    time.sleep(1)

if proc.returncode:
    print(f"ERROR: process terminated with returncode {proc.returncode}")
    proc.kill()
    stdout, _ = proc.communicate()
    print(f"{stdout.decode('utf-8')}")
