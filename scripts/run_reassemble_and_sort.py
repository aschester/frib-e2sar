#!/usr/bin/env python3

'''@file run_reassemble_and_sort.py
@details 
Reassemble and sort raw DDAS data for event building (DDAS only)
'''

import os
from workflows import ReassembleAndSort

if __name__ == "__main__":
    '''@brief Main function - create the app and run it'''
    if os.getenv("DAQBIN") is None:
        print("[main] NSCLDAQ 12 environment is required")
        sys.exit(1)

    try:
        workflow = ReassembleAndSort()
        workflow.run()
    except KeyboardInterrupt:
        print("[main] Process interrupted, exiting")
    except Exception as e:
        print(f"[main] Unexpected {type(e).__name__}: {e}")
