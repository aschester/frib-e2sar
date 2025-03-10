#!/usr/bin/env python3

##
# @file run_eventlog.py
# @details Run event build and logging pipeline for E2SAR evb workflow
# @note Deprecated 3/7/25: use run_combined.py instead.
#

import os
import shlex
import signal
import subprocess
import sys
import time
import tkinter

tcl_interp = tkinter.Tcl()

tcl_cmd = """
# Start Tclsh \
    exec /usr/bin/wish ${0} ${@}
lappend auto_path [file join $::env(DAQROOT) TclLibs]
package require Tk
package require EventBuilder
package require EVB::connectionList
package require EVB::GUI
package require evbcallouts
package require ring
package require ui
set port [EVBC::getOrdererPort]
puts $port
"""
print(tcl_cmd)
res = tcl_interp.eval(tcl_cmd)
sys.exit(1)

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
# eventlog to write data to disk. Note that this will write data
# into the current working directory:
#

# --oneshot exits after single run, creates .started, .exited files in dir
cmd = f"{daqbin}/eventlog -s tcp://localhost/frib_e2sar_evb --number-of-sources=1 --oneshot"
args = shlex.split(cmd)
print(f"Running eventlog command: {cmd}")

while True:
    print("Recording run...",end="")
    sys.stdout.flush()
    proc = subprocess.run(args, capture_output=True)                    
    print("done")
    sys.stdout.flush()
    
    try:
        proc.check_returncode()
    except subprocess.CalledProcessError as e:
        print(f"ERROR: {e} {proc.stdout.decode('utf-8')} " \
              f"{proc.stderr.decode('utf-8')}")
        sys.exit(1)
    finally:
        print(f"{cmd} completed with returncode {proc.returncode}")
        print("Restarting ring fragment source...")
        
sys.exit(0)
