#!/usr/bin/env python3

##
# @file run_combined.py
# @details Try to run eventlog and EVB pipe from one script.
#

## @todo (ASC 3/7/25): Configure via command line.
## @todo (ASC 3/7/25): Setup all recv/sorters with one evb framework app.

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
# Event builder:
#

evb_init_path = os.path.dirname(os.path.realpath(__file__))
evb_cmd = f"{evb_init_path}/setup_evb.sh"
evb_args = shlex.split(evb_cmd)
print(f"Running evtbuild command: {evb_cmd}")
    
##
# eventlog to write data to disk. Note that this will write data
# into the current working directory:
#

evtlog_cmd = f"{daqbin}/eventlog -s tcp://localhost/frib_e2sar_evb " \
    f"--number-of-sources=1 --oneshot"
evtlog_args = shlex.split(evtlog_cmd)
print(f"Running eventlog command: {evtlog_cmd}")

# Keep evb open, restart after run end:

while True:
    evb_proc = subprocess.Popen(evb_args, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
    print("Recording run...",end="")
    sys.stdout.flush()
    evtlog_proc = subprocess.run(evtlog_args, capture_output=True)      
    print("done")
    sys.stdout.flush()
    
    try:
        evtlog_proc.check_returncode()
    except subprocess.CalledProcessError as e:
        print(f"ERROR: {e} {evtlog_proc.stdout.decode('utf-8')} " \
              f"{evtlog_proc.stderr.decode('utf-8')}")
        print("Killing evb...")
        evb_proc.kill()
        sys.exit(1)
    finally:
        print(f"{evtlog_cmd} completed with returncode " \
              f"{evtlog_proc.returncode}")
        evb_proc.kill()
        evb_proc.wait()
        print(f"{evb_cmd} completed with returncode "\
              f"{evb_proc.returncode}")
        time.sleep(1)
        print("Restarting evb pipeline...")
