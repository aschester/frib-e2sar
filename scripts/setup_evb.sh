#!/bin/bash
# Start Tclsh \
    exec /usr/bin/wish ${0} ${@}

##
# @file setup_evb.sh
# @brief Initialize EVB and configure clients.
#

## @todo (ASC 3/7/25): Configure destring, glombuild, etc. via command line.

lappend auto_path [file join $::env(DAQROOT) TclLibs]

package require Tk
package require EventBuilder
package require EVB::connectionList
package require EVB::GUI
package require evbcallouts
package require ring
package require ui
package require cmdline

# Options:

set usage "Setup EVB for FRIB-E2SAR workflows\nOptions:"
set options {
    {sink.arg   ""   "Ringbuffer sink for built data"}
    {window.arg 1000 "Event build coincidence window in nanoseconds"}
}
set mandatory [list sink]

proc usage {} {
    puts stderr [::cmdline::usage $::options $::usage]
}

#  Parse the command line arguments:

set parsed [cmdline::getoptions argv $options $usage]
set parsed [dict create {*}$parsed]

foreach option $mandatory {
    if {[dict get $parsed $option] eq ""} {
        puts stderr "\nERROR: -$option is required\n"
        usage
        exit -1
    }
}

set evbring [dict get $parsed sink]
set glomdt  [dict get $parsed window]

# Start EVB:

wm title . "FRIB-E2SAR EVB"

proc launchRingSources {} {
    set daqbin $::env(DAQBIN)
    if {$daqbin eq {}} {
	   puts "DAQBIN must be set!"
	   exit	
       }
       set port [EVBC::getOrdererPort]
       puts "Orderer listening on $port"
       
       # Add additional clients here:
       
       set reas0 "[file join $daqbin ringFragmentSource] --evbhost=localhost --evbport=$port --info=reas0 --ring=tcp://localhost/reas0_sort --ids=0 --expectbodyheaders"
       puts $reas0

       # Start all clients:
       
       exec {*}$reas0 &
}

EVBC::initialize -gui on -destring $evbring -glombuild yes -glomdt $glomdt
EVBC::onBegin

set output [Output::getInstance .output]
grid .output -sticky nsew
grid rowconfigure . {0} -weight 1
grid columnconfigure . {0} -weight 1

after [expr 1000]

launchRingSources
