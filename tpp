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
# usage: tpp [-s num]... [-r] <prefix> <start> <end> [device]
#   e.g. tpp -s 3612 -s 3613 -r 555 3610 3614 /dev/cua1
#
# =============================================================================

set revision {$Revision: 1.7 $}
set version {}
set pgmname "THE PHANTOM PHREAK"
set pgmauthor "c1ph3rpunk"
regexp {Revision: ([^ ]+)} $revision -> version

# ---- argument parsing (manual - no cmdline package in Tcl 7.3) ----
set skip_list {}
set random_scan 0
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
    } elseif {$arg == "-r"} {
        set random_scan 1
    } else {
        lappend args $arg
    }
    incr i
}

if {[llength $args] < 3} {
    puts "$pgmname v$version \[$pgmauthor\]"
    puts "usage: tpp \[-s num\]... \[-r\] <prefix> <start> <end> \[device\]"
    puts "       device defaults to /dev/cua1"
    puts "       -s can be specified multiple times"
    puts "       -r dials the range in a scrambled, not sequential, order"
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
# general log message
proc tpp_log {msg} {
    puts "\[TPP:GENERAL\]: $msg"
}

# expect_out is not a Tcl keyword - it's an ordinary global array that
# Expect's own `expect` command writes to by convention whenever it
# matches something (expect_out(buffer) holds everything consumed up
# to and including the match). `global expect_out` below just makes
# that same array visible inside these procs; nothing special about
# the `global` call itself.

# used when we send something TO the modem
proc modem_out {cmd {terminator "\r"}} {
    global expect_out
    puts "\[TPP:MODEM OUT\]: $cmd"
    catch {set expect_out(buffer) ""}
    send "$cmd$terminator"
}

# used when we get something FROM the modem
proc modem_in {} {
    global expect_out
    set txt [string trim $expect_out(buffer)]
    foreach line [split $txt "\r\n"] {
        set line [string trim $line]
        if {$line != ""} {
            puts "\[TPP:MODEM IN\]: $line"
        }
    }
}

# ---- helper for -r: walk the range in a scrambled order instead of
# sequentially. No rand()/clock in this Tcl build, so this isn't a
# real PRNG - it steps through 0..count-1 by a stride that's coprime
# with count, which is guaranteed to visit every index exactly once,
# just not in order. pid seeds where the walk starts.
proc tpp_gcd {a b} {
    while {$b != 0} {
        set t [expr {$a % $b}]
        set a $b
        set b $t
    }
    return $a
}

# -------------------------
# variables
set prefix     [lindex $args 0]
set start      [lindex $args 1]
set end        [lindex $args 2]
set modem_dev  [expr {[llength $args] >= 4 ? [lindex $args 3] : "/dev/cua1"}]
set cmd_timeout  5
set dial_timeout 60

# timeout is another Expect convention, not a keyword: it's a plain
# global variable that `expect` reads (if a call doesn't pass its own
# -timeout) to decide how many seconds to wait for a match before
# giving up. This `set` just assigns it like any other variable - the
# dial loop's expect overrides it per-call with -timeout $dial_timeout,
# every other expect in this script falls back to this global.
set timeout $cmd_timeout
set init_string "ATX4E0Q0V1S7=45"
set completion_messages {
    "Scan complete, go buy some Mountain Dew."
    "Scan complete, why are you still here?"
    "Scan complete, you're out of Jolt."
}

# -------------------------

# unlike timeout/expect_out, log_user is a real Expect command, not a
# variable convention. It controls Expect's own automatic echo of the
# spawned process's raw I/O to stdout (on by default). We turn it off
# here because the script already logs everything itself via
# modem_out/modem_in/tpp_log - without this, the raw modem bytes would
# print a second time alongside our formatted [TPP:...] lines.
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

# ---- -r setup: scramble the dial order without a real RNG ----
# scan_count is how many numbers are in range; the loop below always
# dials exactly that many, once each, whether -r is on or not.
#
# The trick: instead of picking random numbers (which risks repeats
# or misses without a real RNG), walk the range 0..scan_count-1 by a
# fixed stride (scan_step) that shares no common factor with
# scan_count. Modular arithmetic guarantees that stepping by a value
# coprime with the modulus visits every residue 0..scan_count-1
# exactly once before repeating - so every number in range still
# gets dialed, just not in ascending order.
#
# scan_step: start the search around the midpoint of the range (a
# small step like 1 or 2 would barely scramble anything) and count
# up until tpp_gcd finds one that's coprime with scan_count. Falls
# back to 1 (plain sequential) in the unlikely case nothing smaller
# than scan_count qualifies.
#
# scan_idx: where the walk starts. pid is the only source of
# variation this old Tcl build has (no rand()/clock), so each run
# starts at a different offset into the same stride pattern.
set scan_count [expr {$end - $start + 1}]
set scan_step 1
set scan_idx 0
if {$random_scan} {
    set scan_step [expr {$scan_count / 2 + 1}]
    while {[tpp_gcd $scan_step $scan_count] != 1} {
        incr scan_step
        if {$scan_step >= $scan_count} {
            set scan_step 1
            break
        }
    }
    set scan_idx [expr {[pid] % $scan_count}]
    tpp_log "randomizing dial order (step=$scan_step)"
}

# main dial loop - count just tracks how many of scan_count numbers
# have been dialed so far; n (the actual number offset) comes either
# sequentially (count) or via the scrambled walk (scan_idx), which
# is advanced mod scan_count each time so it wraps to cover the
# whole range without ever repeating a number.
for {set count 0} {$count < $scan_count} {incr count} {
    if {$random_scan} {
        set n [expr {$start + $scan_idx}]
        set scan_idx [expr {($scan_idx + $scan_step) % $scan_count}]
    } else {
        set n [expr {$start + $count}]
    }

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
