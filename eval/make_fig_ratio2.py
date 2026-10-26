#!/usr/bin/env python3
"""
speedup_curve.pdf, now with the reproduction campaign of 18 October.

Lower panel  A/off, original (two campaigns stitched) against the
             reproduction (ten cardinalities, one occasion).
Upper panel  B/off and B/A, same pairing. The reproduction supplies B at
             the four cardinalities the supplementary campaign could not.

Filled markers: as reported in Chapter 7.  Open markers: reproduction.
"""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

K_rep = [10, 100, 1000, 10000, 20000, 50000, 100000, 200000, 300000, 428656]
K_org6 = [10, 100, 1000, 10000, 100000, 428656]

# ---- as reported in Chapter 7 ----------------------------------------------
org_Aoff = [1.32, 1.31, 1.19, 1.13, 1.02, 0.92, 0.95, 1.02, 1.05, 1.01]
org_Boff = [3.30, 3.29, 3.08, 2.92, 2.54, 2.49]
org_BA   = [2.51, 2.51, 2.58, 2.59, 2.67, 2.47]

# ---- reproduction, 18 October, medians of three ----------------------------
rep_Aoff = [1.36, 1.37, 1.26, 1.17, 1.02, 0.93, 1.03, 1.04, 1.07, 1.09]
rep_Boff = [3.59, 3.58, 3.36, 3.20, 3.09, 2.79, 2.80, 2.79, 2.76, 2.75]
rep_BA   = [2.64, 2.61, 2.67, 2.75, 3.02, 2.99, 2.71, 2.69, 2.59, 2.52]

C_A, C_B, C_BA = "#D55E00", "#009E73", "#555555"

plt.rcParams.update({
    "font.family": "serif", "font.size": 9,
    "axes.labelsize": 9, "legend.fontsize": 7.5,
    "xtick.labelsize": 8, "ytick.labelsize": 8,
    "axes.spines.top": False, "axes.spines.right": False,
    "axes.grid": True, "grid.color": "#dddddd", "grid.linewidth": 0.6,
    "figure.dpi": 150,
})

fig, (ax_t, ax_b) = plt.subplots(
    2, 1, figsize=(5.4, 4.8), sharex=True,
    gridspec_kw={"height_ratios": [1.0, 1.15], "hspace": 0.12})

# ============ upper panel: the selective ratios =============================
ax_t.plot(K_org6, org_Boff, color=C_B, marker="^", linestyle="-.",
          linewidth=1.3, markersize=5, markeredgecolor="white",
          markeredgewidth=0.6, zorder=3)
ax_t.plot(K_rep, rep_Boff, color=C_B, marker="^", linestyle="-",
          linewidth=1.6, markersize=5.5, markerfacecolor="white",
          markeredgewidth=1.3, zorder=4)

ax_t.plot(K_org6, org_BA, color=C_BA, marker="D", linestyle="--",
          linewidth=1.3, markersize=4, markeredgecolor="white",
          markeredgewidth=0.6, zorder=3)
ax_t.plot(K_rep, rep_BA, color=C_BA, marker="D", linestyle="-",
          linewidth=1.6, markersize=4.5, markerfacecolor="white",
          markeredgewidth=1.3, zorder=4)

ax_t.annotate("B/off", xy=(300, 3.30), color=C_B, fontsize=8.5, ha="center")
ax_t.annotate("B/A", xy=(300, 2.47), color=C_BA, fontsize=8.5, ha="center")
ax_t.set_ylim(2.3, 3.8)
ax_t.set_yticks([2.4, 2.8, 3.2, 3.6])
ax_t.set_ylabel("Ratio")

# ============ lower panel: compression against the baseline =================
ax_b.axvspan(2.5e4, 8.0e4, color=C_A, alpha=0.09, zorder=0, linewidth=0)
ax_b.axhline(1.0, color="#777777", linewidth=1.0, linestyle=":", zorder=1)
ax_b.annotate("parity", xy=(8.5, 1.0), xytext=(0, 3),
              textcoords="offset points", color="#777777", fontsize=7.5)

ax_b.plot(K_rep, org_Aoff, color=C_A, marker="s", linestyle="-.",
          linewidth=1.4, markersize=5, markeredgecolor="white",
          markeredgewidth=0.6, label="as reported, two campaigns", zorder=3)
ax_b.plot(K_rep, rep_Aoff, color=C_A, marker="o", linestyle="-",
          linewidth=2.2, markersize=6, markerfacecolor="white",
          markeredgewidth=1.6, label="reproduction, one occasion", zorder=5)

ax_b.annotate("A/off", xy=(10, 1.36), xytext=(4, 9),
              textcoords="offset points", color=C_A, fontsize=8.5)
ax_b.annotate("net loss", xy=(4.5e4, 1.42), color=C_A, fontsize=7.5,
              ha="center", va="center")

ax_b.set_ylim(0.88, 1.47)
ax_b.set_yticks([0.9, 1.0, 1.1, 1.2, 1.3, 1.4])
ax_b.set_ylabel("Ratio")
ax_b.set_xscale("log")
ax_b.set_xlim(7, 1.3e6)
ax_b.set_xlabel("Distinct values per column (observed $K$, log scale)")
ax_b.legend(frameon=False, loc="lower left", handlelength=2.2,
            numpoints=1, borderaxespad=0.6)

fig.subplots_adjust(left=0.115, right=0.975, top=0.97, bottom=0.105)
fig.savefig("/home/claude/speedup_curve.pdf")
fig.savefig("/home/claude/speedup_curve.png")
print("written")
