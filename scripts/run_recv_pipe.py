#!/usr/bin/env python3

##
# @file run_recv_pipe.py
# @details Run receive-side pipe and eventlog for FRIB-E2SAR workflows
#

##
# @todo (ASC 5/21/25): More detailed argument help. For example, if --glom 0
# and --ddas-raw are both specified, the latter argument is ignored.
#
##
# @todo (ASC 6/4/25): evb sink and evtlog source may be different if filtering.
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
    print("DAQBIN is not defined! You must source an NSCLDAQ version to run "
          "the receive pipeline.")
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
        print(f"ringbuffer sink {sink_name} already exists on localhost")
        return
    else:
        print(f"creating ringbuffer sink {sink_name}")  
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
def main():
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
    parser.add_argument("-S", "--sink",
                        help="ringbuffer sink for built event data (localhost)",
                        default="frib_e2sar_evb")
    parser.add_argument("-w", "--window",
                        help="event orderer build window in seconds",
                        default=20)
    parser.add_argument("--glom",
                        help="build events yes/no = 1/0",
                        default=1)
    parser.add_argument("--glomdt",
                        help="correlation window for building events in "
                        "nanoseconds (ignored if '--glom 0')",
                        default=1000)
    parser.add_argument("--segment-size",
                        help="output file segment size (e.g., 2g = 2 GB)",
                        default="1000g")
    parser.add_argument("--ddas-raw",
                        action="store_true",
                        help="data source is NSCLDAQ 12 raw DDAS data")
    args = parser.parse_args()

    # Signal handler for this script:
    
    signal.signal(signal.SIGINT, handler)
    
    ##
    # Event builder:
    #

    ddasraw = 1 if args.ddas_raw else 0
    evbcmd = (f"{args.startup} -sink {args.sink} -build {args.glom} "
              f"-glomdt {args.glomdt} -window {args.window} "
              f"-ddasraw {ddasraw}")
    print(f"Running evtbuild command: {evbcmd}")

    ##
    # eventlog to write data to a sink:
    #
    
    logcmd = (f"{daqbin}/eventlog -s tcp://localhost/{args.sink} "
              f"-n {args.number_of_sources} -S {args.segment_size} "
              f"--oneshot")
    print(f"Running eventlog command: {logcmd}")

    ##
    # Processing loop:
    #

    #while True:
    create_sink(args.sink) # Make sure the sink exists
    evbproc = subprocess.Popen(shlex.split(evbcmd), stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
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
        print("done")
        sys.stdout.flush()            
        print(f"{logcmd} completed with returncode {logproc.returncode}")
        time.sleep(2)            
        evbproc.kill()
        evbproc.wait()
        print(f"{evbcmd} completed with returncode {evbproc.returncode}")
        time.sleep(2)
        #print("Restarting evb pipeline...")
            
if __name__ == "__main__":
    main()
    
