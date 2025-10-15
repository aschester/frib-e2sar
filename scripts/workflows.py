'''
@file workflows.py
@brief Define various workflows for EJFAT processing
@details
All workflow classes follow an application object pattern an are derived
from the WorkflowBase class. The base class defines some shared parameters
and functions which need to be set or defined in the derived classes. At
some point we may want to interact with these objects by their base class
members hence the hirearchy.
'''

import argparse
import os
import shlex
import signal
import sys
import threading as th
import time

from process_runner import ProcessRunner

##
# @note (ASC 5/9/25): Assumes frib-e2sar binaries installed at
# ~/frib_e2sar/bin. If this is not the case, point the `e2sarwf` variable
# defined below your installation directory:
#
from pathlib import Path
e2sarwf=f"""{str(Path.home())}/frib-e2sar/bin/e2sarwf"""

def get_default_reas_parser(prog_name, prog_info) -> argparse.ArgumentParser:
    '''
    @brief Set default parser args for E2SAR Reassembler
    @param prog_name Program name
    @param prog_info Description of program
    @return argparse.ArgumentParser
    '''
    parser = argparse.ArgumentParser(
        prog=prog_name,
        description=prog_info,
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    parser.add_argument(
        "-i", "--ini",
        help="path to Reassembler configuration file",
        default=f"{os.getcwd()}/reassembler_config.ini"
    )
    parser.add_argument(
        "--ip",
        help="IP addr the Reassembler process",
        default="35.11.82.130"
    )
    parser.add_argument(
        "--port",
        help="Starting port number the Reassembler listens on",
        default=20000
    )
    parser.add_argument(
        "-t", "--threads",
        help="number of threads/ports Reassembler is listening on",
        default=1
    )
    parser.add_argument(
        "-S", "--sinkname",
        help="reassembled sink ringbuffer sinkname",
        default="reas"
    )
    parser.add_argument(
        "--deq",
        help="number of dequeue threads",
        default=1
    )
    parser.add_argument(
        "-d", "--duration",
        help="run duration in seconds to keep Reassembler open",
        default=0
    )
    parser.add_argument(
        "-u", "--uri",
        help="specify EJFAT_URI on the command line instead of envvar",
        default=None
    )
    parser.add_argument(
        "--useTs",
        action="store_true",
        help="use nanosecond timestamp as event number"
    )

    return parser

class WorkflowBase:
    '''
    @class WorkflowBase
    @details
    Base class for all workflows using the E2SAR Reassembler. Defines a basic
    argument parser with always-needed args and define the shutdown handler
    method for subclasses.
    @note Most subclasses will want to parse the args into self.args after
    setting whatever custom arguments they need

    Attributes:
      parser (argparse.ArgumentParser): Parser for cmdline args
      args (argparse.Namespace): Parsed cmdline args
      lock (threading.Lock): Thread mutex
      is_shutdown (bool): Flag to prevent double shutdown on, e.g., Ctrl-C
 

    Methods:
      __init__(): Constructor
      run(): Run the workflow
      shutdown(): Safe shutdown
      _get_commands(): Get the command string(s)
      _shutdown_handler(int, frame object): Call `shutdown()` and raise 
        keyboard interrupt signal (handled by main)

    '''
    def __init__(self) -> None:
        '''@brief Constructor'''
        self.parser = None       # Setup delegeted to derived classes
        self.args = None         # Set via self.parser.parse_args() generally
        self.lock = th.Lock()    # Shutdown lock to be used in derived classes
        self.is_shutdown = False # Prevent double shutdown on Ctrl-C
        
        signal.signal(signal.SIGINT, self._shutdown_handler)

    def run(self) -> None:
        '''@brief Run the workflow - must be implemented by derived classes'''
        raise NotImplementedError("Subclasses must implement `run()` method!")

    def shutdown(self):
        '''@brief Safe shutdown - must be implemented by derived classes'''
        raise NotImplementedError(
            "Subclasses must implement `shutdown()` method!"
        )
        
    ######################################################################## 
    # Private functions                                                    #
    ########################################################################

    def _get_commands(self):
        '''@brief Get commands to execute'''
        raise NotImplementedError(
            "Subclasses must implement `_get_commands()` method!"
        )
    
    def _shutdown_handler(self, signum, frame) -> None:
        '''@brief Signal handler for safe shutdown on interrupt
        @details
        Here we follow a pretty typical pattern for signal interrupts in 
        python: call a safe shutdown method (must implemented in derived 
        classes!) and raise the KeyboardInterrupt signal back to the caller.
        @param signum (int): Signal number
        @param frame (stack frame object): Where we are in the stack
        @raise KeyboardInterrupt Always after shutdown
        '''
        self.shutdown()
        raise KeyboardInterrupt

class ReassembleAndSort(WorkflowBase):
    '''
    @class ReassebleAndSort
    @details
    Reassemble raw DDAS data and time-order it using ddasSort for feeding 
    into an EVB pipe
    Attributes:
      parser (argparse.ArgumentParser): Parser for cmdline args
      args (argparse.Namespace): Parsed cmdline args
      lock (threading.Lock): Thread mutex
      is_shutdown (bool): Flag to prevent double shutdown on, e.g., Ctrl-C
      reas_proc (Popen object): E2SAR Reassembler process
      reas_event (threading.Event): Sychroniziation primitive for Reassembler
      sort_proc (Popen object): NSCLDAQ ddasSort process
      sort_event (threading.Event): Sychroniziation primitive for ddasSort

    Methods:
      __init__(): Constructor
      run(): Run the workflow
      shutdown(): Safe shutdown
      _get_commands(): Get the command string(s)
      _shutdown_handler(int, frame object): Call `shutdown()` and raise 
        keyboard interrupt signal (handled by main)

    '''
    def __init__(self) -> None:
        '''@brief Constructor'''
        super().__init__()
        self.parser = get_default_reas_parser(
            "ReassembleAndSort",
            "Reassemble and sort raw DDAS data"
        )
        self.parser.add_argument(
            "-w", "--window",
            help="accumulation window for DDAS sorter " 
            "in seconds",
            default=10
        )
        self.args = self.parser.parse_args()
        self.reas_proc = None
        self.reas_event = th.Event()
        self.sort_proc = None
        self.sort_event = th.Event()

    def run(self) -> None:
        '''@brief Run the workflow'''
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
                self.reas_event.set()
                self.reas_proc.join()
            sys.exit(1)

        while True:
            if not self.reas_proc.is_alive():
                break
            if not self.sort_proc.is_alive():
                break        
            time.sleep(0.1)

        self.shutdown()

    def shutdown(self) -> None:
        '''@brief Safe shutdown'''
        with self.lock:
            if self.is_shutdown:
                return
            self.is_shutdown = True
            if self.reas_proc and self.reas_proc.is_alive():
                self.reas_event.set()
                self.reas_proc.join()            
            if self.sort_proc and self.sort_proc.is_alive():
                self.sort_event.set()                
                self.sort_proc.join()
            print("[ctrl] Subprocesses terminated, exiting")

    ######################################################################## 
    # Private functions                                                    #
    ########################################################################
    
    def _get_commands(self) -> tuple[str, str]:
        '''@brief Get the commands to run
        @return tuple[str, str] The reas_cmd and sort_cmd strings
        '''
        reas_cmd = (
            f"{e2sarwf} --recv "        
            f"--ini {self.args.ini} "      
            f"--ip {self.args.ip} "     
            f"--port {self.args.port} " 
            f"--threads {self.args.threads} "  
            f"--deq {self.args.deq} "   
            f"--duration {self.args.duration} " 
            f"--sinkname {self.args.sinkname}"
        )

        if self.args.useTs:
            reas_cmd += f" --useTs"
        if self.args.uri is not None:
            reas_cmd += f" --uri {self.args.uri}"
            
        sort_cmd = (
            f"{os.getenv('DAQBIN')}/ddasSort "
            f"--source tcp://localhost/{self.args.sinkname} "
            f"--sink {self.args.sinkname}_sort " 
            f"--window {self.args.window}"
        )

        return reas_cmd, sort_cmd
            
class ReassembleAndLog(WorkflowBase):
    '''
    @class ReassebleAndLog
    @details
    Reassemble NSCLDAQ data and write it to disk.

    Attributes:
      parser (argparse.ArgumentParser): Parser for cmdline args
      args (argparse.Namespace): Parsed cmdline args
      lock (threading.Lock): Thread mutex
      is_shutdown (bool): Flag to prevent double shutdown on, e.g., Ctrl-C
      reas_proc (Popen object): E2SAR Reassembler process
      reas_event (threading.Event): Sychroniziation primitive for Reassembler
      log_proc (Popen object): NSCLDAQ eventlog process
      log_event (threading.Event): Sychroniziation primitive for eventlog

    Methods:
      __init__(): Constructor
      run(): Run the workflow
      shutdown(): Safe shutdown
      _get_commands(): Get the command string(s)
      _shutdown_handler(int, frame object): Call `shutdown()` and raise 
        keyboard interrupt signal (handled by main)

    '''
    def __init__(self) -> None:
        '''@brief Constructor'''
        super().__init__()
        self.parser = get_default_reas_parser(
            "ReassembleAndLog",
            "Reassemble and write data to disk"
        )
        self.parser.add_argument(
            "-n", "--number-of-sources",
            help="number of data sources for event builder",
            default=1
        )
        self.parser.add_argument(
            "--segment-size",
            help="output file segment size (e.g., 2g = 2 GB)",
            default="1000g"
        )
        self.args = self.parser.parse_args()
        self.reas_proc = None
        self.reas_event = th.Event()
        self.log_proc = None
        self.log_event = th.Event()

    def run(self) -> None:
        '''@brief Run the workflow'''
        reas_cmd, log_cmd = self._get_commands()

        self.reas_proc = ProcessRunner("reas", reas_cmd, self.reas_event)
        self.log_proc = ProcessRunner("log", log_cmd, self.log_event)

        self.reas_proc.start()
        self.reas_proc.join(timeout=1)
        if not self.reas_proc.is_alive():
            print("[ctrl] ERROR: e2sarwf failed to start, exiting...")
            sys.exit(1)
            
        self.log_proc.start()
        self.log_proc.join(timeout=1)
        if not self.log_proc.is_alive():
            print("[ctrl] ERROR: eventlog failed to start, exiting...")
            if self.reas_proc.is_alive():
                self.reas_event.set()
                self.reas_proc.join()
            sys.exit(1)

        while True:
            if not self.reas_proc.is_alive():
                break
            if not self.log_proc.is_alive():
                break        
            time.sleep(0.1)    

        self.shutdown()

    def shutdown(self) -> None:
        '''@brief Safe shutdown'''
        with self.lock:
            if self.is_shutdown:
                return
            self.is_shutdown = True
            if self.reas_proc and self.reas_proc.is_alive():
                self.reas_event.set()
                self.reas_proc.join()            
            if self.log_proc and self.log_proc.is_alive():
                self.log_event.set()                
                self.log_proc.join()
            print("[ctrl] Subprocesses terminated, exiting")
            
    ######################################################################## 
    # Private functions                                                    #
    ########################################################################
            
    def _get_commands(self) -> tuple[str, str]:
        '''@brief Get the Reassembler and eventlog command strings
        @return tuple[str,str] Reassembler and eventlog command strings
        '''
        reas_cmd = (
            f"{e2sarwf} --recv "
            f"--ini {self.args.ini} "
            f"--ip {self.args.ip} "
            f"--port {self.args.port} "
            f"--threads {self.args.threads} "
            f"--deq {self.args.deq} "
            f"--duration {self.args.duration} "
            f"--sinkname {self.args.sinkname} "
        )

        if self.args.useTs:
            reas_cmd += f"--useTs"
        if self.args.uri is not None:
            reas_cmd += f" --uri {self.args.uri}"
        
        log_cmd = (
            f"{os.getenv('DAQBIN')}/eventlog "   
            f"--source tcp://localhost/{self.args.sinkname} " 
            f"--number-of-sources {self.args.number_of_sources} "    
            f"--segmentsize {self.args.segment_size} "
            f"--oneshot "
        )

        return reas_cmd, log_cmd
         
class ReassembleAndFit(WorkflowBase):
    '''
    @class ReassebleAndFit
    @details
    Reassemble built data fit it using the FRIBDAQ EventEditor

    Attributes:
      parser (argparse.ArgumentParser): Parser for cmdline args
      args (argparse.Namespace): Parsed cmdline args
      lock (threading.Lock): Thread mutex
      is_shutdown (bool): Flag to prevent double shutdown on, e.g., Ctrl-C
      reas_proc (Popen object): E2SAR Reassembler process
      reas_event (threading.Event): Sychroniziation primitive for Reassembler
      fit_proc (Popen object): Trace fitting process (really a bash script)
      fit_event (threading.Event): Sychroniziation primitive for fit script

    Methods:
      __init__(int, int): Constructor
      run(): Run the workflow
      shutdown(): Safe shutdown
      _get_commands(): Get the command string(s)
      _shutdown_handler(int, frame object): Call `shutdown()` and raise 
        keyboard interrupt signal (handled by main)

    '''
    def __init__(self) -> None:
        '''@brief Constructor
        @param workers Number of workers for parallel trace fitting
        @param chunk_size Size of chunks (in events) handed to each worker
        '''
        super().__init__()
        self.parser = get_default_reas_parser(
            "ReassembleAndFit",
            "Reassemble and fit DDAS data with waveforms"
        )
        self.parser.add_argument(
            "-w", "--workers",
            help="number of EventEditor workers used to fit trace data",
            default=32
        )
        self.parser.add_argument(
            "-c", "--chunk-size",
            help="work unit size (# of events) handed to EventEditor workers",
            default=1000
        )
        self.parser.add_argument(
            "-p", "--parallel-strategy",
            help="parallel strategy for EventEditor ('threaded' or 'mpi')",
            default="mpi"
        )
        self.args = self.parser.parse_args()
        self.reas_proc = None
        self.reas_event = th.Event()
        self.fit_proc = None
        self.fit_event = th.Event()

    def run(self) -> None:
        '''@brief Run the workflow'''
        reas_cmd, fit_cmd = self._get_commands()

        self.reas_proc = ProcessRunner("reas", reas_cmd, self.reas_event)
        self.fit_proc = ProcessRunner("fit", fit_cmd, self.fit_event)

        self.reas_proc.start()
        self.reas_proc.join(timeout=1)
        if not self.reas_proc.is_alive():
            print("[ctrl] ERROR: e2sarwf failed to start, exiting...")
            sys.exit(1)

        self.fit_proc.start()
        self.fit_proc.join(timeout=1)
        if not self.fit_proc.is_alive():
            print("[ctrl] ERROR: EventEditor failed to start, exiting...")
            if self.reas_proc.is_alive():
                self.reas_event.set()
                self.reas_proc.join()
            sys.exit(1)

        while True:
            if not self.reas_proc.is_alive():
                break
            if not self.fit_proc.is_alive():
                break        
            time.sleep(0.1)

        self.shutdown()

    def shutdown(self) -> None:
        '''@brief Safe shutdown'''
        with self.lock:
            if self.is_shutdown:
                return
            self.is_shutdown = True

            if self.reas_proc and self.reas_proc.is_alive():
                self.reas_event.set()
                self.reas_proc.join(timeout=5)
                if self.reas_proc.is_alive():
                    print(
                        "[ctrl] WARNING: Reassemble process did not "
                        "terminate cleanly"
                    )

            if self.fit_proc and self.fit_proc.is_alive():
                self.fit_event.set()
                self.fit_proc.join(timeout=5)
                if self.fit_proc.is_alive():
                    print(
                        "[ctrl] WARNING: EventEditor process did not "
                        "terminate cleanly"
                    )

            print("[ctrl] Subprocesses terminated, exiting")        

    ######################################################################## 
    # Private functions                                                    #
    ########################################################################
            
    def _get_commands(self) -> tuple[str, str]:
        '''@brief Get the Reassembler and fit command strings
        @details
        Because the EventEditor trace-fitting program requries a lot of 
        configuration, we do not call it directly here but rather run a 
        script which ensures that the runtime environment is configured 
        correctly.
        @return tuple[str, str] Reassembler and fit command strings
        '''
        reas_cmd = (
            f"{e2sarwf} --recv "
            f"--ini {self.args.ini} "
            f"--ip {self.args.ip} "
            f"--port {self.args.port} "
            f"--threads {self.args.threads} "
            f"--deq {self.args.deq} "
            f"--duration {self.args.duration} "
            f"--sinkname {self.args.sinkname} "
        )

        if self.args.useTs:
            reas_cmd += f"--useTs"
        if self.args.uri is not None:
            reas_cmd += f" --uri {self.args.uri}"
            
        fit_cmd = (
            f"./run_ee.sh "
            f"{self.args.workers} "
            f"{self.args.chunk_size} "
            f"{self.args.parallel_strategy}"
        )

        return reas_cmd, fit_cmd

class EvbAndLog(WorkflowBase):
    '''
    @class EvbAndLog
    @details
    Run the event-building and event log pipeline
    @note This class does not reassemble data and is used to build events on
    the receive side of the E2SAR pipe

    Attributes:
      parser (argparse.ArgumentParser): Parser for cmdline args
      args (argparse.Namespace): Parsed cmdline args
      lock (threading.Lock): Thread mutex
      is_shutdown (bool): Flag to prevent double shutdown on, e.g., Ctrl-C
      evb_proc (Popen object): Event-builder process (really a bash script)
      evb_event (threading.Event): Sychroniziation primitive for evb script
      log_proc (Popen object): NSCLDAQ eventlog process
      log_event (threading.Event): Sychroniziation primitive for eventlog

    Methods:
      __init__(): Constructor
      run(): Run the workflow
      shutdown(): Safe shutdown
      _get_commands(): Get the command string(s)
      _shutdown_handler(int, frame object): Call `shutdown()` and raise 
        keyboard interrupt signal (handled by main)
      _create_sink(): Create the ringbuffer data sink for the event builder

    '''
    def __init__(self) -> None:
        '''@brief Constructor
        @note Since this workflow does not run the E2SAR Reassembler, we have 
        to define our own parser here as it contains many unique arguments.
        '''
        super().__init__()
        parser = argparse.ArgumentParser(
            prog="EvbAndLog",
            description="Build reassembled data into events and write to disk",
            formatter_class=argparse.ArgumentDefaultsHelpFormatter
        )
        parser.add_argument(
            "--startup",
            help="script to configure and launch event builder",
            required=True
        )
        parser.add_argument(
            "-n", "--number-of-sources",
            help="number of data sources for event builder",
            default=1
        )
        parser.add_argument(
            "-s", "--source",
            help="reassembled source ringbuffer basename",
            default="reas"
        )
        parser.add_argument(
            "-S", "--sink",
            help="ringbuffer sink for built event data (localhost)",
            default="frib_e2sar_evb"
        )
        parser.add_argument(
            "-w", "--window",
            help="event orderer build window in seconds",
            default=20
        )
        parser.add_argument(
            "--glomdt",
            help="correlation window for building events in nanoseconds",
            default=1000
        )
        parser.add_argument(
            "--segment-size",
            help="output file segment size (e.g., 2g = 2 GB)",
            default="1000g"
        )
        self.parser = parser
        self.args = self.parser.parse_args()
        self.evb_proc = None
        self.evb_event = th.Event()
        self.log_proc = None
        self.log_event = th.Event()

    def run(self) -> None:
        '''@brief Run the process'''
        evb_cmd, log_cmd = self._get_commands()

        self.evb_proc = ProcessRunner("evb", evb_cmd, self.evb_event)
        self.log_proc = ProcessRunner("log", log_cmd, self.log_event)

        self.evb_proc.start()
        self.evb_proc.join(timeout=1)
        if not self.evb_proc.is_alive():
            print("[ctrl] ERROR: event builder failed to start, exiting...")
            sys.exit(1)
            
        self.log_proc.start()
        self.log_proc.join(timeout=1)
        if not self.log_proc.is_alive():
            print("[ctrl] ERROR: eventlog failed to start, exiting...")
            if self.evb_proc.is_alive():
                self.evb_event.set()
                self.evb_proc.join()
            sys.exit(1)

        while True:
            if not self.evb_proc.is_alive():
                break
            if not self.log_proc.is_alive():
                break        
            time.sleep(0.1)
            
        self.shutdown()            

    def shutdown(self) -> None:
        '''@brief Safe shutdown'''
        with self.lock:
            if self.is_shutdown:
                return
            self.is_shutdown = True
            if self.evb_proc and self.evb_proc.is_alive():
                self.evb_event.set()
                self.evb_proc.join()            
            if self.log_proc and self.log_proc.is_alive():
                self.log_event.set()                
                self.log_proc.join()
            print("[ctrl] Subprocesses terminated, exiting")
            
    ######################################################################## 
    # Private functions                                                    #
    ########################################################################
            
    def _get_commands(self) -> tuple[str, str]:
        '''@brief Get the event builder and eventlog command strings
        @return tuple[str, str] The event builder and eventlog command strings
        '''
        evb_cmd = (
            f"{self.args.startup} "
            f"-source {self.args.source} "
            f"-sink {self.args.sink} "
            f"-glomdt {self.args.glomdt} "
            f"-window {self.args.window} "
        )

        log_cmd = (
            f"{os.getenv('DAQBIN')}/eventlog "
            f"--source tcp://localhost/{self.args.sink} "
            f"--number-of-sources {self.args.number_of_sources} "
            f"--segmentsize {self.args.segment_size} "
            f"--oneshot "
        )
        
        return evb_cmd, log_cmd

    def _create_sink(sink_name: str) -> None:
        '''@brief Create the data sink for the event builder pipe
        @param sink_name Name of the data sink
        '''
        cmd = f"{os.getenv('DAQBIN')}/ringbuffer list"
        proc = subprocess.run(
            shlex.split(cmd),
            capture_output=True,
            text=True
        )

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
            cmd = f"{os.getenv('DAQBIN')}/ringbuffer create {sink_name}"
            proc = subprocess.run(
                shlex.split(cmd),
                capture_output=True,
                text=True
            )
        try:
            proc.check_returncode() 
        except subprocess.CalledProcessError as e:
            print(f"ERROR: {e} {proc.stdout} {proc.stdin}")
            sys.exit(1)
