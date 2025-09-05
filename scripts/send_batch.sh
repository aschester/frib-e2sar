#!/bin/bash

##
# @file send_batch.sh
# @brief Sequentially send a bunch of data through the FRIB-E2SAR pipeline
#

usage() {
    echo "$0 [-r <send rate (Gbps)>]"
    exit 0
    }


rate=1

OPTIND=1
while getopts ":h?r:" opt; do
    case "$opt" in
        h|\?)
            usage
            exit 0
            ;;
        r)
            rate=$OPTARG
            ;;
        :)
            echo "Option -$OPTARG requires an argument." >&2
            usage
            ;;
	    esac
done

shift $((OPTIND - 1))

basepath=/scratch/e2sar/data
files=(
    15Jul2025-111813-run-0077-00.evt
    15Jul2025-123846-run-0078-00.evt
    15Jul2025-130914-run-0079-00.evt
)

for f in ${files[@]}; do
    ~/frib-e2sar/bin/e2sarwf --send -o sendmmsg --ddasraw -s file://${basepath}/${f} -r ${rate}
    sleep 10
done
