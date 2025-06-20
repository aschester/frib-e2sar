#!/usr/bin/env python3

##
# @file reassemble.py
# @brief Create the reassembly pipeline.
# @details
# There are some assumptions being made:
# - Reassembly on localhost
# - Ringbuffer output into tcp://localhost/reas_tXX where XX is the thread
#   number; one ringbuffer  per dequeue thread (note actually registering
#   these with the event builder is done manually in a setup script).
#

## @todo (ASC 3/6/25): Check receive rings are actually created
## @todo (ASC 5/21/25): Raw and sorted ringbuffer names set by user (must still
#  match what is in setup_evb.sh as ringFragmentSources feeding the evb)
## @todo (ASC 5/22/25): Multithreaded receive requires multiple ddasSorts OR
#  try to put all data into a single sink ringbuffer for sorting

##
# @note (ASC 5/9/25): Assumes frib-e2sar binaries installed at
# ~/frib_e2sar/bin. If this is not the case, point the reasexec variable
# defined below your installation directory:
#

from pathlib import Path
reasexec=f"""{str(Path.home())}/frib-e2sar/bin/e2sarwf"""

import argparse
import os
import shlex
import signal
import subprocess
import sys
import time

##
# @brief Print results from process stdout
# @param proc Process stdout to read and print
#
def print_proc_results(proc):
    for line in proc.stdout:
        print(line, end="")

##
# @brief Basic event reassembly
# @details
# This reassembly function should be called when dealing with:
# - Data of any format coming from an event builder
# - Raw data from non-DDAS systems or DDAS data from NSCLDAQ 11.3 or older
# - Data output from ddasSort
# The last case is unlikely - usually we expect to see raw DDAS data and
# call the `reassemble_ddas_events()` function to run a combined reassembly
# and sort process for NSCLDAQ 12 DDAS data to feed an event builder.
# @param args Parsed arguments used to configure the receive process
#
def reassemble_events(args):
    cmd = (f"{reasexec} --recv -i {args.ini} --ip {args.ip} "
           f"--port {args.port} -t {args.threads} --deq {args.deq} "
           f"-d {args.duration} --basename {args.basename}")
    print(f"Running reas command: {cmd}")
    proc = subprocess.Popen(shlex.split(cmd), stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)

    ##
    # @brief Define a process-aware signal handler. Communicate signals to
    # data receiver subprocess.
    # @param signum Signal
    # @param frame Stack frame interrupted by signal
    #
    def handler(signum, frame):
        signame = signal.Signals(signum).name
        print(f"\nSignal handler invoked: signal {signame} ({signum})")
        proc.send_signal(signal.Signals(signum))
        proc.wait()
        print(f"Process exited, returncode: {proc.returncode}")
        sys.exit(0)
        
    signal.signal(signal.SIGINT, handler)
    
    time.sleep(1) # Wait to ensure startup and ring creation
    print(f"STARTED OK {cmd}")
    
    while proc.poll() is None:
        if proc.returncode:
            print(f"ERROR: proc exited with retval {proc.returncode}!")
            print_proc_results(proc)
            sys.exit(1)
        else:
            print_proc_results(proc)
            time.sleep(1)

