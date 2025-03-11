#!/bin/bash

if [ -z "$1" ]; then
    prefix=$HOME/frib-e2sar
else
    prefix=$1
fi

if [ -z "$DAQROOT" ]; then
    . /usr/opt/daq/12.1-pre6.e2sar/daqsetup.bash
fi


# Clean build and install:

rm -rf build

(mkdir build ;
 cd build ;
 cmake .. -DNSCLDAQ_ROOT=$DAQROOT -DCMAKE_INSTALL_PREFIX=$prefix ;
 cmake --build . -j4 ;
 cmake --install .)
