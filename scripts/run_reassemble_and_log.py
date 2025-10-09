#!/usr/bin/env python3

'''@file run_reassemble_and_log.py
 @details 
Reassemble and log pre-built NSCLDAQ data (type independent)
'''

import os
from workflows import ReassembleAndLog

if __name__ == "__main__":
    '''@brief Main function - create the app and run it'''
    if os.getenv("DAQBIN") is None:
        print("[main] NSCLDAQ 12 environment is required")
        sys.exit(1)

    try:
        workflow = ReassembleAndLog()
        workflow.run()
    except KeyboardInterrupt:
        print("[main] Process interrupted, exiting")
    except Exception as e:
        print(f"[main] Unexpected {type(e).__name__}: {e}")

