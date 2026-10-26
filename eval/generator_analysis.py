#!/usr/bin/env python3
"""
Offline-Rekonstruktion des YCSB-Wertegenerators.

Belegt zwei Aussagen der Arbeit ohne Maschinenzeit:

  1. Die beobachtete Kardinalitaet von ~428.656 bei angeforderten 500.000
     entsteht durch den 32-Bit-Ueberlauf in der Multiplikation, nicht durch
     zufaellige Kollisionen. Ohne Wrap waeren es exakt 500.000.

  2. Die Wertverteilung bei 500.000 ist nahezu gleichverteilt. Die
     Wiederverwendung der Woerterbucheintraege sinkt gegenueber 100.000 um
     den Faktor vier. Eine verbesserte Cache-Lokalitaet scheidet damit als
     Erklaerung fuer die Anomalie in A/off aus.

Quelle der Formel: src/benchmark/ycsb/ycsb_loader.cpp, Zeile 158
    uint32_t v = (uint32_t)((rowid * 2654435761u + i * 40503u) % cardinality);
Die Multiplikation laeuft in 32 Bit, weil 2654435761u unsigned int ist.
"""

from collections import Counter

N_ROWS = 512_000          # -k 512
N_COLS = 10               # COLUMN_COUNT
A = 2654435761
B = 40503
M32 = 0xFFFFFFFF


def values(K, col, wrap32=True):
    """Spaltenwerte aller Zeilen. wrap32=False = hypothetische 64-Bit-Variante."""
    if wrap32:
        return (((r * A + col * B) & M32) % K for r in range(N_ROWS))
    return ((r * A + col * B) % K for r in range(N_ROWS))


def stats(freqs):
    f = sorted(freqs)
    n, tot = len(f), sum(f)
    reuse = sum((x / tot) ** 2 for x in f)
    gini = 2 * sum((k + 1) * x for k, x in enumerate(f)) / (n * tot) - (n + 1) / n
    return n, f[0], f[-1], reuse, gini


print("Teil 1 -- 32 Bit gegen 64 Bit, Spalte 0\n")
print(f"{'K':>8} {'32 Bit':>9} {'64 Bit':>9}")
for K in (10, 100, 1_000, 10_000, 100_000, 500_000):
    a = len(Counter(values(K, 0, True)))
    b = len(Counter(values(K, 0, False)))
    print(f"{K:>8} {a:>9} {b:>9}")

print("\n\nTeil 2 -- beobachtete Kardinalitaet je Spalte bei K = 500.000\n")
per_col = [len(Counter(values(500_000, c))) for c in range(N_COLS)]
for c, d in enumerate(per_col):
    print(f"  Spalte {c}: {d}")
print(f"  min {min(per_col)}  max {max(per_col)}  Mittel {sum(per_col)/N_COLS:.1f}")

print("\n\nTeil 3 -- Verteilungsmasse ueber die Kampagnenkardinalitaeten\n")
print(f"{'K':>8} {'distinct':>9} {'f_min':>6} {'f_max':>6} {'Reuse':>11} {'Gini':>7}")
for K in (10, 100, 1_000, 10_000, 100_000, 500_000):
    n, lo, hi, reuse, gini = stats(Counter(values(K, 0)).values())
    print(f"{K:>8} {n:>9} {lo:>6} {hi:>6} {reuse:>11.3e} {gini:>7.3f}")

print("\nReuse = Wahrscheinlichkeit, dass zwei unabhaengige Zugriffe denselben")
print("Woerterbucheintrag aufloesen. Faellt von 100.000 auf 500.000 um Faktor 4.")
