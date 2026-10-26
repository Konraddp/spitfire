#!/usr/bin/env python3
"""
Regenerates the two throughput figures from the medians reported in
Tables 7.2 (tab:res:absolute) and 7.3 (tab:res:relative).

The figures are drawn from the aggregated medians, not from the raw logs, so
they agree with the tables by construction.

Output: absolute.pdf / speedup_curve.pdf (vector, for LaTeX) plus PNG previews.
"""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ---- data: medians of three repetitions, thousands of operations per second --
K      = [10, 100, 1000, 10000, 100000, 428656]
off    = [2603, 2600, 2610, 2599, 2611, 2610]
modeA  = [3433, 3408, 3116, 2925, 2483, 2634]
modeB  = [8602, 8556, 8043, 7581, 6619, 6501]

r_Aoff = [1.32, 1.31, 1.19, 1.13, 0.95, 1.01]
r_BA   = [2.51, 2.51, 2.58, 2.59, 2.67, 2.47]
r_Boff = [3.30, 3.29, 3.08, 2.92, 2.54, 2.49]

# ---- style ------------------------------------------------------------------
C_OFF, C_A, C_B = "#0072B2", "#D55E00", "#009E73"   # validated, CVD-safe
plt.rcParams.update({
    "font.family": "serif",
    "font.size": 9,
    "axes.labelsize": 9,
    "axes.titlesize": 9,
    "legend.fontsize": 8,
    "xtick.labelsize": 8,
    "ytick.labelsize": 8,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "axes.grid": True,
    "grid.color": "#dddddd",
    "grid.linewidth": 0.6,
    "figure.dpi": 150,
})

SERIES = [
    ("Uncompressed baseline (off)", off, C_OFF, "o", "-"),
    ("Full decoding (Mode A)",      modeA, C_A, "s", "--"),
    ("Selective decoding (Mode B)", modeB, C_B, "^", "-."),
]

# ---- figure 1: absolute throughput -----------------------------------------
fig, ax = plt.subplots(figsize=(5.2, 3.1))
for label, y, c, m, ls in SERIES:
    ax.plot(K, y, color=c, marker=m, linestyle=ls, linewidth=1.6,
            markersize=5, markeredgecolor="white", markeredgewidth=0.6,
            label=label, zorder=3)
ax.set_xscale("log")
ax.set_xlabel("Distinct values per column (observed $K$, log scale)")
ax.set_ylabel("Throughput [$10^3$ ops/s]")
ax.set_ylim(0, 9600)
ax.set_xlim(7, 7e5)
ax.legend(frameon=False, loc="lower left", handlelength=2.6)
fig.tight_layout()
fig.savefig("/home/claude/absolute.pdf")
fig.savefig("/home/claude/absolute.png")
plt.close(fig)

# ---- figure 2: ratios -------------------------------------------------------
fig, ax = plt.subplots(figsize=(5.2, 3.1))
ax.axhline(1.0, color="#777777", linewidth=1.0, linestyle=":", zorder=1)
ax.annotate("parity", xy=(7.6, 1.0), xytext=(7.6, 1.09),
            color="#777777", fontsize=7.5)

ax.plot(K, r_Boff, color=C_B, marker="^", linestyle="-.", linewidth=1.4,
        markersize=5, markeredgecolor="white", markeredgewidth=0.6,
        alpha=0.85, label=r"B/off", zorder=2)
ax.plot(K, r_BA, color="#555555", marker="D", linestyle="--", linewidth=1.4,
        markersize=4.2, markeredgecolor="white", markeredgewidth=0.6,
        alpha=0.85, label=r"B/A", zorder=2)
ax.plot(K, r_Aoff, color=C_A, marker="s", linestyle="-", linewidth=2.2,
        markersize=6, markeredgecolor="white", markeredgewidth=0.8,
        label=r"A/off", zorder=4)

# direct labels, placed where the curves are well separated
ax.annotate("B/off", xy=(1000, r_Boff[2]), xytext=(0, 8),
            textcoords="offset points", color=C_B, fontsize=8,
            ha="center", va="bottom")
ax.annotate("B/A", xy=(1000, r_BA[2]), xytext=(0, -9),
            textcoords="offset points", color="#555555", fontsize=8,
            ha="center", va="top")
ax.annotate("A/off", xy=(K[-1], r_Aoff[-1]), xytext=(7, 0),
            textcoords="offset points", color=C_A, fontsize=8, va="center")

ax.set_xscale("log")
ax.set_xlabel("Distinct values per column (observed $K$, log scale)")
ax.set_ylabel("Throughput ratio")
ax.set_ylim(0.7, 3.6)
ax.set_xlim(7, 1.3e6)
fig.tight_layout()
fig.savefig("/home/claude/speedup_curve.pdf")
fig.savefig("/home/claude/speedup_curve.png")
plt.close(fig)

print("written: absolute.pdf/.png, speedup_curve.pdf/.png")
