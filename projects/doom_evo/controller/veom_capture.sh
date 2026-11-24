#!/bin/sh
# Boot sniffer launcher (lives on sda0 so it is reachable at early boot). Logs to
# /dev/shmem, waits for veom0, latches tcpdump to /dev/shmem/boot.pcap (RAM, no
# sda1 dependency) BEFORE the DSI clients connect. Single-instance; self-limits.
LOG=/dev/shmem/evo_shim.log
echo "launcher: start" >> $LOG 2>/dev/null
LOCK=/dev/shmem/veom_cap.lock
if ! ( set -o noclobber; : > "$LOCK" ) 2>/dev/null ; then echo "launcher: lock busy -> exit" >> $LOG 2>/dev/null; exit 0; fi
PCAP=/dev/shmem/boot.pcap
rm -f "$PCAP" /dev/shmem/boot_tcpdump.pid
echo "launcher: waiting for veom0" >> $LOG 2>/dev/null
i=0
while [ $i -lt 1500 ]; do
  if ifconfig veom0 >/dev/null 2>&1 ; then
    tcpdump -i veom0 -s 0 -w "$PCAP" host 160.48.199.253 >/dev/null 2>&1 &
    TP=$!
    sleep 0.3
    if kill -0 $TP 2>/dev/null ; then
      echo "launcher: tcpdump latched pid=$TP (iter=$i)" >> $LOG 2>/dev/null
      echo $TP > /dev/shmem/boot_tcpdump.pid
      break
    fi
  fi
  sleep 0.1 ; i=$((i+1))
done
( sleep 90 ; kill $(cat /dev/shmem/boot_tcpdump.pid 2>/dev/null) 2>/dev/null ; echo "launcher: stopped" >> $LOG 2>/dev/null ; rm -f "$LOCK" 2>/dev/null ) >/dev/null 2>&1 &
