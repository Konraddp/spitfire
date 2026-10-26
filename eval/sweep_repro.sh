#!/bin/bash
# ============================================================================
# sweep_repro.sh: Reproduktionskampagne, eine Gelegenheit, zwei Bloecke
# ============================================================================
# Zweck
# -----
# BLOCK A ersetzt Haupt- und Zusatzkampagne durch EINEN Durchgang ueber alle
# zehn Kardinalitaeten mit allen drei Armen. Damit stammt die gesamte Kurve
# aus einer Messgelegenheit, und die Annahme, dass Verhaeltnisse zwischen
# Gelegenheiten uebertragbar sind, wird nicht mehr gebraucht -- der
# schwerwiegendste methodische Einwand in Abschnitt 8.5 entfaellt.
#
# BLOCK B ist die dritte Schreibkampagne unter kontrollierter Last. Die
# beiden vorhandenen widersprechen sich um 15 % in der Kurvenform, weshalb
# Kapitel 7 dort keine Aussage macht.
#
# Zeitbedarf (5 min pro Lauf, gemessen)
# -------------------------------------
#   Block A: 10 Kardinalitaeten x 3 Arme x 3 Wdh. =  90 Laeufe ~  7,5 h
#   Block B:  6 Kardinalitaeten x 2 Arme x 3 Wdh. =  36 Laeufe ~  3,0 h
#   zusammen                                       126 Laeufe ~ 10,5 h
#   mit Wiederholungen nach sporadischen Abbruechen     ca. 13-14 h
#
# Wiederholungen sind die AEUSSERE Schleife. Bricht der Lauf nach zwei
# Dritteln ab, hast du zwei vollstaendige Wiederholungen von allem statt drei
# Wiederholungen von zwei Dritteln. Das erste ist auswertbar, das zweite nicht.
#
# Bereits vorhandene, erfolgreiche Logs werden uebersprungen, der Lauf ist
# also nach einem Abbruch fortsetzbar: einfach erneut starten.
#
# WICHTIG -- Stand des Quellbaums. Muss derselbe sein wie in beiden
# bisherigen Kampagnen:
#
#     cd ~/spitfire && git log --oneline -1       # muss 6b746cb sein
#     git checkout campaign-20260821 && cd build && make -j ycsb
#
# Aufruf:  nohup ./sweep_repro.sh > ~/repro_console.log 2>&1 &
#          tail -f ~/repro_console.log
#
# Nur Block A:  BLOCKS=A ./sweep_repro.sh
# Trockenlauf:  ./sweep_repro.sh --dry
# ============================================================================

set -u

BUILD=~/spitfire/build
OUT=${OUT:-~/results/repro_$(date +%Y%m%d_%H%M)}
NVM=/mnt/pmem1/pkonrad/spitfire_buf/
DB=~/spitfire_test/db/

SCALE=512
DRAM_PAGES=7480
NVM_PAGES=90000
WARMUP=60
DURATION=120
PROFILE=10

REPS=3
MAX_TRIES=4
BLOCKS=${BLOCKS:-AB}

# Block A: read-only, alle zehn Kardinalitaeten, drei Arme
A_CARDS=(10 100 1000 10000 20000 50000 100000 200000 300000 500000)
A_MODES=(off A B)
A_UPDATE=0

# Block B: Schreiblast, die sechs Kardinalitaeten der Schreibkampagne.
# Modus A entfaellt: seine Schreibvorgaenge werden nicht zuverlaessig
# persistiert (Abschnitt 5.6.4).
B_CARDS=(10 100 1000 10000 100000 500000)
B_MODES=(off B)
B_UPDATE=10

DRY=${1:-}

# ---------------------------------------------------------------------------
# Vorbedingungen
# ---------------------------------------------------------------------------
if pgrep -u "$USER" ycsb > /dev/null; then
    echo "FEHLER: es laeuft bereits ein ycsb-Prozess. Abbruch."
    exit 1
fi
if [ ! -x "$BUILD/ycsb" ]; then
    echo "FEHLER: $BUILD/ycsb nicht gefunden oder nicht ausfuehrbar."
    exit 1
fi

mkdir -p "$OUT"
SUMMARY="$OUT/summary.csv"
[ -f "$SUMMARY" ] || echo "block,mode,cardinality,update,rep,tries,status,run_ops" > "$SUMMARY"

