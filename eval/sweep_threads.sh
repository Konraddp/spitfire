#!/bin/bash
# Thread-Skalierung: eine Kardinalitaet, drei Arme, vier Workerzahlen.
# 12 Zellen x 3 Wiederholungen = 36 Laeufe a 180 s -> ca. 2 h inkl. Load.
# Fuer eine Stunde: REPS=1 setzen (dann aber keine Streuungsangabe moeglich).
#
# Verifiziert gegen src/benchmark/ycsb/ycsb_configuration.cpp:
#   Zeile  66: { "backend_count", optional_argument, NULL, 'b' }
#   Zeile 180: state.backend_count = 1   (Default -- deshalb fehlt das Flag im
#              Kampagnenaufruf und alle 54 Laeufe waren einthreadig)

set -u

THREAD_FLAG="-b"
CARD=100                  # niedrige Kardinalitaet: A liegt hier ueber off,
                          # die Ordnung der Arme ist am deutlichsten
THREADS="1 4 8 16"
MODES="off A B"
REPS=3
MAXTRIES=4                # Mode A stuerzt sporadisch ab

OUT=~/results/threads_$(date +%Y%m%d_%H%M)
mkdir -p "$OUT"

for t in $THREADS; do
  for m in $MODES; do
    for r in $(seq 1 $REPS); do
      log="$OUT/t${t}_${m}_r${r}.log"
      tries=0
      until [ $tries -ge $MAXTRIES ]; do
        tries=$((tries + 1))
        ./ycsb -k 512 -T 7480 -Y 90000 -I -U 60 -s -B 1 -u 0 -d 120 -p 10 \
               -K $CARD -C $m $THREAD_FLAG $t \
               -P /mnt/pmem1/pkonrad/spitfire_buf/ \
               -D ~/spitfire_test/db/ > "$log" 2>&1
        rc=$?
        echo "tries=$tries exit=$rc" >> "$log"
        [ $rc -eq 0 ] && break
      done
      echo "t=$t mode=$m rep=$r tries=$tries exit=$rc"
    done
  done
done

echo "fertig: $OUT"
