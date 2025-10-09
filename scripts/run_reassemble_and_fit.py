#!/usr/bin/env python3

'''@file run_reassemble_and_fit.py
@details 
Run the Reassembler and DDASToys/EventEditor trace fitting as a
processing pipeline (DDAS only)
'''

import os
from workflows import ReassembleAndFit

if __name__ == "__main__":
    '''@brief Main function - create the app and run it'''
    daqbin = os.getenv("DAQBIN")
    if daqbin is None:
        print("NSCLDAQ 12 environment is required")
        sys.exit(1)

    while True:
        try: 
            workflow = ReassembleAndFit()
            workflow.run()
        except KeyboardInterrupt:
            print("[main] Process interrupted, exiting")
            break
        except Exception as e:
            print(f"[main] Unexpected {type(e).__name__}: {e}")
            
