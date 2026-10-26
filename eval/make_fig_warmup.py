#!/usr/bin/env python3
"""
Figure for Section 6.4.1: the warm-up artefact.

Both series are Mode A at a cardinality of 100. The old configuration
(10 s warm-up, 20 s window, 5 s intervals) is still rising when its window
closes; the corrected one (60 s warm-up, 120 s window, 10 s intervals) is
stationary. The dashed line is the aggregate the old configuration reported.

Data: thesis Table 6.x for the old run, campaign log K100_A_r1.log for the new.
"""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ---- data (thousand operations per second) ---------------------------------
old_t = [10, 15, 20]
old_v = [415, 1429, 3286]
old_aggregate = 1013

new_t = list(range(20, 130, 10))
new_v = [3427.5, 3437.2, 3439.1, 3449.6, 3437.7, 3441.5,
         3446.9, 3451.4, 3437.8, 3450.7, 3450.9]
new_aggregate = 3406

C_OLD, C_NEW = "#D55E00", "#0072B2"

plt.rcParams.update({
    "font.family": "serif", "font.size": 9,
    "axes.labelsize": 9, "legend.fontsize": 8,
    "xtick.labelsize": 8, "ytick.labelsize": 8,
    "axes.spines.top": False, "axes.spines.right": False,
    "axes.grid": True, "grid.color": "#dddddd", "grid.linewidth": 0.6,
    "figure.dpi": 150,
})

fig, ax = plt.subplots(figsize=(5.4, 3.0))

ax.plot(old_t, old_v, color=C_OLD, marker="s", linestyle="-", linewidth=1.8,
        markersize=5.5, markeredgecolor="white", markeredgewidth=0.6,
        label=r"10 s warm-up, 20 s window", zorder=4)
ax.plot(new_t, new_v, color=C_NEW, marker="o", linestyle="-", linewidth=1.6,
        markersize=4.5, markeredgecolor="white", markeredgewidth=0.6,
        label=r"60 s warm-up, 120 s window", zorder=3)

ax.axhline(old_aggregate, color=C_OLD, linestyle=":", linewidth=1.2, zorder=2)
ax.annotate("aggregate reported: 1013",
            xy=(66, old_aggregate), xytext=(0, 6), textcoords="offset points",
            color=C_OLD, fontsize=7.5, ha="center")

ax.set_xlabel("Time within the measurement window [s]")
ax.set_ylabel("Throughput [$10^3$ ops/s]")
ax.set_xlim(0, 128)
ax.set_ylim(0, 3900)
ax.legend(frameon=False, loc="lower right", handlelength=2.4)

fig.tight_layout()
fig.savefig("/home/claude/warmup_artefact.pdf")
fig.savefig("/home/claude/warmup_artefact.png")
print("written")
