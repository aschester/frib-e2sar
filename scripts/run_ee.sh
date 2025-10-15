#!/bin/bash

##
# @file runee.sh
# @brief Run EventEditor for testing/debugging
#

##
# How to use this script:
#
if [ $# -ne 3 ]
then
    echo "usage: ./runee.sh <procs> <chunk> <mode>"
    echo "  <procs>: number of MPI processes (min = 4) or threads"
    echo "  <chunk>: number of events passed as a work unit"
    echo "  <mode>:  parallel mode (mpi or threaded)"
    exit 1
fi

##
# Configure environment:
#
VERSION=12.1-dev.e2sar
. /usr/opt/daq/$VERSION/daqsetup.bash -f
if [ -z "${DAQROOT}" ]; then
    echo "No NSCLDAQ version $VERSION found in /usr/opt/daq"
    exit 1
else
    echo "NSCLDAQ version is is $DAQROOT"
fi

MPIROOT=/usr/opt/mpi/openmpi-4.1.4
if [ -z "${MPIROOT}" ]
then
    echo "ERROR: Cannot find expected MPI installation directory in \
    image $APPTAINER_CONTAINER"
    exit 1
else
    echo "Using MPI installed at $MPIROOT"
fi

##   
# More subshell configuration, program and I/O paths, etc.:
#
DDASTOYS=/usr/opt/ddastoys/6.3-dev.e2sar

MPIRUN=$MPIROOT/bin/mpirun
export PATH=$MPIROOT/bin:$PATH
export LD_LIBRARY_PATH=$MPIROOT/lib:$LD_LIBRARY_PATH

export FIT_CONFIGFILE=$PWD/fitconfig.txt
export TEMPLATE_CONFIGFILE=$PWD/template.txt
echo Using fit configfile $FIT_CONFIGFILE
echo Using template configfile $TEMPLATE_CONFIGFILE

input=tcp://localhost/reas
output=tcp://localhost/fitted

if [ "$3" == "mpi" ]; then
    workers=`expr $1 - 3` # 3 reserved for fan out, fan in, sort
    time $MPIRUN --mca btl ^openib \
	 -np $1 $DAQBIN/EventEditor \
	 -l $DDASTOYS/lib/libFitEditorMLInference.so \
	 -s $input \
	 -S $output \
	 -n $workers \
	 -c $2 \
	 -p $3
elif [ "$3" == "threaded" ]; then
    time $DAQBIN/EventEditor \
	 -l $DDASTOYS/lib/libFitEditorMLInference.so \
	 -s $input \
	 -S $output \
	 -n $1 \
	 -c $2 \
	 -p $3
else
    echo "Unsupported parallel strategy: must be 'mpi' or 'threaded'"
    exit 1
fi
