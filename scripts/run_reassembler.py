#!/usr/bin/env python3

##
# @file run_reassembler.py
# @details Application object pattern work-in-progress to run receive-side
# pipe and eventlog for FRIB-E2SAR workflows
#

##
# @note (ASC 5/9/25): Assumes frib-e2sar binaries installed at
# ~/frib_e2sar/bin. If this is not the case, point the reasexec variable
# defined below your installation directory:
#
from pathlib import Path
e2sarwf=f"""{str(Path.home())}/frib-e2sar/bin/e2sarwf"""

import argparse
import os
import select
import shlex
import signal
import subprocess
import sys
import threading as th
import time

class ProcessRunner(th.Thread):
    def __init__(self, name: str, cmd: str, term_event: th.Event=None) -> None:
        super().__init__()
        self.name = name
        self.cmd = cmd
        self.proc = None
        self.term_event = term_event or th.Event()

    def run(self):
        print(f"[{self.name}] Starting: {self.cmd}")
        self.proc = subprocess.Popen(shlex.split(self.cmd),
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT,
                                     text=True)
        
        while True:
            if self.proc.poll() is not None:
                print(f"[{self.name}] Exited with return code "
                      f"{self.proc.returncode}")
                break

            if self.term_event.is_set():
                self.proc.terminate()
                try:
                    self.proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    print(f"[{self.name}] Timeout expired, force killing...")
                    self.proc.kill()
                break

            readable, _, _ = select.select([self.proc.stdout], [], [], 0.1)
            if readable:
                for line in self.proc.stdout:
                    print(f"[{self.name}] {line}", end="")

class NsclReassembler:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.reas_proc = None
        self.sort_proc = None
        self.reas_event = th.Event()
        self.sort_event = th.Event()
        self.lock = th.Lock()

    def run(self):
        signal.signal(signal.SIGINT, self._shutdown_handler)
        
        reas_cmd, sort_cmd = self._get_commands()

        self.reas_proc = ProcessRunner("reas", reas_cmd, self.reas_event)
        self.sort_proc = ProcessRunner("sort", sort_cmd, self.sort_event)

        self.reas_proc.start()
        self.reas_proc.join(timeout=1)
        if not self.reas_proc.is_alive():
            print("[ctrl] ERROR: e2sarwf failed to start, exiting...")
            sys.exit(1)
        
        self.sort_proc.start()
        self.sort_proc.join(timeout=1)
        if not self.sort_proc.is_alive():
            print("[ctrl] ERROR: ddasSort failed to start, exiting...")
            if self.reas_proc.is_alive():
                print("[ctrl] Killing Reassembler...")
                self.reas_event.set()
                self.reas_proc.join()
            sys.exit(1)

        self.reas_proc.join()
        self._shutdown()

    def _get_commands(self) -> tuple[str, str]:
        reas_cmd = (f"{e2sarwf} --recv -i {self.args.ini} --ip {self.args.ip} "
                    f"--port {self.args.port} -t {self.args.threads} "
                    f"--deq {self.args.deq} -d {self.args.duration} "
                    f"--sinkname {self.args.sinkname}")

        if self.args.usect:
            reas_cmd += f" --usect"
        
        sort_cmd = (f"{os.getenv('DAQBIN')}/ddasSort "
                    f"-s tcp://localhost/{self.args.sinkname} "
                    f"-S {self.args.sinkname}_sort -W {self.args.window}")

        return reas_cmd, sort_cmd

    def _shutdown_handler(self, signum, frame):
        self._shutdown()

    def _shutdown(self):
        with self.lock:
            if self.reas_proc and self.reas_proc.is_alive():
                self.reas_event.set()
                self.reas_proc.join()            
            if self.sort_proc and self.sort_proc.is_alive():
                self.sort_event.set()                
                self.sort_proc.join()
            print("[ctrl] Subprocesses terminated, exiting")
            sys.exit(0)

if __name__ == "__main__":
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
    parser.add_argument("-S", "--sinkname",
                        help="reassembled sink ringbuffer sinkname",
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
    parser.add_argument("--usect",
                        action="store_true",
                        help="use event count as event timestamp")
    args = parser.parse_args()

    daqbin = os.getenv("DAQBIN")
    if daqbin is None:
        print("NSCLDAQ 12 environment is required to process raw DDAS data")
        sys.exit(1)
    
    reassembler = NsclReassembler(args)
    reassembler.run()
