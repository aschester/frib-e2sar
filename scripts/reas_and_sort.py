#!/usr/bin/env python3

##
# @file reas_and_sort.py
# @details Create the reassembly and sorting pipeline
#

## @todo (ASC 3/7/25): Configure parameters via command line.

import os
import shlex
import signal
import subprocess
import sys
import time

def print_proc_results(proc):
    stdout, _ = proc.communicate()
    print(f"process return code: {proc.returncode}")
    print(f"{stdout.decode('utf-8')}")

# SIGINT is propagated to subprocesses, get results and exit:
def handler(signum, frame):
    signame = signal.Signals(signum).name
    print(f"\nSignal handler called with signal {signame} ({signum})")
    print_proc_results(reas_proc)
    sys.exit(0)

signal.signal(signal.SIGINT, handler)

# NSCLDAQ needed for ddasSort:    
daqbin = os.getenv("DAQBIN")
if daqbin is None:
    print("DAQBIN must be defined!")
    sys.exit(1)
else:
    print(f"Using DAQBIN: {daqbin}")
    
##
# Recv command - we start this first as it creates the ringbuffer
# that ddasSort reads from:
#

script_dir = os.path.dirname(os.path.realpath(__file__))
top_dir = os.path.dirname(script_dir) # Top level install directory
reas_cmd = f"{top_dir}/bin/evtbuild/recv -S tcp://localhost/reas0_raw -t 4"
reas_args = shlex.split(reas_cmd)
print(f"Running reas command: {reas_cmd}")

with subprocess.Popen(reas_args, stdout=subprocess.PIPE,
                      stderr=subprocess.STDOUT) as reas_proc:
    ## @todo (ASC 3/6/25): Check ring is actually created
    time.sleep(1) # Wait to ensure startup and ring creation
    print(f"STARTED OK {reas_cmd}")
    
    ##
    # Sort command:
    #
    
    sort_cmd = f"{daqbin}/ddasSort -s tcp://localhost/reas0_raw " \
        f"-S reas0_sort -W 10.0"
    sort_args = shlex.split(sort_cmd)    
    print(f"Running sort command: {sort_cmd}")    
    sort_proc = subprocess.Popen(sort_args, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT)
    if sort_proc.returncode:
        print(f"ERROR: sort exited with retval {sort_proc.returncode}")
        print_proc_results(sort_proc)
        reas_proc.kill()
        sys.exit(1)
    else:
        print(f"STARTED OK {sort_cmd}")

    ##
    # Run loop: poll the reassembler, check returncode and exit if errors.
    #
        
    while reas_proc.poll() == None:
        if reas_proc.returncode:
            print(f"ERROR: reas exited with retval {reas_proc.returncode}!")
            print_proc_results(reas_proc)
            sort_proc.kill() # Kill sorter if receiver fails
            sort_proc.wait()
            sys.exit(1)
        else:
            time.sleep(1)
    
# If a receive duration is set we still want to exit the sorter:

time.sleep(2) # Wait before shutdown
sort_proc.terminate()
sort_proc.wait()
sys.exit(0)
