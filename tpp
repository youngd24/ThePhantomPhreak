#!/bin/sh
# the next line restarts using expect \
exec expect -- "$0" ${1+"$@"}
# =============================================================================
#
# tpp - "The Phantom Phreak" - demon/war dialer for Slackware 2.0.1
# Talks directly to a USR Sportster on a serial device (e.g. /dev/cua1)
# rather than through pppd/dip, since we just want AT-command scanning,
# not an actual SLIP/PPP session. Defaults to /dev/cua1.
#
# $Id: tpp,v 1.7 1998/09/06 03:48:30 youngd Exp youngd $
#
# usage: tpp [-s num]... <prefix> <start> <end> [device]
#   e.g. tpp -s 3612 -s 3613 555 3610 3614 /dev/cua1
#
# =============================================================================

set revision {$Revision: 1.7 $}
set version {}
set pgmname "THE PHANTOM PHREAK"
set pgmauthor "c1ph3rpunk"
regexp {Revision: ([^ ]+)} $revision -> version

# ---- argument parsing (manual - no cmdline package in Tcl 7.3) ----
set skip_list {}
set args {}
set i 0

while {$i < $argc} {
    set arg [lindex $argv $i]
    if {$arg == "-s"} {
        incr i
        if {$i >= $argc} {
            puts "usage: -s requires a number argument"
            exit 1
        }
        lappend skip_list [lindex $argv $i]
    } else {
        lappend args $arg
    }
    incr i
}

if {[llength $args] < 3} {
    puts "$pgmname v$version \[$pgmauthor\]"
    puts "usage: tpp \[-s num\]... <prefix> <start> <end> \[device\]"
    puts "       device defaults to /dev/cua1"
    puts "       -s can be specified multiple times"
    exit 1
}

# -------------------------------------------------------------------

puts {
*********************************************
*                                           *
*    T H E   P H A N T O M   P H R E A K    *
*                                           *
*  ---------------------------------------  *
*      a u t o m a t e d   d i a l e r      *
*  ---------------------------------------  *
*                                           *
*               by c1ph3rpunk               *
*                                           *
*********************************************
}

# ---- output helpers ----
proc tpp_log {msg} {
    puts "\[TPP\]: $msg"
}
proc modem_out {cmd {terminator "\r"}} {
    global expect_out
    puts "\[MODEM OUT\]: $cmd"
    catch {set expect_out(buffer) ""}
    send "$cmd$terminator"
}
proc modem_in {} {
    global expect_out
    set txt [string trim $expect_out(buffer)]
    foreach line [split $txt "\r\n"] {
        set line [string trim $line]
        if {$line != ""} {
            puts "\[MODEM IN\]: $line"
        }
    }
}

# -------------------------
set prefix     [lindex $args 0]
set start      [lindex $args 1]
set end        [lindex $args 2]
set modem_dev  [expr {[llength $args] >= 4 ? [lindex $args 3] : "/dev/cua1"}]
set cmd_timeout  5
set dial_timeout 60
set timeout $cmd_timeout
set init_string "ATX4E0Q0V1S7=45"
set completion_messages {
    "Scan complete, go buy some Mountain Dew."
    "Scan complete, why are you still here?"
    "Scan complete, you're out of Jolt."
}
log_user 0

tpp_log "tpp v$version starting..."

if {[llength $skip_list] > 0} {
    tpp_log "skipping: $skip_list"
}

# line discipline for the Sportster - adjust speed as needed
# (older stty doesn't understand GNU's "-F device" syntax; redirect
# into the device instead to target it. clocal keeps the fd alive
# across real DCD drops on hangup/disconnect.)
exec stty 9600 cs8 -cstopb -parenb -echo raw clocal < $modem_dev

# open the modem device
tpp_log "Opening modem $modem_dev"
set fd [open $modem_dev r+]
spawn -open $fd

# Reset modem
tpp_log "Resetting modem"
modem_out "ATZ"
expect {
    "OK" { modem_in }
    timeout {
        tpp_log "modem did not respond to ATZ, aborting"
        catch {close $fd}
        exit 1
    }
}

# ATZ resets to the NVRAM profile; we then force the settings the
# scan actually depends on rather than trusting whatever profile
# is stored, once, up front:
#   X4 - full result codes (adds BUSY and NO DIALTONE detection)
#   E0 - command echo off
#   Q0 - result codes are sent (not suppressed)
#   V1 - verbal (word) result codes, not numeric
#   S7=45 - give up waiting for carrier after 45s
tpp_log "initializing modem on $modem_dev"
modem_out $init_string
expect {
    "OK" { modem_in }
    timeout {
        tpp_log "modem did not accept init string, aborting"
        catch {close $fd}
        exit 1
    }
}

# start scan
tpp_log "scanning [format %s%04d $prefix $start] through [format %s%04d $prefix $end]"
for {set n $start} {$n <= $end} {incr n} {
    if {[lsearch -exact $skip_list $n] >= 0} {
        tpp_log "[format "%s%04d" $prefix $n]: skipped"
        continue
    }

    set number [format "%s%04d" $prefix $n]

    modem_out "ATDT$number"
    expect {
        -timeout $dial_timeout
        -re "CONNECT" { modem_in; set result "CARRIER" }
        "BUSY"        { modem_in; set result "busy" }
        "NO CARRIER"  { modem_in; set result "no-carrier" }
        "NO DIALTONE" { modem_in; set result "no-dialtone" }
        "ERROR"       { modem_in; set result "error" }
        timeout       { modem_in; set result "timeout" }
    }
    tpp_log "$number: $result"

    if {$result == "CARRIER"} {
        sleep 1
        modem_out "+++" ""
        sleep 1
        expect {
            "OK" { modem_in }
            timeout {}
        }
        modem_out "ATH0"
        expect {
            "OK" { modem_in }
            timeout { modem_in }
        }
        tpp_log "waiting for line to clear"
        sleep 5
    }

    sleep 2
}

catch {close $fd}

# print a random closing message
# (no rand()/srand() expr functions and no clock command on this old
# Tcl build - pid is the only source of variation available, but it's
# enough to rotate through the list run to run)
set idx [expr {[pid] % [llength $completion_messages]}]
set msg [lindex $completion_messages $idx]
tpp_log $msg
