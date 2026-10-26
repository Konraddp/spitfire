#!/bin/bash
# Schreibpfad Stufe 1: gleiche Kardinalitaeten wie die Lesekampagne vom
# 21.08., aber mit Updates (-u 0.1) und nur zwei Armen.
#
# Mode A fehlt absichtlich: die Validierung vom 17.09. zeigt, dass Mode A
# unter Updates von off und B abweichende Pruefsummen liefert. Eine
# komprimierte Seite kann nicht blockweise mit unkomprimierten Bytes
# ueberschrieben werden, und genau das tut der Flush-Pfad nach einem
# Volldecode. Durchsatzwerte fuer Mode A waeren daher nicht verwertbar.
#
# Keine NUMA-Bindung, damit die Zahlen mit der Hauptkampagne vergleichbar
# bleiben.

set -u

# Zwei gleichzeitige Laeufe loeschen sich gegenseitig das NVM-Verzeichnis
# und die DB. Am 17.09. zweimal passiert -- daher dieser Riegel.
if pgrep -u "$USER" ycsb > /dev/null; then
    echo "FEHLER: es laeuft bereits ein ycsb-Prozess. Abbruch."
    exit 1
fi

CARDS="10 100 1000 10000 100000 500000"
MODES="off B"
REPS=3
MAXTRIES=4
UPDATE_RATIO=0.1

OUT=~/results/write_$(date +%Y%m%d_%H%M)
mkdir -p "$OUT"

SUMMARY="$OUT/summary.csv"
echo "mode,cardinality,rep,tries,status,warmup_ops,run_ops" > "$SUMMARY"

for card in $CARDS; do
  for m in $MODES; do
    for r in $(seq 1 $REPS); do
      log="$OUT/K${card}_${m}_r${r}.log"
      tries=0
      rc=1
      until [ $tries -ge $MAXTRIES ]; do
        tries=$((tries + 1))
        { echo "=== start $(date -Is) tries=$tries"
          uptime
          echo "--- fremde ycsb-Prozesse:"
          ps aux | grep "[y]csb" | grep -v pkonrad
          echo "---"
        } > "$log" 2>&1
        ./ycsb -k 512 -T 7480 -Y 90000 -I -U 60 -s -B 1 -u $UPDATE_RATIO \
               -d 120 -p 10 -K $card -C $m -b 1 \
               -P /mnt/pmem1/pkonrad/spitfire_buf/ \
               -D ~/spitfire_test/db/ >> "$log" 2>&1
        rc=$?
        echo "tries=$tries exit=$rc" >> "$log"
        [ $rc -eq 0 ] && break
      done

      # Ergebniszeile: Feld NF-3 = warmup_ops, NF-2 = run_ops (wie sweep.sh)
      line=$(grep "::" "$log" | tail -1)
      if [ $rc -eq 0 ] && [ -n "$line" ]; then
        warm=$(echo "$line" | awk '{print $(NF-3)}')
        runv=$(echo "$line" | awk '{print $(NF-2)}')
        echo "$m,$card,$r,$tries,OK,$warm,$runv" >> "$SUMMARY"
      else
        echo "$m,$card,$r,$tries,FAILED,," >> "$SUMMARY"
      fi

      echo "card=$card mode=$m rep=$r tries=$tries exit=$rc run=${runv:--}"
    done
  done
done

echo "fertig: $OUT"