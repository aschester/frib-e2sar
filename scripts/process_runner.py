'''@file process_runner.py
@brief Runs a subprocesses until a termination signal is sent
'''

import select
import shlex
import subprocess
import threading as th

class ProcessRunner(th.Thread):
    '''@class Threaded process runner for EJFAT workflows
    
    Attributes:
      name (str): Descriptive name of the process
      cmd (str): The command we're running
      proc (subprocess.Popen object): The running process
      term_event (threading.Event): Sychronization primitive for running 
        threaded processes
    
    Methods:
      __init__(name: str, cmd: str, term_event: th.Event=None): Constructor
      run(): Execute the command `cmd` using subprocess.Popen

    '''
    def __init__(self, name: str, cmd: str, term_event: th.Event=None) -> None:
        '''@brief Constructor
        @param name Descriptive name of the process
        @param cmd The command we're running
        @param term_event Sychronization primitive for threaded process 
        (default=None)
        '''
        super().__init__()
        self.name = name
        self.cmd = cmd
        self.proc = None
        self.term_event = term_event or th.Event()

    def run(self) -> None:
        '''@brief Run the process and capture its stdout and stderr'''
        print(f"[{self.name}] Starting: {self.cmd}")
        self.proc = subprocess.Popen(
            shlex.split(self.cmd),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True
        )
        
        while True:
            if self.proc.poll() is not None:
                print(
                    f"[{self.name}] Exited with return code "
                    f"{self.proc.returncode}"
                )
                break

            if self.term_event.is_set():
                self.proc.terminate()
                try:
                    self.proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    print(f"[{self.name}] Timeout expired, force killing...")
                    self.proc.kill()
                    self.proc.wait()
                    break

            readable, _, _ = select.select([self.proc.stdout], [], [], 0.1)
            if readable:
                line = self.proc.stdout.readline()
                print(f"[{self.name}] {line}", end="")
            
        print(f"[{self.name}] ProcessRunner thread terminated")
