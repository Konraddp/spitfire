#!/bin/bash
# ============================================================================
# sweep_anchor.sh: Ankerpunkte zur Pruefung der Uebertragbarkeit zwischen
#                  zwei Messgelegenheiten
# ============================================================================
# Problem, das dieses Skript schliesst:
#
# Die Hauptkampagne (21.08.) und die Zusatzkampagne (05.10.) haben KEINE
# gemeinsame Kardinalitaet. Das absolute Niveau der zweiten liegt rund 21 %
# unter dem der ersten. Kapitel 7 legt beide Kurvenstuecke trotzdem
# aneinander -- mit der Begruendung, dass Verhaeltnisse innerhalb einer
# Gelegenheit gebildet werden und eine multiplikative Niveauverschiebung sich
# herauskuerzt.
#
# Das ist bislang eine Annahme. Wird dieselbe Kardinalitaet auf einer dritten
# Gelegenheit erneut gemessen und kommt dasselbe VERHAELTNIS heraus, obwohl
# das NIVEAU wieder anders ist, ist die Annahme belegt.
#
# Erwartungswerte aus der Hauptkampagne (Tabellen 7.2 und 7.3):
#
#   K = 100.000   off 2611   A 2483   B 6619   A/off 0.95   B/A 2.67   B/off 2.54
#   K =  10.000   off 2599   A 2925   B 7581   A/off 1.13   B/A 2.59   B/off 2.92
#
# Pruefkriterium: die VERHAELTNISSE muessen innerhalb weniger Prozent
# getroffen werden. Die ABSOLUTWERTE duerfen beliebig daneben liegen -- genau
# das ist der Punkt der Uebung. Weichen auch die Verhaeltnisse deutlich ab,
# ist die Zusammenfuegung der beiden Kurvenstuecke in Abschnitt 7.3 nicht
# haltbar und muss im Text zurueckgenommen werden.
#
# 100.000 steht zuerst, weil es der wichtigere Anker ist: es liegt direkt am
# strittigen Bereich. Bricht der Lauf vorzeitig ab, ist das Wesentliche
# trotzdem gemessen.
#
# 2 Kardinalitaeten x 3 Arme x 3 Wiederholungen = 18 Laeufe, ca. 1h40.
# Nur 100.000 (CARDINALITIES auf einen Wert kuerzen): 9 Laeufe, ca. 50 min.
#
# WICHTIG -- Stand des Quellbaums. Es muss derselbe Stand sein wie in beiden
# bisherigen Kampagnen, NICHT der Schreibpfad-Stand:
#
#     cd ~/spitfire && git log --oneline -1       # muss 6b746cb sein
#     # falls nicht:
#     git checkout campaign-20260821 && cd build && make -j ycsb
#
# Aufruf:  nohup ./sweep_anchor.sh > ~/anchor_console.log 2>&1 &
#          tail -f ~/anchor_console.log
# ============================================================================

set -u

BUILD=~/spitfire/build
OUT=~/results/anchor_$(date +%Y%m%d_%H%M)
NVM=/mnt/pmem1/pkonrad/spitfire_buf/
DB=~/spitfire_test/db/

SCALE=512
DRAM_PAGES=7480
NVM_PAGES=90000
WARMUP=60
DURATION=120
PROFILE=10

MODES=(off A B)
CARDINALITIES=(100000 10000)
REPS=3
MAX_TRIES=4

DRY=${1:-}

if pgrep -u "$USER" ycsb > /dev/null; then
    echo "FEHLER: es laeuft bereits ein ycsb-Prozess. Abbruch."
    exit 1
fi

if [ ! -x "$BUILD/ycsb" ]; then
    echo "FEHLER: $BUILD/ycsb nicht gefunden oder nicht ausfuehrbar."
    exit 1
fi

run_once() {   # $1=mode $2=cardinality $3=logfile
    "$BUILD/ycsb" -k $SCALE -T $DRAM_PAGES -Y $NVM_PAGES -I -U $WARMUP -s \
        -B 1 -u 0 -d $DURATION -p $PROFILE \
        -K "$2" -C "$1" -P "$NVM" -D "$DB" > "$3" 2>&1
}

