#!/usr/bin/env python3

##
# @file run_recv_pipe.py
# @details Run receive-side pipe and eventlog for FRIB-E2SAR workflows
#

##
# @todo (ASC 6/18/25): Specify cmdline arg data types.
#

##
# @todo (ASC 7/8/25): Could be refactored into an application class with
# interruptable, threaded subprocesses and a simple main a la
# run_reassembler.py.
#

##
# @todo (ASC 8/8/25): Exit everything if the EVB GUI closes.
#

import argparse
import os
import shlex
import signal
import subprocess
import sys
import time

# NSCLDAQ needed to create evb sources:
daqbin = os.getenv("DAQBIN")
if daqbin is None:
    print("DAQBIN is not defined! You must source an NSCLDAQ version "
          "to run the receive pipeline.")
    sys.exit(1)
else:
    print(f"Using DAQBIN: {daqbin}")

##
# @brief Signal handler for EVB and eventlog
# @param signum Signal
# @param frame Stack frame interrupted by signal
#
def handler(signum, frame):
    signame = signal.Signals(signum).name
    print(f"\nSignal handler called with signal {signame} ({signum})")
    sys.exit(0)
    
##
# @brief Creates a sink ring of the given name
# @param sink_name Name of the sink ringbuffer (not URI!)
#
def create_sink(sink_name):
    cmd = f"{daqbin}/ringbuffer list"
    proc = subprocess.run(shlex.split(cmd), capture_output=True, text=True)
    try:
        proc.check_returncode()
    except subprocess.CalledProcessError as e:
        print(f"ERROR: {e} {proc.stdout} {proc.stdin}")
        sys.exit(1)

    ringbuffers = proc.stdout.split("\n")
    if ringbuffers[-1] == "":
        ringbuffers = ringbuffers[:-1]
        
    if sink_name in ringbuffers:
        print(f"Ringbuffer sink {sink_name} already exists on localhost")
        return
    else:
        print(f"Creating ringbuffer sink {sink_name}")  
        cmd = f"{daqbin}/ringbuffer create {sink_name}"
        proc = subprocess.run(shlex.split(cmd), capture_output=True,
                              text=True)
        try:
            proc.check_returncode() 
        except subprocess.CalledProcessError as e:
            print(f"ERROR: {e} {proc.stdout} {proc.stdin}")
            sys.exit(1)
            
##
# @brief Parse arguments and run the EVB pipe to receive data
# @details
# Most parameters have some sensible defaults. Users are required to provide
# a configuration script to setup the event builder pipeline. In theory this
# should be doable from within python but I've had some issues working with
# the tkinter package, so instead I've opted for the slightly convoluted method
# of using subprocess.run() to execute a bash script which is a wrapper for
# running Tcl/Tk using wish.
#
# The event-builder startup script must create the ring sources and initialize
# the event-builder pipeline. Details of this implementation are left to the
# user. An example setup script, setup_evb.sh is provided in the /scripts
# directory under your top-level frib-e2sar installation directory, refer to
# that file for additional documentation.
#
            
if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        prog="run_recv_pipe.py",
        description="Run receive-side pipe and eventlog for FRIB-E2SAR "
        "workflows",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    parser.add_argument("--startup",
                        help="script to configure and launch event builder",
                        required=True)
    parser.add_argument("-n", "--number-of-sources",
                        help="number of data sources for event builder",
                        default=1)
    parser.add_argument("-s", "--source",
                        help="reassembled source ringbuffer basename",
                        default="reas")
    parser.add_argument("-S", "--sink",
                        help="ringbuffer sink for built event data (localhost)",
                        default="frib_e2sar_evb")
    parser.add_argument("-w", "--window",
                        help="event orderer build window in seconds",
                        default=20)
    parser.add_argument("--glomdt",
                        help="correlation window for building events in "
                        "nanoseconds",
                        default=1000)
    parser.add_argument("--logdata", type=int,
                        help="log data from sink ringbuffer yes/no = 1/0",
                        default=1)
    parser.add_argument("--segment-size",
                        help="output file segment size (e.g., 2g = 2 GB)",
                        default="1000g")
    args = parser.parse_args()
    
    # Signal handler for this script:
    
    signal.signal(signal.SIGINT, handler)
    
    ##
    # Processing loop:
    #

    evbcmd = (f"{args.startup} -source {args.source} -sink {args.sink} "
              f"-glomdt {args.glomdt} -window {args.window}")
    print(f"[evbcmd] {evbcmd}")
    
    logcmd = (f"{daqbin}/eventlog -s tcp://localhost/{args.sink} "
              f"-n {args.number_of_sources} -S {args.segment_size} "
              f"--oneshot")
    
    # Run the processing loop forever:
    
    #while True:
            
    create_sink(args.sink) # Make sure the sink exists

    evbproc = subprocess.Popen(shlex.split(evbcmd), stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)

    # Poll the evb process for a second to ensure it started properly:
    pollct = 0
    while pollct < 10:
        evbproc.poll()
        if evbproc.returncode:
            print(f"ERROR: {evbcmd} failed to start with returncode "
                  f"{evbproc.returncode}")
            print(f"evbproc process output:")
            for line in evbproc.stdout:
                print(line, end="")
            sys.exit(1)
        pollct += 1
        time.sleep(0.1)


    # Start eventlog if enabled:
    if args.logdata:
        print(f"[logcmd] {logcmd}")
        print("Recording run...",end="")
        sys.stdout.flush()
        logproc = subprocess.run(shlex.split(logcmd), capture_output=True,
                                 text=True)

        try:
            logproc.check_returncode()
        except subprocess.CalledProcessError as e:
            print(f"ERROR: {e} {logproc.stdout} {logproc.stderr}")
            print("Killing evb...")
            evbproc.kill() # Kill off pipe if the event logger fails
            evbproc.wait()
            sys.exit(1)
        else:
            print("Done")
            sys.stdout.flush()            
            print(f"{logcmd} Completed with returncode "
                  f"{logproc.returncode}")
            time.sleep(2)            
            evbproc.kill()
            evbproc.wait()
            print(f"{evbcmd} Completed with returncode "
                  f"{evbproc.returncode}")
    else:
        print("Not recording data...")
        evbproc.wait()

    time.sleep(1) # Wait a second before restart
