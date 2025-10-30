#!/usr/bin/env python3

'''@file run_reassemble_and_fit.py
@details 
Run the Reassembler and DDASToys/EventEditor trace fitting (DDAS only)
'''

import os
import sys
from workflows import ReassembleAndFit

if __name__ == "__main__":
    '''@brief Main function - create the app and run it'''
    if os.getenv("DAQBIN") is None:
        print("[main] NSCLDAQ 12 environment is required")
        sys.exit(1)

    try: 
        workflow = ReassembleAndFit()
        workflow.run()
    except KeyboardInterrupt:
        print("[main] Process interrupted, exiting")
        sys.exit(0)
    except Exception as e:
        print(f"[main] Unexpected {type(e).__name__}: {e}")
        sys.exit(1)
    
