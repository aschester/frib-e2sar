#!/bin/bash

##
# @file sorter.sh
# @brief Run ddasSort on the reassembled data.
#

# Trivial one-liner assuming everything is set:

$DAQBIN/ddasSort -s tcp://localhost/reas0_raw -S reas0_sort -W 10.0
