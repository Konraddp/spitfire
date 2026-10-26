#!/bin/bash
# sweep_cross.sh: Zwischen- und Hochpunkte zur Eingrenzung des Crossovers
# 20.000/50.000: Schnittpunkt A gegen off eingrenzen
# 200.000/300.000: Erholung pruefen (dort noch kein 32-Bit-Ueberlauf)
# 4 Kardinalitaeten x 2 Arme x 3 Wiederholungen = 24 Laeufe, ca. 2 h
# Quellbaum: campaign-20260821 (6b746cb)

set -u

BUILD=~/spitfire/build
OUT=~/results/cross_$(date +%Y%m%d_%H%M)
NVM=/mnt/pmem1/pkonrad/spitfire_buf/
DB=~/spitfire_test/db/

SCALE=512
DRAM_PAGES=7480
NVM_PAGES=90000
WARMUP=60
DURATION=120
PROFILE=10

MODES=(off A)
CARDINALITIES=(20000 50000 200000 300000)
REPS=3
MAX_TRIES=4

DRY=${1:-}

if pgrep -u "$USER" ycsb > /dev/null; then
    echo "FEHLER: es laeuft bereits ein ycsb-Prozess. Abbruch."
    exit 1
fi

run_once() {
    "$BUILD/ycsb" -k $SCALE -T $DRAM_PAGES -Y $NVM_PAGES -I -U $WARMUP -s \
        -B 1 -u 0 -d $DURATION -p $PROFILE \
        -K "$2" -C "$1" -P "$NVM" -D "$DB" > "$3" 2>&1
}

mkdir -p "$OUT"
SUMMARY="$OUT/summary.csv"
echo "mode,cardinality,rep,tries,status,run_ops" > "$SUMMARY"

total=$(( ${#MODES[@]} * ${#CARDINALITIES[@]} * REPS ))
done_n=0
failed_n=0
start_ts=$(date +%s)

echo "Zusatzlaeufe: ${#CARDINALITIES[@]} Kardinalitaeten x ${#MODES[@]} Arme x $REPS Wdh. = $total Laeufe"
echo "Ausgabe:  $OUT"
echo "Start:    $(date)"
echo

for card in "${CARDINALITIES[@]}"; do
  for mode in "${MODES[@]}"; do
    for rep in $(seq 1 $REPS); do
      log="$OUT/K${card}_${mode}_r${rep}.log"
      done_n=$(( done_n + 1 ))

      if [ "$DRY" = "--dry" ]; then
          echo "[dry] $log"
          continue
      fi

      {
        echo "LOAD_INFO uptime: $(uptime)"
        echo "LOAD_INFO foreign ycsb: $(pgrep -c ycsb)"
      } > "${log}.load"

      status="FAILED"
      tries=0
      for t in $(seq 1 $MAX_TRIES); do
          tries=$t
          if run_once "$mode" "$card" "$log"; then
              status="OK"
              break
          fi
      done

      if [ "$status" = "OK" ]; then
          line=$(tail -1 "$log")
          runv=$(echo "$line" | awk '{print $(NF-2)}')
      else
          runv=""
          failed_n=$(( failed_n + 1 ))
          mv "$log" "${log}.failed" 2>/dev/null
      fi

      echo "$mode,$card,$rep,$tries,$status,$runv" >> "$SUMMARY"

      elapsed=$(( $(date +%s) - start_ts ))
      printf "[%2d/%2d] card=%-6s mode=%-3s rep=%d tries=%d %-6s run=%-14s (%dm)\n" \
             "$done_n" "$total" "$card" "$mode" "$rep" "$tries" "$status" \
             "${runv:--}" $(( elapsed / 60 ))
    done
  done
done

echo
echo "Fertig um $(date). $failed_n von $total Laeufen endgueltig gescheitert."
echo "Auswertung: python3 ~/analyze.py $SUMMARY"
