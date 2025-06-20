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
# - Improved control over configuration options i.e., set source IDs
# - Read path for filter from env?
# - Configuration of ringFragmentSource is inflexible and doesn't cover
#   enough of the possible cases of rawdata input and/or building data
#
 
lappend auto_path [file join $::env(DAQROOT) TclLibs]

package require EventBuilder
package require EVB::connectionList
package require EVB::GUI

package require cmdline
package require evbcallouts
package require ring
package require Tk
package require ui
   
# Options:

set usage "Setup EVB for FRIB-E2SAR workflows\nOptions:"
set options {
    {source.arg  ""   "Reassembled data ringbuffer source basename"}
    {sink.arg    ""   "Ringbuffer sink for built data"}
    {build.arg   1    "Build events (yes,no = 1,0)"}
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
set glombuild [dict get $parsed build]
set glomdt    [dict get $parsed glomdt]
set window    [dict get $parsed window]
set ddasraw   [dict get $parsed ddasraw]

# Start EVB:

wm title . "FRIB-E2SAR EVB"

##
# @brief Configure and launch the ringFragmentSources input to the EVB pipe
# @details
# Users are expected to modify this function for thier particular application
# by adding and starting the proper clients.
# @param build Build events yes/no = 1/0
#
proc launchRingSources {ddasraw srcname} {
    set daqbin $::env(DAQBIN)

    ##
    # Add additional clients here:
    #
    
    set reas0 "[file join $daqbin ringFragmentSource] --evbhost=localhost --ids=0 --expectbodyheaders "
	   
    if {$ddasraw == 1} { # Building events on recv
	   set port [EVBC::getOrdererPort]
	   puts "Orderer listening on $port"
	   append reas0 "--evbport=$port --info=${srcname}_t00_sort --ring=tcp://localhost/${srcname}_t00_sort"
       } else { # Otherwise read from the recv ringbuffer
	   append reas0 "--evbname myOrderer --info=${srcname}_t00 --ring=tcp://localhost/${srcname}_t00 "
       }
	  
    puts $reas0

    ##
    # Start all clients:
    #
    
    exec {*}$reas0 &
}

EVBC::configParams window $window

if {$glombuild == 1} {
       EVBC::initialize -gui off -destring $evbring -glombuild $glombuild -glomdt $glomdt
       EVBC::onBegin
   } else {
       set daqbin $::env(DAQBIN)
       set startScript [file join $daqbin startOrderer]
       set orderer [file join $daqbin Orderer]
       set pipecommand "$orderer 2> orderer.err"

       set glom "[file join $daqbin glom] --dt=$glomdt -s 0xff --nobuild"
       append pipecommand " | $glom"

       set filter "~/frib-e2sar/bin/evbfilter"
       append pipecommand " | $filter"

       set stdintoring "[file join $daqbin stdintoring] $evbring"
       append pipecommand " | $stdintoring |& cat"

       set pipefd [open "| $pipecommand" w+]

       fconfigure $pipefd -buffering line -blocking 0

       puts $pipefd "source $startScript"
       ::flush $pipefd
       puts $pipefd "set ::OutputRing $evbring"
       ::flush $pipefd
       puts $pipefd "start myOrderer"
       ::flush $pipefd
   }
   
set output [Output::getInstance .output]
grid .output -sticky nsew
grid rowconfigure . {0} -weight 1
grid columnconfigure . {0} -weight 1

after [expr 2000]

launchRingSources $ddasraw $srcname