##
# @brief Reassemble and sort raw DDAS data from NSCLDAQ 12 for event building.
# @details
# This function should be called when dealing with NSCLDAQ 12 format raw DDAS
# data which requires an external sorting step prior to being input to an
# event builder. When an end run item is encountered, the pipeline will restart
# itself and wait for more data.
# @param args Parsed arguments to configure the receiver and ddasSort
#
def reassemble_ddas_events(args):
    rcmd = (f"{reasexec} --recv -i {args.ini} --ip {args.ip} "
            f"--port {args.port} -t {args.threads} --deq {args.deq} "
            f"-d {args.duration} --basename {args.basename}")
    print(f"Running reas command: {rcmd}")
    rproc = subprocess.Popen(shlex.split(rcmd), stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    if rproc.returncode:
        print(f"ERROR: reas exited with retval {rproc.returncode}!")
        print_proc_results(rproc)
        sys.exit(1)
    else :
        time.sleep(2) # Wait to ensure startup and ring creation
        print(f"STARTED OK {rcmd}")

    ##
    # Sort command - single src, fixed name, see @todos:
    #

    sprocs = []
    scmds = []
    for i in range(int(args.deq)):
        scmd = (f"""{os.getenv("DAQBIN")}/ddasSort -s tcp://localhost/{args.basename}_t0{i} -S {args.basename}_t0{i}_sort -W {args.window}""")
        print(f"Running sort command: {scmd}")
        sproc = subprocess.Popen(shlex.split(scmd), stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True)
        scmds.append(scmd)
        sprocs.append(sproc)
    
    if any(p.returncode for p in sprocs):
        print(f"ERROR: sort exited with retval {sproc.returncode}")
        print_proc_results(p)
        rproc.kill()
        rproc.wait()
        sys.exit(1)
    else:
        time.sleep(2)
        print(f"STARTED OK {all(cmd for cmd in scmds)}")
        
    ##
    # @brief Define a process-aware signal handler. Communicate signals to
    # data receiver and exit the sort subprocess.
    # @param signum Signal
    # @param frame Stack frame interrupted by signal
    #
    def handler(signum, frame):
        signame = signal.Signals(signum).name
        print(f"\nSignal handler invoked: signal {signame} ({signum})")
        rproc.send_signal(signal.Signals(signum))
        rproc.wait()
        print(f"Reas process exited, returncode: {rproc.returncode}")
        sproc.terminate()
        sproc.wait()
        print(f"Sort process exited, returncode: {sproc.returncode}")
        sys.exit(0)

    signal.signal(signal.SIGINT, handler)
        
    ##
    # Run loop: poll the reassembler, check returncode and exit if errors
    #
    
    while rproc.poll() == None:
        if rproc.returncode:
            print(f"ERROR: reas exited with retval {rproc.returncode}!")
            print_proc_results(rproc)
            sproc.kill() # Kill sorter if receiver fails
            sproc.wait()
            sys.exit(1)
        else:
            print_proc_results(rproc)
            time.sleep(1)

    ##
    # If a receive duration is set we still want to exit the sorter:
    #
    time.sleep(2) # Wait before shutdown
    sproc.terminate()
    sproc.wait()
    sys.exit(0)
    
##
# @brief Parse arguments and run the receive program
#
def main():     
    parser = argparse.ArgumentParser(
        prog="reassemble.py",
        description="Run EVB pipe and eventlog for FRIB-E2SAR workflows",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    parser.add_argument("-i", "--ini",
                        help="path to Reassembler configuration file",
                        default=f"{os.getcwd()}/reassembler_config.ini")
    parser.add_argument("--ip",
                        help="IP addr the Reassembler process",
                        default="35.11.82.130")
    parser.add_argument("--port",
                        help="Starting port number the Reassembler listens on",
                        default=20000)
    parser.add_argument("-t", "--threads",
                        help="number of threads/ports Reassembler is "
                        "listening on",
                        default=1)
    parser.add_argument("-b", "--basename",
                        help="reassembled sink ringbuffer basename",
                        default="reas")
    parser.add_argument("--deq",
                        help="number of dequeue threads (one sink per thread)",
                        default=1)
    parser.add_argument("-d", "--duration",
                        help="run duration in seconds to keep Reassembler open",
                        default=0)
    parser.add_argument("-w", "--window",
                        help="accumulation window for DDAS sorter in seconds",
                        default=10)
    parser.add_argument("--ddasraw",
                        action="store_true",
                        help="data source is NSCLDAQ 12 raw DDAS data")
    args = parser.parse_args()
    
    ##
    # Recv command - we start this first as it creates the ringbuffers
    # that the sort processes read from:
    #

    if args.ddasraw:
        daqbin = os.getenv("DAQBIN")
        if daqbin is None:
            print("NSCLDAQ 12 environment is required to sort raw DDAS data")
            sys.exit(1)
        else:
            print(f"Using DAQBIN: {daqbin}")
        reassemble_ddas_events(args)
    else:
        reassemble_events(args)

if __name__ == "__main__":
    main()