{
  echo "commit:  $(cd ~/spitfire && git rev-parse --short HEAD 2>/dev/null)"
  echo "tag:     $(cd ~/spitfire && git describe --tags --always 2>/dev/null)"
  echo "start:   $(date -Is)"
  echo "host:    $(hostname)"
  echo "blocks:  $BLOCKS"
  echo "uptime:  $(uptime)"
} >> "$OUT/provenance.txt"
cat "$OUT/provenance.txt"
echo

run_once() {   # $1=mode $2=cardinality $3=logfile $4=update_ratio
    "$BUILD/ycsb" -k $SCALE -T $DRAM_PAGES -Y $NVM_PAGES -I -U $WARMUP -s \
        -B 1 -u "$4" -d $DURATION -p $PROFILE \
        -K "$2" -C "$1" -P "$NVM" -D "$DB" > "$3" 2>&1
}

# Gesamtzahl der Laeufe fuer die Fortschrittsanzeige
total=0
case "$BLOCKS" in
  *A*) total=$(( total + ${#A_CARDS[@]} * ${#A_MODES[@]} * REPS ));;
esac
case "$BLOCKS" in
  *B*) total=$(( total + ${#B_CARDS[@]} * ${#B_MODES[@]} * REPS ));;
esac

done_n=0
failed_n=0
skipped_n=0
start_ts=$(date +%s)

echo "Reproduktionskampagne: $total Laeufe, geschaetzt $(( total * 5 / 60 ))h$(( total * 5 % 60 ))m ohne Wiederholungen"
echo "Ausgabe: $OUT"
echo

do_cell() {   # $1=block $2=mode $3=card $4=update $5=rep
    local blk=$1 mode=$2 card=$3 upd=$4 rep=$5
    local log="$OUT/${blk}_K${card}_${mode}_r${rep}.log"
    done_n=$(( done_n + 1 ))

    if [ "$DRY" = "--dry" ]; then
        echo "[dry] $log"
        return
    fi

    # fortsetzbar: einen bereits gelungenen Lauf nicht wiederholen
    if [ -s "$log" ] && grep -q "run_ops\|throughput" "$log" 2>/dev/null; then
        skipped_n=$(( skipped_n + 1 ))
        printf "[%3d/%3d] %s K=%-6s %-3s r%d  uebersprungen (vorhanden)\n" \
               "$done_n" "$total" "$blk" "$card" "$mode" "$rep"
        return
    fi

    {
      echo "LOAD_INFO uptime: $(uptime)"
      echo "LOAD_INFO foreign ycsb: $(pgrep -a ycsb | grep -v $$ | wc -l)"
    } > "${log}.load"

    local status="FAILED" tries=0 t
    for t in $(seq 1 $MAX_TRIES); do
        tries=$t
        if run_once "$mode" "$card" "$log" "$upd"; then
            status="OK"
            break
        fi
    done

    local runv=""
    if [ "$status" = "OK" ]; then
        runv=$(tail -1 "$log" | awk '{print $(NF-2)}')
    else
        failed_n=$(( failed_n + 1 ))
        mv "$log" "${log}.failed" 2>/dev/null
    fi

    echo "$blk,$mode,$card,$upd,$rep,$tries,$status,$runv" >> "$SUMMARY"

    local el=$(( $(date +%s) - start_ts ))
    local eta=0
    [ "$done_n" -gt "$skipped_n" ] && \
        eta=$(( el * (total - done_n) / (done_n - skipped_n) / 60 ))
    printf "[%3d/%3d] %s K=%-6s %-3s r%d t%d %-6s run=%-12s  %dh%02dm  ETA %dh%02dm\n" \
           "$done_n" "$total" "$blk" "$card" "$mode" "$rep" "$tries" "$status" \
           "${runv:--}" $(( el / 3600 )) $(( el % 3600 / 60 )) \
           $(( eta / 60 )) $(( eta % 60 ))
}

# ---------------------------------------------------------------------------
# Wiederholung als aeussere Schleife
# ---------------------------------------------------------------------------
for rep in $(seq 1 $REPS); do
  echo
  echo "===== Wiederholung $rep von $REPS  ($(date +%H:%M)) ====="

  case "$BLOCKS" in *A*)
    for card in "${A_CARDS[@]}"; do
      for mode in "${A_MODES[@]}"; do
        do_cell A "$mode" "$card" "$A_UPDATE" "$rep"
      done
    done
  ;; esac

  case "$BLOCKS" in *B*)
    for card in "${B_CARDS[@]}"; do
      for mode in "${B_MODES[@]}"; do
        do_cell B "$mode" "$card" "$B_UPDATE" "$rep"
      done
    done
  ;; esac
done

echo
echo "Fertig um $(date). $failed_n von $total Laeufen endgueltig gescheitert, $skipped_n uebersprungen."
echo "end: $(date -Is)" >> "$OUT/provenance.txt"
echo

# ---------------------------------------------------------------------------
# Sofortauswertung gegen die berichteten Werte
# ---------------------------------------------------------------------------
if [ "$DRY" = "--dry" ]; then exit 0; fi
command -v python3 > /dev/null || { echo "python3 fehlt, Auswertung: $SUMMARY"; exit 0; }

python3 - "$SUMMARY" <<'PY'
import csv, statistics, sys

# Verhaeltnisse aus Tabelle 7.3 bzw. Tabelle 7.6
READ = {
    10:     {"A/off": 1.32, "B/A": 2.51, "B/off": 3.30},
    100:    {"A/off": 1.31, "B/A": 2.51, "B/off": 3.29},
    1000:   {"A/off": 1.19, "B/A": 2.58, "B/off": 3.08},
    10000:  {"A/off": 1.13, "B/A": 2.59, "B/off": 2.92},
    20000:  {"A/off": 1.02},
    50000:  {"A/off": 0.92},
    100000: {"A/off": 0.95, "B/A": 2.67, "B/off": 2.54},
    200000: {"A/off": 1.02},
    300000: {"A/off": 1.05},
    500000: {"A/off": 1.01, "B/A": 2.47, "B/off": 2.49},
}
WRITE_RANGE = (1.24, 1.49)   # B/off, beide bisherigen Schreibkampagnen

vals = {}
with open(sys.argv[1]) as f:
    for r in csv.DictReader(f):
        if r["status"] != "OK" or not r["run_ops"]:
            continue
        try:
            v = float(r["run_ops"])
        except ValueError:
            continue
        vals.setdefault((r["block"], int(r["cardinality"]), r["mode"]), []).append(v)

def med(blk, card, mode):
    s = vals.get((blk, card, mode), [])
    return statistics.median(s) / 1000.0 if s else None

print("=" * 74)
print("BLOCK A -- read-only, alle Punkte aus EINER Gelegenheit")
print("=" * 74)
print(f"{'K':>8} {'off':>6} {'A':>6} {'B':>6} |"
      f"{'A/off (Kap.7)':>20}{'B/A (Kap.7)':>20}{'B/off (Kap.7)':>20}")
for card in sorted(READ):
    o, a, b = med("A", card, "off"), med("A", card, "A"), med("A", card, "B")
    if o is None:
        continue
    row = f"{card:>8} {o:>6.0f} {a if a else 0:>6.0f} {b if b else 0:>6.0f} |"
    for name, num, den in (("A/off", a, o), ("B/A", b, a), ("B/off", b, o)):
        if num and den:
            now = num / den
            exp = READ[card].get(name)
            if exp:
                row += f"{now:>8.2f} [{exp:.2f} {100*(now-exp)/exp:+4.0f}%]"
            else:
                row += f"{now:>8.2f}            "
        else:
            row += " " * 20
    print(row)

print()
print("Lesart: die Spalte A/off muss das Band wiedergeben -- ueber 1 bei")
print("kleinen K, Minimum nahe 50.000, Rueckkehr ueber 1 ab 200.000. Die")
print("Absolutwerte duerfen abweichen, die Form nicht.")

wrows = [(c, med("B", c, "off"), med("B", c, "B")) for c in sorted(set(
         k[1] for k in vals if k[0] == "B"))]
if wrows:
    print()
    print("=" * 74)
    print("BLOCK B -- 10 % Schreiblast")
    print("=" * 74)
    print(f"{'K':>8} {'off':>7} {'B':>7} {'B/off':>8}   Urteil")
    for c, o, b in wrows:
        if not (o and b):
            continue
        r = b / o
        verdict = ("im Bereich der bisherigen Kampagnen"
                   if WRITE_RANGE[0] - 0.05 <= r <= WRITE_RANGE[1] + 0.05
                   else "AUSSERHALB 1.24-1.49")
        print(f"{c:>8} {o:>7.0f} {b:>7.0f} {r:>8.2f}   {verdict}")
    print()
    print("Diese Kampagne lief unter kontrollierter Last. Reproduziert sie eine")
    print("der beiden bisherigen Kurvenformen, kann Abschnitt 7.5.2 eine Aussage")
    print("ueber die Kardinalitaetsabhaengigkeit machen statt keine.")
print("=" * 74)
PY
