#!/bin/sh
# rmthreads.sh - what each thread of a running process costs over a window:
# its CPU time (utime + stime, in the kernel's 100 Hz ticks) and its context
# switches per second (voluntary and not), busiest first.  For newton on the
# reMarkable (docs/host-remarkable.md, "Battery life"): BusyBox's shell and
# awk only (the tablet has no join, pkill or perf).
#
#   scp tools/remarkable/rmthreads.sh root@10.11.99.1:/tmp/
#   ssh root@10.11.99.1 'sh /tmp/rmthreads.sh $(pidof newton) 20'
#
# A thread's wait shows what it is: cat /proc/<pid>/task/<tid>/wchan
# (hrtimer_nanosleep a timed sleep, do_sys_poll a poll, futex_wait_queue a
# lock or condition).
pid=$1; secs=${2:-20}
snap() { for t in /proc/$pid/task/*; do
  id=$(basename $t); n=$(tr ' ' '_' < $t/comm)
  set -- $(cut -d' ' -f14,15 $t/stat)
  c=$(( $1 + $2 ))
  w=$(awk '/^voluntary_ctxt|^nonvoluntary_ctxt/ {s+=$2} END {print s}' $t/status)
  echo "$id $n $c $w"; done; }
snap > /tmp/rmthreads.a; sleep $secs; snap > /tmp/rmthreads.b
awk -v s=$secs 'NR==FNR {c[$1]=$3; w[$1]=$4; next} ($1 in c) {printf "%-7s %-16s cpu %4d ticks  switches/s %7.1f\n", $1, $2, $3-c[$1], ($4-w[$1])/s}' \
  /tmp/rmthreads.a /tmp/rmthreads.b | sort -k7 -n -r
rm -f /tmp/rmthreads.a /tmp/rmthreads.b
