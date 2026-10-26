#!/bin/bash
# Thread-Skalierung, zweiter Durchgang.
# Gegenueber sweep_threads.sh geaendert:
#   - numactl bindet CPUs und Speicher an NUMA-Knoten 1, an dem der genutzte
#     Optane-Namespace haengt (/sys/class/block/pmem1/device/numa_node = 1)
#   - vor jedem Lauf werden Zeitstempel, uptime und fremde ycsb-Prozesse in
#     das Log geschrieben, damit die Maschinenlast dokumentiert ist
#
# Flag-Beleg: src/benchmark/ycsb/ycsb_configuration.cpp Zeile 66 ('b'),
# Zeile 180 (Default backend_count = 1).

set -u

THREAD_FLAG="-b"
CARD=100
THREADS="1 4"
MODES="off A B"
REPS=3
MAXTRIES=4

OUT=~/results/threads_$(date +%Y%m%d_%H%M)
mkdir -p "$OUT"

for t in $THREADS; do
  for m in $MODES; do
    for r in $(seq 1 $REPS); do
      log="$OUT/t${t}_${m}_r${r}.log"
      tries=0
      until [ $tries -ge $MAXTRIES ]; do
        tries=$((tries + 1))
        { echo "=== start $(date -Is) tries=$tries"
          uptime
          echo "--- fremde ycsb-Prozesse:"
          ps aux | grep "[y]csb" | grep -v pkonrad
          echo "---"
        } > "$log" 2>&1
        numactl --cpunodebind=1 --membind=1 \
        ./ycsb -k 512 -T 7480 -Y 90000 -I -U 60 -s -B 1 -u 0 -d 120 -p 10 \
               -K $CARD -C $m $THREAD_FLAG $t \
               -P /mnt/pmem1/pkonrad/spitfire_buf/ \
               -D ~/spitfire_test/db/ >> "$log" 2>&1
        rc=$?
        echo "tries=$tries exit=$rc" >> "$log"
        [ $rc -eq 0 ] && break
      done
      echo "t=$t mode=$m rep=$r tries=$tries exit=$rc"
    done
  done
done

echo "fertig: $OUT"