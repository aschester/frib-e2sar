#!/bin/bash
# Start Tclsh \
exec /usr/bin/wish ${0} ${@}

##
# @file build.sh
# @brief Build events from FRIB-E2SAR pipeline
#

# @todo (ASC 3/4/25): Ring names, glom dt, etc. configurable on cmdline

lappend auto_path [file join $::env(DAQROOT) TclLibs]

package require Tk
package require EventBuilder
package require EVB::connectionList
package require EVB::GUI
package require evbcallouts
package require ring
package require ui

wm title . "FRIB-E2SAR EVB"

proc launchRingSources {} {
    set daqbin $::env(DAQBIN)
    if {$daqbin eq {}} {
	   puts "DAQBIN must be set!"
	   exit	
       }
       set port [EVBC::getOrdererPort]

       set reas0 "[file join $daqbin ringFragmentSource] --evbhost=localhost --evbport=$port --info=reassembler0 --ring=tcp://localhost/reas0_sort --ids=0 --timeout=20 --expectbodyheaders"
       puts $reas0
       
       exec {*}$reas0 &
       
}

EVBC::initialize -glomdt 1000 -gui on -destring frib_e2sar_evb -glombuild true
EVBC::onBegin

set output [Output::getInstance .output]
grid .output -sticky nsew
grid rowconfigure . {0} -weight 1
grid columnconfigure . {0} -weight 1

after [expr 2*3000]

puts $::EVBC::pipefd "EVB::config set window 40"

launchRingSources
