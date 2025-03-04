#!/bin/bash

##
# @file record.sh
# @brief Log data from FRIB-E2SAR processing pipeline
#

$DAQBIN/eventlog -s tcp://localhost/frib_e2sar_evb --oneshot --number-of-sources=1
