#!/usr/bin/env python3
"""
analyze.py (v3) — turns the sweep's summary.csv into the thesis figures.

    python3 analyze.py ~/results/sweep_YYYYMMDD_HHMM/summary.csv

Changes over v2:
  - the retry confound check is now also broken down BY CARDINALITY. The
    pooled figure mixes cardinalities that have different intrinsic
    throughput, and since crashes cluster at the low cardinalities where
    Mode A is fastest, the pooled delta is an aggregation artefact. The
    per-cardinality figure is the one the thesis reports.
  - fixed a length mismatch in the speedup plot: the x list was built from
    the cardinalities present for A and B, but the y lists were filtered
    again against `off`, so a missing baseline run made matplotlib raise.
    Each curve now builds its own x list.

Failed runs are treated as MISSING, never as zero.
"""
import sys, csv
from collections import defaultdict
import statistics as st

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    HAVE_PLT = True
except ImportError:
    HAVE_PLT = False

MODES = ["off", "A", "B"]


def quartiles(v):
    """median, q1, q3 — works for small n without numpy."""
    s = sorted(v)
    n = len(s)
    med = st.median(s)
    if n < 4:
        return med, s[0], s[-1]
    lo = s[: n // 2]
    hi = s[(n + 1) // 2:]
    return med, st.median(lo), st.median(hi)


def main(path):
    runs = []                      # every OK row as a dict
    fails = defaultdict(int)
    with open(path) as f:
        for row in csv.DictReader(f):
            key = (row["mode"], int(row["cardinality"]))
            if row["status"] == "OK" and row["run_ops"]:
                runs.append({
                    "mode": row["mode"],
                    "card": int(row["cardinality"]),
                    "rep": int(row["rep"]),
                    "tries": int(row["tries"]),
                    "ops": float(row["run_ops"]),
                })
            else:
                fails[key] += 1

    data = defaultdict(list)
    for r in runs:
        data[(r["mode"], r["card"])].append(r["ops"])

    cards = sorted({c for (_, c) in data})

    # ---- main table --------------------------------------------------------
    print("Median throughput (ops/s), IQR as robust spread\n")
    print(f"{'card':>8} | " + " | ".join(f"{m:>24}" for m in MODES) + " |   B/A |  B/off |  A/off")
    print("-" * 120)
    med = defaultdict(dict)
    for c in cards:
        cells = []
        for m in MODES:
            v = data.get((m, c), [])
            if v:
                mv, q1, q3 = quartiles(v)
                med[m][c] = mv
                spread = (q3 - q1) / mv * 100 if mv else 0
                cells.append(f"{mv:>13,.0f} IQR{spread:5.1f}% n={len(v)}")
            else:
                cells.append(f"{'no data':>24}")
        ba = bo = ao = ""
        if c in med.get("A", {}) and c in med.get("B", {}) and med["A"][c]:
            ba = f"{med['B'][c]/med['A'][c]:5.2f}x"
        if c in med.get("off", {}) and c in med.get("B", {}) and med["off"][c]:
            bo = f"{med['B'][c]/med['off'][c]:5.2f}x"
        if c in med.get("off", {}) and c in med.get("A", {}) and med["off"][c]:
            ao = f"{med['A'][c]/med['off'][c]:5.2f}x"
        print(f"{c:>8} | " + " | ".join(cells) + f" | {ba} | {bo} | {ao}")

    # ---- outliers (1.5 * IQR rule) ----------------------------------------
    print("\nOutliers (1.5x IQR rule):")
    any_out = False
    for (m, c), v in sorted(data.items()):
        if len(v) < 4:
            continue
        mv, q1, q3 = quartiles(v)
        iqr = q3 - q1
        lo, hi = q1 - 1.5 * iqr, q3 + 1.5 * iqr
        for r in runs:
            if r["mode"] == m and r["card"] == c and not (lo <= r["ops"] <= hi):
                print(f"   mode={m:<3} card={c:<7} rep={r['rep']} "
                      f"tries={r['tries']} ops={r['ops']:>12,.0f}")
                any_out = True
    if not any_out:
        print("   none")

    # ---- confound check: do retried runs measure slower? -------------------
    print("\nRetry confound check (does a run after crashes measure differently?):")
    for m in MODES:
        first = [r["ops"] for r in runs if r["mode"] == m and r["tries"] == 1]
        retried = [r["ops"] for r in runs if r["mode"] == m and r["tries"] > 1]

        if not retried:
            if first:
                print(f"   mode={m:<3} all runs succeeded on first try "
                      f"(n={len(first)})")
            continue

        fm, rm = st.median(first), st.median(retried)
        print(f"   mode={m:<3} POOLED over all cardinalities — "
              f"tries=1: {fm:>12,.0f} (n={len(first):>2})   "
              f"tries>1: {rm:>12,.0f} (n={len(retried):>2})   "
              f"delta={(rm - fm) / fm * 100:+6.1f}%")

        # Same comparison inside each cardinality. This is the figure that
        # matters: the pooled delta above is confounded because crashes are
        # not evenly distributed over cardinalities.
        rows = []
        for c in cards:
            f = [r["ops"] for r in runs
                 if r["mode"] == m and r["card"] == c and r["tries"] == 1]
            q = [r["ops"] for r in runs
                 if r["mode"] == m and r["card"] == c and r["tries"] > 1]
            if f and q:
                cf, cr = st.median(f), st.median(q)
                rows.append((c, cf, len(f), cr, len(q), (cr - cf) / cf * 100))

        if not rows:
            print(f"        within cardinality: no cardinality has runs in "
                  f"both groups — comparison not possible")
            continue

        print(f"        within cardinality:")
        for c, cf, nf, cr, nr, d in rows:
            print(f"          card={c:<7} tries=1: {cf:>12,.0f} (n={nf})   "
                  f"tries>1: {cr:>12,.0f} (n={nr})   delta={d:+6.1f}%")
        worst = max(rows, key=lambda r: abs(r[5]))
        print(f"        -> largest |delta| within a cardinality: "
              f"{worst[5]:+.1f}% at K={worst[0]}; "
              f"{len(rows)} of {len(cards)} cardinalities comparable")
        if min(r[2] for r in rows) < 2 or min(r[4] for r in rows) < 2:
            print(f"        note: at least one group has n=1, so the "
                  f"'median' there is a single run")

    nfail = sum(fails.values())
    if nfail:
        print(f"\n{nfail} runs failed permanently:")
        for k, n in sorted(fails.items()):
            print(f"   mode={k[0]:<3} card={k[1]:<7} {n} failed")

    if not HAVE_PLT:
        print("\n(matplotlib not available — no plots written)")
        return

    # ---- speedup curve -----------------------------------------------------
    # Each curve builds its own x list, so a missing run in one mode does not
    # desynchronise the others.
    def ratio_curve(num, den):
        xs = [c for c in cards
              if c in med.get(num, {}) and c in med.get(den, {}) and med[den][c]]
        return xs, [med[num][c] / med[den][c] for c in xs]

    curves = [
        ("B", "A", "o-", "#7F77DD", 2.2, "Mode B / Mode A"),
        ("B", "off", "s--", "#26A69A", 1.8, "Mode B / vanilla"),
        ("A", "off", "^-", "#E57373", 2.0, "Mode A / vanilla"),
    ]
    if any(ratio_curve(n, d)[0] for n, d, *_ in curves):
        plt.figure(figsize=(7, 4.5))
        plt.axhline(1.0, color="#999999", linestyle="--", linewidth=1.2,
                    label="break-even")
        for num, den, fmt, col, lw, lab in curves:
            xs, ys = ratio_curve(num, den)
            if xs:
                plt.plot(xs, ys, fmt, color=col, linewidth=lw, label=lab)
        plt.xscale("log")
        plt.ylim(bottom=0)
        plt.xlabel("cardinality (distinct values per column)")
        plt.ylabel("throughput ratio")
        plt.title("Selective decoding vs. full decompression")
        plt.grid(alpha=.3)
        plt.legend()
        plt.tight_layout()
        plt.savefig("speedup_curve.png", dpi=150)
        print("\nwrote speedup_curve.png")

    # ---- absolute throughput with IQR error bars ---------------------------
    plt.figure(figsize=(7, 4.5))
    for m, col in zip(MODES, ["#9E9E9E", "#E57373", "#66BB6A"]):
        xs = [c for c in cards if c in med.get(m, {})]
        if not xs:
            continue
        ys, errlo, errhi = [], [], []
        for c in xs:
            mv, q1, q3 = quartiles(data[(m, c)])
            ys.append(mv); errlo.append(mv - q1); errhi.append(q3 - mv)
        plt.errorbar(xs, ys, yerr=[errlo, errhi], fmt="o-", color=col,
                     label=m, linewidth=2, capsize=3)
    plt.xscale("log")
    plt.ylim(bottom=0)
    plt.xlabel("cardinality (distinct values per column)")
    plt.ylabel("throughput (ops/s)")
    plt.title("Absolute throughput per mode (median, IQR)")
    plt.grid(alpha=.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig("absolute.png", dpi=150)
    print("wrote absolute.png")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: analyze.py <summary.csv>")
    main(sys.argv[1])