#!/usr/bin/env python3
"""
analyze_threads.py — wertet den Thread-Sweep aus den Einzellogs aus.

    python3 analyze_threads.py ~/results/threads_20260909_0043

Das Thread-Verzeichnis enthaelt keine summary.csv (die schreibt nur sweep.sh),
deshalb liest dieses Skript die Logs direkt. Die Durchsatzzahl wird wie in
sweep.sh Zeile 86 aus der Ergebniszeile geholt: das Feld NF-2 der Zeile mit
'::', also run_ops. NF-3 waere warmup_ops.

Schreibt zusaetzlich summary.csv ins Sweep-Verzeichnis, im selben Format wie
sweep.sh, damit die Daten spaeter mit demselben Werkzeug behandelt werden
koennen wie die Hauptkampagne.
"""
import sys, os, re, csv
from collections import defaultdict
import statistics as st

MODES = ["off", "A", "B"]
NAME = re.compile(r"^t(\d+)_(off|A|B)_r(\d+)\.log$")


def parse_log(path):
    """(run_ops, warmup_ops, tries) aus einem Lauf-Log."""
    ops = warm = None
    tries = 1
    with open(path, errors="replace") as f:
        for line in f:
            if "::" in line:
                p = line.split()
                if len(p) >= 4:
                    try:
                        warm, ops = float(p[-4]), float(p[-3])
                    except ValueError:
                        pass
            elif line.startswith("tries="):
                m = re.match(r"tries=(\d+)", line)
                if m:
                    tries = int(m.group(1))
    return ops, warm, tries


def quartiles(v):
    s = sorted(v)
    n = len(s)
    med = st.median(s)
    if n < 4:
        return med, s[0], s[-1]
    return med, st.median(s[: n // 2]), st.median(s[(n + 1) // 2:])


def main(d):
    runs = []
    for fn in sorted(os.listdir(d)):
        m = NAME.match(fn)
        if not m:
            continue
        t, mode, rep = int(m.group(1)), m.group(2), int(m.group(3))
        ops, warm, tries = parse_log(os.path.join(d, fn))
        if ops is None:
            print(f"WARNUNG: keine Ergebniszeile in {fn}")
            continue
        runs.append({"threads": t, "mode": mode, "rep": rep,
                     "tries": tries, "ops": ops, "warm": warm})

    if not runs:
        sys.exit("keine auswertbaren Logs gefunden")

    out = os.path.join(d, "summary.csv")
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["mode", "threads", "rep", "tries", "status",
                    "warmup_ops", "run_ops"])
        for r in sorted(runs, key=lambda r: (r["threads"], r["mode"], r["rep"])):
            w.writerow([r["mode"], r["threads"], r["rep"], r["tries"],
                        "OK", f"{r['warm']:.6f}", f"{r['ops']:.6f}"])
    print(f"summary.csv geschrieben ({len(runs)} Laeufe)\n")

    data = defaultdict(list)
    for r in runs:
        data[(r["mode"], r["threads"])].append(r["ops"])
    threads = sorted({r["threads"] for r in runs})

    med = defaultdict(dict)
    print("Durchsatz (Median ueber Wiederholungen, ops/s)\n")
    print(f"{'thr':>4} | " + " | ".join(f"{m:>22}" for m in MODES)
          + " |   B/A |  B/off |  A/off")
    print("-" * 104)
    for t in threads:
        cells = []
        for m in MODES:
            v = data.get((m, t), [])
            if v:
                mv, q1, q3 = quartiles(v)
                med[m][t] = mv
                cells.append(f"{mv:>13,.0f} IQR{(q3-q1)/mv*100:4.1f}%")
            else:
                cells.append(f"{'no data':>22}")
        r = []
        for num, den in (("B", "A"), ("B", "off"), ("A", "off")):
            if t in med.get(num, {}) and t in med.get(den, {}) and med[den][t]:
                r.append(f"{med[num][t]/med[den][t]:5.2f}x")
            else:
                r.append("     ")
        print(f"{t:>4} | " + " | ".join(cells) + " | " + " | ".join(r))

    # Skalierung relativ zu einem Worker: sagt, ob der Arm ueberhaupt skaliert.
    base = threads[0]
    print(f"\nSkalierung relativ zu {base} Worker(n) "
          f"(ideal waere der Faktor der Workerzahl)\n")
    print(f"{'thr':>4} | " + " | ".join(f"{m:>10}" for m in MODES) + " | ideal")
    print("-" * 52)
    for t in threads:
        cells = []
        for m in MODES:
            if t in med.get(m, {}) and base in med.get(m, {}):
                cells.append(f"{med[m][t]/med[m][base]:9.2f}x")
            else:
                cells.append(f"{'--':>10}")
        print(f"{t:>4} | " + " | ".join(cells) + f" | {t/base:5.2f}x")

    # Abstuerze nach Workerzahl: die eigentliche Nebenbeobachtung des Sweeps.
    print("\nWiederholte Laeufe (tries > 1):")
    any_r = False
    for r in sorted(runs, key=lambda r: (r["threads"], r["mode"], r["rep"])):
        if r["tries"] > 1:
            print(f"   threads={r['threads']:<3} mode={r['mode']:<3} "
                  f"rep={r['rep']} tries={r['tries']}")
            any_r = True
    if not any_r:
        print("   keine")
    else:
        per_t = defaultdict(int)
        for r in runs:
            if r["tries"] > 1:
                per_t[r["threads"]] += 1
        print("   je Workerzahl: " +
              ", ".join(f"{t}: {per_t.get(t, 0)}" for t in threads))

    # Retry-Confound wie in der Hauptkampagne, hier je Workerzahl.
    print("\nRetry-Confound innerhalb einer Workerzahl (nur Mode A):")
    rows = []
    for t in threads:
        f1 = [r["ops"] for r in runs
              if r["mode"] == "A" and r["threads"] == t and r["tries"] == 1]
        fr = [r["ops"] for r in runs
              if r["mode"] == "A" and r["threads"] == t and r["tries"] > 1]
        if f1 and fr:
            a, b = st.median(f1), st.median(fr)
            rows.append((t, a, len(f1), b, len(fr), (b - a) / a * 100))
    if not rows:
        print("   keine Workerzahl hat beide Gruppen")
    else:
        for t, a, na, b, nb, dl in rows:
            print(f"   threads={t:<3} tries=1: {a:>12,.0f} (n={na})   "
                  f"tries>1: {b:>12,.0f} (n={nb})   delta={dl:+6.1f}%")
        print(f"   groesste |delta|: "
              f"{max(rows, key=lambda r: abs(r[5]))[5]:+.1f}%")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: analyze_threads.py <sweep-verzeichnis>")
    main(sys.argv[1])