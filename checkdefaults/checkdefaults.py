#!/usr/bin/env python

# Add the path of the pybind module
import sys
sys.path.append("/usr/local/lib/python3/dist-packages")

import e2sar_py

# Default python, better be 3.10+
print(f"python version: {sys.version}")

# Get the version
print(f"E2SAR version: {e2sar_py.get_version()}")

# Print the constant atrributes
print(f"Default data plane port: {e2sar_py._dp_port}")
print(f"Default Reassembler Header version: {e2sar_py._rehdr_version}")
print(f"Default Reassembler Header nibble: {e2sar_py._rehdr_version_nibble}")
print(f"Default Load Balancer Header version: {e2sar_py._lbhdr_version}")
print(f"Default Sync Header version: {e2sar_py._synchdr_version}")

# The Hdr lengths
print(f"IP header length: {e2sar_py._iphdr_len}")
print(f"UDP header length: {e2sar_py._udphdr_len}")
print(f"Total header length: {e2sar_py._total_hdr_len}")
print(f"LB + RE header length: {e2sar_py._total_hdr_len - e2sar_py._iphdr_len - e2sar_py._udphdr_len}")

# Setting the Reaasembler header fields
rehdr = e2sar_py.REHdr()

# Default fields are zeros
print(f"Before setting fields: {rehdr.get_fields()}")

# Set fields and print
rehdr.set(data_id=0b0001, buff_off=0b0010, buff_len=0b0100, event_num=0b1000)
print(f"After setting fields: {rehdr.get_fields()}")
print(f"  data_id={rehdr.get_dataId()}")
print(f"  buff_off={rehdr.get_bufferOffset()}")
print(f"  buff_len={rehdr.get_bufferLength()}")
print(f"  event_num={rehdr.get_eventNum()}")

# Load balancer header
lbhdr = e2sar_py.LBHdr()

print("Before", lbhdr.get_fields())
# The field values in sequence
print(lbhdr.get_version())
print(lbhdr.get_nextProto())
print(lbhdr.get_entropy())
print(lbhdr.get_eventNum())

lbhdr.set(entropy=200, event_num=50)
print("After", lbhdr.get_fields())