mkdir -p "$OUT"
SUMMARY="$OUT/summary.csv"
echo "mode,cardinality,rep,tries,status,run_ops" > "$SUMMARY"

# Stand des Quellbaums mitprotokollieren -- fehlte in der Hauptkampagne.
{
  echo "commit: $(cd ~/spitfire && git rev-parse --short HEAD 2>/dev/null)"
  echo "tag:    $(cd ~/spitfire && git describe --tags --always 2>/dev/null)"
  echo "datum:  $(date -Is)"
  echo "host:   $(hostname)"
} > "$OUT/provenance.txt"
cat "$OUT/provenance.txt"
echo

total=$(( ${#MODES[@]} * ${#CARDINALITIES[@]} * REPS ))
done_n=0
failed_n=0
start_ts=$(date +%s)

echo "Ankerlaeufe: ${#CARDINALITIES[@]} Kardinalitaeten x ${#MODES[@]} Arme x $REPS Wdh. = $total Laeufe"
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
        echo "LOAD_INFO foreign ycsb: $(pgrep -a ycsb | grep -v $$ | wc -l)"
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
echo

# ---------------------------------------------------------------------------
# Sofortauswertung: Verhaeltnisse gegen die Hauptkampagne
# ---------------------------------------------------------------------------
if [ "$DRY" = "--dry" ]; then exit 0; fi
if ! command -v python3 > /dev/null; then
    echo "python3 nicht gefunden -- Auswertung manuell: $SUMMARY"
    exit 0
fi

python3 - "$SUMMARY" <<'PY'
import csv, statistics, sys

EXPECTED = {   # Hauptkampagne, Tabelle 7.3
    100000: {"A/off": 0.95, "B/A": 2.67, "B/off": 2.54},
    10000:  {"A/off": 1.13, "B/A": 2.59, "B/off": 2.92},
}
LEVEL = {100000: 2611, 10000: 2599}   # off-Arm der Hauptkampagne

vals = {}
with open(sys.argv[1]) as f:
    for row in csv.DictReader(f):
        if row["status"] != "OK" or not row["run_ops"]:
            continue
        try:
            v = float(row["run_ops"])
        except ValueError:
            continue
        vals.setdefault((int(row["cardinality"]), row["mode"]), []).append(v)

print("=" * 66)
print("ANKERPRUEFUNG")
print("=" * 66)

for card in sorted(EXPECTED, reverse=True):
    med = {}
    for mode in ("off", "A", "B"):
        s = vals.get((card, mode), [])
        if s:
            med[mode] = statistics.median(s) / 1000.0   # in Tausend ops/s
    if "off" not in med:
        print(f"\nK = {card}: kein off-Lauf -- keine Auswertung moeglich.")
        continue

    print(f"\nK = {card}")
    print(f"  Niveau: off = {med['off']:.0f} k  "
          f"(Hauptkampagne {LEVEL[card]} k, "
          f"Abweichung {100*(med['off']-LEVEL[card])/LEVEL[card]:+.1f} %)")
    print("  -> eine Abweichung hier ist erwartet und unschaedlich.\n")

    print(f"  {'Verhaeltnis':<10} {'jetzt':>8} {'Kap. 7':>8} {'Delta':>9}   Urteil")
    ratios = {}
    if "A" in med:
        ratios["A/off"] = med["A"] / med["off"]
    if "B" in med:
        ratios["B/off"] = med["B"] / med["off"]
    if "A" in med and "B" in med:
        ratios["B/A"] = med["B"] / med["A"]

    for name in ("A/off", "B/A", "B/off"):
        if name not in ratios:
            continue
        now, exp = ratios[name], EXPECTED[card][name]
        d = 100 * (now - exp) / exp
        verdict = "ok" if abs(d) < 4 else ("grenzwertig" if abs(d) < 8
                                           else "WIDERSPRUCH")
        print(f"  {name:<10} {now:>8.3f} {exp:>8.2f} {d:>8.1f} %   {verdict}")

print()
print("Lesart: stimmen die Verhaeltnisse trotz abweichendem Niveau, ist die")
print("Zusammenfuegung der Kurvenstuecke in Abschnitt 7.3 belegt. Weicht vor")
print("allem A/off deutlich ab, muss der Abschnitt zurueckgenommen werden.")
print("=" * 66)
PY
