#!/bin/bash
# ============================================================================
# sweep.sh: benchmark campaign: mode x cardinality x repetitions
# ============================================================================
# Produces one log file per run in $OUT. A run that segfaults is retried up to
# MAX_TRIES times; if it never succeeds it is recorded as missing (no log),
# which the parser must treat as a gap, NOT as a zero.
#
# Known issue this works around: Mode A crashes sporadically (~50%) under high
# eviction pressure. Cause not identified (not reproducible under gdb, no
# working TSan on this machine). See thesis methodology chapter.
#
# Usage:  ./sweep.sh            (full campaign)
#         ./sweep.sh --dry      (print what would run, no execution)
# ============================================================================

set -u

# ---- configuration ---------------------------------------------------------
BUILD=~/spitfire/build
OUT=~/results/sweep_$(date +%Y%m%d_%H%M)
NVM=/mnt/pmem1/pkonrad/spitfire_buf/
DB=~/spitfire_test/db/

SCALE=512          # -k : 512k tuples (~1.4 GB)
DRAM_PAGES=7480    # -T : 122 MB DRAM buffer
NVM_PAGES=90000    # -Y : NVM tier large enough for the whole DB
WARMUP=10          # -U
DURATION=20        # -d
PROFILE=5          # -p : must divide DURATION, else Spitfire crashes on exit

MODES=(off A B)
CARDINALITIES=(10 100 1000 10000 100000 500000)
REPS=5
MAX_TRIES=6        # retries per run before giving up

DRY=${1:-}

# ---- helpers ---------------------------------------------------------------
run_once() {   # $1=mode $2=cardinality $3=logfile  -> exit status of ycsb
    "$BUILD/ycsb" -k $SCALE -T $DRAM_PAGES -Y $NVM_PAGES -I -U $WARMUP -s \
        -B 1 -P "$NVM" -D "$DB" -u 0 -d $DURATION -p $PROFILE \
        -C "$1" -K "$2" > "$3" 2>&1
}

mkdir -p "$OUT"
SUMMARY="$OUT/summary.csv"
echo "mode,cardinality,rep,tries,status,warmup_ops,run_ops" > "$SUMMARY"

total=$(( ${#MODES[@]} * ${#CARDINALITIES[@]} * REPS ))
done_n=0
failed_n=0
start_ts=$(date +%s)

echo "Campaign: ${#MODES[@]} modes x ${#CARDINALITIES[@]} cardinalities x $REPS reps = $total runs"
echo "Output:   $OUT"
echo

for card in "${CARDINALITIES[@]}"; do
  for rep in $(seq 1 $REPS); do
    # modes interleaved inside the repetition loop: if machine conditions
    # drift over the hours, the drift hits all modes equally
    for mode in "${MODES[@]}"; do
      log="$OUT/mode-${mode}_card-${card}_rep-${rep}.log"
      done_n=$(( done_n + 1 ))

      if [ "$DRY" = "--dry" ]; then
          echo "[dry] $log"
          continue
      fi

      status="FAIL"
      tries=0
      for t in $(seq 1 $MAX_TRIES); do
          tries=$t
          if run_once "$mode" "$card" "$log"; then
              status="OK"
              break
          fi
      done

      if [ "$status" = "OK" ]; then
          # last line: "<scale> <backends> <ops> <upd> <zipf> :: <warmup> <run> <abort> <mem>"
          line=$(tail -1 "$log")
          warm=$(echo "$line" | awk '{print $(NF-3)}')
          runv=$(echo "$line" | awk '{print $(NF-2)}')
      else
          warm=""; runv=""
          failed_n=$(( failed_n + 1 ))
          mv "$log" "${log}.failed" 2>/dev/null
      fi

      echo "$mode,$card,$rep,$tries,$status,$warm,$runv" >> "$SUMMARY"

      elapsed=$(( $(date +%s) - start_ts ))
      printf "[%3d/%3d] card=%-6s rep=%d mode=%-3s tries=%d %-4s run=%-14s (%dm elapsed)\n" \
             "$done_n" "$total" "$card" "$rep" "$mode" "$tries" "$status" "${runv:--}" $(( elapsed / 60 ))
    done
  done
done

echo
echo "Done. $failed_n of $total runs failed permanently."
echo "Summary: $SUMMARY"