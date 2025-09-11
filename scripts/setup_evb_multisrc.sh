#!/bin/bash
# Start Tclsh \
    exec /usr/bin/wish ${0} ${@}

##
# @file setup_evb.sh
# @brief Initialize EVB and configure clients.
# @details
# Use this script to set up an event-building pipleine for FRIB data
# transmitted over E2SAR. There are a few things to note here, as this
# script is intended to be modified by users for their own needs:
# - Data sources are hardcoded, edit the `launchRingSources` proc for
#   your source configuraiton
# - This script assumes an NSCLDAQ environment is set by e.g., sourcing
#   the daqsetup.sh script from an installed NSCLDAQ version (e.g., by
#   caller)
# - Assumes input rings to EVB pipeline exist (e.g., created by caller)
#

##
# @todo (ASC 5/21/25):
# - Improved control over configuration options i.e., set source Id
#
 
lappend auto_path [file join $::env(DAQROOT) TclLibs]

package require cmdline
package require EventBuilder
package require evbcallouts
package require EVB::connectionList
package require EVB::GUI
package require ring
package require Tk
package require ui

wm title . "FRIB-E2SAR EVB"

# Options:

set usage "Setup EVB for FRIB-E2SAR workflows\nOptions:"
set options {
    {source.arg  ""   "Reassembled data ringbuffer source basename"}
    {sink.arg    ""   "Ringbuffer sink for built data"}
    {glomdt.arg  1000 "Event build coincidence window in nanoseconds"}
    {window.arg  20   "Event orderer build window in seconds"}
    {ddasraw.arg 0    "NSCLDAQ 12 DDAS raw data yes/no = 1/0"}
}
set mandatory [list source sink]

proc usage {} {
    puts stderr [::cmdline::usage $::options $::usage]
}

# Parse the command line arguments:

set parsed [cmdline::getoptions argv $options $usage]
set parsed [dict create {*}$parsed]

foreach option $mandatory {
    if {[dict get $parsed $option] eq ""} {
        puts stderr "\nERROR: -$option is required\n"
        usage
        exit 1
    }
}

set srcname   [dict get $parsed source]
set evbring   [dict get $parsed sink]
set glomdt    [dict get $parsed glomdt]
set window    [dict get $parsed window]
set ddasraw   [dict get $parsed ddasraw]

##
# @brief Configure and launch the ringFragmentSources input to the EVB pipe
# @details
# Users are expected to modify this function for thier particular application
# by adding and starting the proper clients.
# @param srcname Data source name (not URI)
#
proc launchRingSources {srcname} {
    set daqbin $::env(DAQBIN)

    set port [EVBC::getOrdererPort]
    puts "Orderer listening on $port"
    
    ##
    # Add additional clients here:
    #
    
    set reas0 "[file join $daqbin ringFragmentSource]  \
    	--evbhost=localhost			       \
	--evbport=$port 			       \
	--ring=tcp://localhost/${srcname}_0_sort       \
	--ids=0					       \
	--info=${srcname}_0			       \
   	--expectbodyheaders"		       	       

    set reas2 "[file join $daqbin ringFragmentSource]  \
    	--evbhost=localhost			       \
	--evbport=$port 			       \
	--ring=tcp://localhost/${srcname}_2_sort       \
	--ids=2				               \
	--info=${srcname}_2			       \
   	--expectbodyheaders"
    
    ##
    # Start all clients:
    #
    
    set fd0 [open "| $reas0 |& cat" "r"]
    fconfigure $fd0 -blocking 0
    set fd2 [open "| $reas2 |& cat" "r"]
    fconfigure $fd2 -blocking 0
}

EVBC::initialize -gui off -destring $evbring -glombuild on -glomdt $glomdt

EVBC::onBegin

####################
# Defaults:        #
#------------------#
# Xon      3000000 #
# Xoff     4000000 #
# perQXon    50000 #
# perQXoff  400000 #
####################

set Xon      30000000
set Xoff     40000000
set perQXon    500000
set perQXoff  4000000

EVBC::configParams window $window
EVBC::configParams XonThreshold $Xon
EVBC::configParams XoffThreshold $Xoff
EVBC::configParams perQXonThreshold $perQXon
EVBC::configParams perQXoffThreshold $perQXoff
   
set output [Output::getInstance .output]
grid .output -sticky nsew
grid rowconfigure . {0} -weight 1
grid columnconfigure . {0} -weight 1

after [expr 1000]

launchRingSources $srcname
