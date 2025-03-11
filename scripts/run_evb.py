#!/usr/bin/env python3

##
# @file run_combined.py
# @details Run EVB pipe and eventlog for FRIB-E2SAR workflows.
#

## @todo (ASC 3/7/25): Setup all recv/sorters with one evb framework app.

##
# @note (ASC 3/11/25): EVB on localhost, which may be different than the
# Reassembly and sorting host machine(s). See setup_evb.sh where ring sources
# for the event building are defined.
#

import argparse
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

# Creates the sink ring if it doesnt already exist:
def create_sink(sink_name):
    daqbin = os.getenv("DAQBIN")
    cmd = f"{daqbin}/ringbuffer list"
    proc = subprocess.run(shlex.split(cmd), capture_output=True, text=True)
    try:
        proc.check_returncode() # This really ought never to fail...
    except subprocess.CalledProcessError as e:
        print(f"ERROR: {e} {proc.stdout} {proc.stdin}")
        sys.exit(1) # Perhaps not a clean exit?
    else:
        ringbuffers = proc.stdout.split("\n")
        if ringbuffers[-1] == "":
            ringbuffers = ringbuffers[:-1]            
        if sink_name in ringbuffers:
            print(f"ringbuffer sink {sink_name} already exists, skipping")
            return
        else:
            print(f"creating ringbuffer sink {sink_name}")  
            cmd = f"{daqbin}/ringbuffer create {sink_name}"
            proc = subprocess.run(shlex.split(cmd), capture_output=True,
                                  text=True)
            try:
                proc.check_returncode() # This really ought never to fail...
            except subprocess.CalledProcessError as e:
                print(f"ERROR: {e} {proc.stdout} {proc.stdin}")
                sys.exit(1) # Perhaps not a clean exit?
                
parser = argparse.ArgumentParser(
    prog="run_evb.py",
    description="Run EVB pipe and eventlog for FRIB-E2SAR workflows",
    formatter_class=argparse.ArgumentDefaultsHelpFormatter
)
parser.add_argument("-c", "--config-file",
                    help="path to EVB configuration file",
                    default=f"{os.getcwd()}/setup_evb.sh")
parser.add_argument("-n", "--number-of-sources",
                    help="number of data sources",
                    default=1)
parser.add_argument("-S", "--evbring",
                    help="ringbuffer sink for built event data (localhost)",
                    default="frib_e2sar_evb")
parser.add_argument("--glomdt",
                    help="correlation window for evt building in nanoseconds",
                    default=1000)
args = parser.parse_args()
    
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

evb_cmd = f"{args.config_file} -sink {args.evbring} -window {args.glomdt}"
evb_args = shlex.split(evb_cmd)
print(f"Running evtbuild command: {evb_cmd}")
    
##
# eventlog to write data to disk. Note that this will write data
# into the current working directory:
#

evtlog_cmd = f"{daqbin}/eventlog -s tcp://localhost/{args.evbring} " \
    f"--number-of-sources={args.number_of_sources} --oneshot"
evtlog_args = shlex.split(evtlog_cmd)
print(f"Running eventlog command: {evtlog_cmd}")

##
# Processing loop:
#

while True:
    create_sink(args.evbring) # Make sure the sink exists
    evb_proc = subprocess.Popen(evb_args, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
    print("Recording run...",end="")
    sys.stdout.flush()
    evtlog_proc = subprocess.run(evtlog_args, capture_output=True, text=True)
    
    try:
        evtlog_proc.check_returncode()
    except subprocess.CalledProcessError as e:
        print(f"ERROR: {e} {evtlog_proc.stdout} {evtlog_proc.stderr}")
        print("Killing evb...")
        evb_proc.kill()
        sys.exit(1)
    else:
        print("done")
        sys.stdout.flush()
        print(f"{evtlog_cmd} completed with returncode " \
              f"{evtlog_proc.returncode}")
        time.sleep(2)
        evb_proc.kill()
        evb_proc.wait()
        print(f"{evb_cmd} completed with returncode "\
              f"{evb_proc.returncode}")
        time.sleep(2)
        print("Restarting evb pipeline...")
