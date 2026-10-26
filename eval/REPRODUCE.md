# Reproducing the measurements

This document maps every measured table and figure in the thesis to the script
that produced it, the state of the source tree it ran against, the directory
holding its logs, and the command that turns those logs into the reported
numbers.

It also states, for each quantity, whether repeating the measurement can be
expected to reproduce it. That part matters: the absolute throughput figures
are **not** reproducible, and a reader who re-runs the campaign and sees
different numbers should find that documented rather than surprising. The
reasoning is in the thesis, section *Reproducibility*; the short version is at
the end of this file.

---

## 1. Environment

| | |
|---|---|
| Machine | `bress145-14.fin.uni-magdeburg.de`, shared, no administrative access |
| CPU | 2 × Intel Xeon Gold 5220R, 48 physical cores, 96 logical, 2 NUMA nodes |
| DRAM | 376 GB |
| NVM | Intel Optane DC PM, namespace `namespace1.0`, 504 GiB, `fsdax`, NUMA node 1 |
| Mount | `/mnt/pmem1` with the `dax` option |
| OS / compiler | Ubuntu, Linux 5.4, GCC 8.4 |
| Working directory on NVM | `/mnt/pmem1/pkonrad/spitfire_buf/` |
| Database directory | `~/spitfire_test/db/` |

Two constraints follow from the machine being shared. Competing workloads
cannot be excluded, and CPU frequency scaling cannot be controlled. The later
sweep scripts therefore record `uptime` and the list of foreign `ycsb`
processes into every log; the main campaign predates that practice.

**Do not run two measurements at once.** Every run deletes and recreates the
NVM working directory and the database. Two concurrent runs destroy each
other's data, and the results look plausible rather than obviously wrong. The
sweep scripts refuse to start if another `ycsb` process is running.

---

## 2. Building

    git clone https://github.com/Konraddp/spitfire.git
    cd spitfire
    git checkout <tag>            # see the table below
    mkdir -p build && cd build
    cmake .. && make -j ycsb

Two tags matter, and they are not interchangeable:

| Tag | Commit | Use for |
|---|---|---|
| `campaign-20260821` | `6b746cb` | everything read-only |
| `write-20260917` | `a11430a` | the write-path campaigns and their validation |

The read-only state carries no write path. The later state adds one, and
although the counters show it does not change the work the read path performs,
**read measurements should be taken against `campaign-20260821`** so that the
comparison with the reported numbers is exact.

No build flags are set. `CMAKE_BUILD_TYPE` is empty and `CMAKE_CXX_FLAGS` is
empty, so the binary is unoptimised and `NDEBUG` is not defined, the
assertions in the code were active during all measured runs. If a previous
`cmake` invocation left flags in the cache, clear them:

    grep CMAKE_CXX_FLAGS CMakeCache.txt
    cmake -DCMAKE_CXX_FLAGS="" ..

---

## 3. What produced what

### Read-only campaign

* **Script** `sweep.sh`: *reconstructed, see the note at the end of this
  section*
* **Tag** `campaign-20260821`
* **Logs** `results/sweep_20260821_0534/` (54 runs, all succeeded)
* **Analysis** `python3 analyze.py results/sweep_20260821_0534/summary.csv`
* **Runtime** about four hours

Produces, in the thesis:

| Thesis item | Where the numbers come from |
|---|---|
| Compression ratios | the compaction pass output in any campaign log |
| Absolute throughput | `analyze.py` main table |
| Throughput ratios | `analyze.py` main table |
| Counters per decoding call | the `M3 per_call` line of each log |
| Work per operation | the aggregate `M3` line plus the throughput, at K=100 |
| Buffer statistics (DRAM residency, inclusivity) | the replacer blocks at the end of each log |
| Retry confound check | `analyze.py`, per-cardinality section |
| Figures `absolute.png`, `speedup_curve.png` | `analyze.py` |

To read the counters and the buffer statistics out of a log directly:

    grep "M3" results/sweep_20260821_0534/K100_A_r1.log
    grep -h "inclusivity" results/sweep_20260821_0534/K100_*_r1.log
    sed -n '/dram_buf_replacer/,/^----dram_buf_replacer----$/p' \
        results/sweep_20260821_0534/K100_B_r1.log

**Note on the script.** The file that drove this campaign was overwritten and
is gone. `sweep.sh` in this directory is a reconstruction, derived from the
campaign logs: the parameters from the `LOG_INFO` lines each run writes, the
file naming from the directory listing, the loop order from the log
timestamps, and the CSV format from the existing `summary.csv`. It reproduces
the campaign's behaviour but is not the original file.

### Write-path campaigns

* **Script** `sweep_write.sh`
* **Tag** `write-20260917`
* **Logs** `results/write_20260917_1406/` and `results/write_20260918_0017/`
* **Analysis** `python3 analyze.py results/write_<stamp>/summary.csv`
* **Runtime** about three and a half hours each

Two arms only. Full decoding is excluded because its updates are not reliably
persisted, see the validation below. The `analyze.py` output leaves the B/A
and A/off columns empty, which is expected.

A third write campaign was run on 18 October as Block B of `sweep_repro.sh`
(see below), on a reserved and otherwise idle machine. All three are reported
in the thesis. The first two agree on the ordering and the magnitude but not
on the shape of the curve; the third supplies the shape and shows why the
first two could not. Their uncompressed baselines varied by 20 % and 12 %
across cardinality, a parameter that cannot affect them.

### Full reproduction, 18 October

* **Script** `sweep_repro.sh`: Block A read-only, Block B under update load
* **Tag** `campaign-20260821` (rebuilt from the same state)
* **Logs** `results/repro_A_20261018_0931/` (Block A, read-only) and
  `results/repro_B2_20261018_2006/` (Block B, update load); files are named
  `<block>_K<card>_<arm>_r<rep>.log` with a matching `.log.load` carrying
  `uptime` and the foreign-process count
* **Analysis** built into the script; also `summary.csv` per block
* **Runtime** ten hours nineteen minutes for both blocks

This is the campaign that settles the band and the level dependence. Block A
repeats the read-only campaign in full on one occasion: ten cardinalities,
three arms, three repetitions, ninety runs, with the repetitions as the outer
loop so that any drift over the day affects every cardinality alike. It lost
no cell; four runs needed a second attempt. Block B repeats the write campaign
at six cardinalities, two arms, three repetitions.

**One correction to the script.** An early version passed `-u 10` for the
update ratio. The binary expects a fraction and rejects anything above one
with `Invalid update_ratio`, so Block B failed instantly in all 36 runs. The
version in this directory sets `B_UPDATE=0.1`, matching `sweep_write.sh`.

### Concurrency

* **Script** `sweep_threads2.sh` (binds to NUMA node 1, logs machine load)
* **Tag** `write-20260917`
* **Logs** `results/threads_20260911_1415/`
* **Analysis** `python3 analyze_threads.py results/threads_20260911_1415`
* **Runtime** about two and a half hours

`analyze_threads.py` reads the individual logs rather than a `summary.csv`,
because this sweep does not write one; it produces one in the sweep directory
as a side effect, in the same format the other scripts use.

Two earlier variants are kept for the record and are **not** the source of any
reported number: `sweep_threads.sh` (no NUMA binding, no load logging) produced
`threads_20260909_0043`, whose figures the later run reversed;
`sweep_threads_low.sh` repeated only the 1- and 4-worker cells.

### Correctness validation

Three runs per configuration, compared by hand.

    cd ~/spitfire/build
    for m in off A B; do
      ./ycsb -k 512 -T 7480 -Y 90000 -I -s -B 1 -u 0.1 -V -K <card> -C $m \
             -P /mnt/pmem1/pkonrad/spitfire_buf/ -D ~/spitfire_test/db/ \
             > ~/val_${m}.log 2>&1
    done
    grep -h "checksum=" ~/val_*.log

`-V` reads every tuple, hashes it with FNV-1a and combines the hashes by
exclusive-or, so the result is independent of read order. With `-u 0` it
validates the read path alone; with any `-u > 0` it first applies a
deterministic update sequence through the write path and then takes the
checksum. The value of `-u` beyond being non-zero does not matter here, it is
a switch in this mode.

Expected results, at cardinalities 100, 1000 and 100000, covering all three
identifier widths:

| | read path (`-u 0`) | write path (`-u 0.1`) |
|---|---|---|
| baseline vs. selective decoding | identical | identical |
| baseline vs. full decoding | identical | **differs** |

The divergence of full decoding under updates is a finding, not a defect of
the procedure: that arm has no encoding step, so its updates are not written
back into the compressed page. It is reproducible bit for bit.

### Value generator

* **Script** `generator_analysis.py`

Reproduces the loader's value generator offline. It shows that the observed
cardinality of about 428,656 at a requested 500,000 comes from a 32-bit
overflow in the multiplication rather than from random collisions, and that
the resulting value distribution is close to uniform, which excludes improved
dictionary cache locality as an explanation for the anomaly reported at that
cardinality.

---

## 4. What reproduces and what does not

Repeating a measurement moves the numbers by different amounts depending on
what is being repeated. Three levels were observed:

| Level | Magnitude |
|---|---|
| Consecutive repetitions within one campaign | below 1.5 % (read-only), up to 5 % under foreign load |
| Separate runs on the same day, same state | about 1.3 % |
| Separate days, same machine | 14 % to 17 % |
| Across a machine restart | more than a third |

**Reproduces exactly.** The checksums. The compression ratios. The observed
cardinality per column reported by the compaction pass. These are determined
by the deterministic value generator and the format, not by the machine.

**Reproduces.** Every per-operation counter. Full decoding at a cardinality of
100,000, measured thirty days apart across a machine restart, reported
identical bytes and resolutions per call and per operation while its throughput
differed by a factor of 2.2. This is the basis for the weight the thesis places
on the counters.

**Reproduces approximately, and the error is now measured.** The throughput
*ratios* within a campaign, since the arms are measured consecutively and
share their conditions. The comparison between selective and full decoding
held to within 2 % across the restart; the ratios involving the baseline moved
by about 10 %.

The full reproduction of 18 October quantifies what a ratio costs when it is
carried between occasions. Measuring all ten cardinalities at one level and
comparing against the reported figures, the four cardinalities first measured
by the supplementary campaign, which ran at the same level, came back within
2 %, while the six from the main campaign, which ran 20 % faster, came back
3 % to 9 % high. **Ratios hold within a throughput level and shift by roughly
5 % between levels**, in the direction that the compressed arms gain as the
machine slows. That is enough for the ordering of the arms and for the
existence of the band of net loss; it is not enough to fix the band's width,
whose upper edge moves by a factor of two between the two measurements.

**Does not reproduce.** Absolute throughput. A reproduction attempt three weeks
after the campaign, on the same machine and against the same tagged commit,
measured all three arms at roughly two-thirds of their campaign figures.
Machine load, NUMA binding, warm-up duration and the state of the source tree
were each tested and excluded; the machine had been restarted in the interval.

A reader who re-runs `sweep.sh` today should therefore expect the shape of the
results, flat baseline, both compressed arms declining with cardinality,
selective decoding well above the other two, full decoding crossing the
baseline, but not the values on the axis.

---

## 5. Known issues

**Full decoding fails sporadically.** The arm terminates abnormally in a
substantial fraction of runs, after the warm-up phase. The failure has resisted
diagnosis: it does not reproduce under a debugger, the platform restricts
`ptrace` and discards core dumps, the address sanitiser is too slow to reach
the failure window, and the thread sanitiser cannot be built here. Six
hypotheses were tested and refuted. No variable examined in this work governs
its rate, it has been observed at one worker and at sixteen, and a series of
sixteen consecutive runs of one configuration produced none at all. The sweep
scripts retry up to four times; runs that complete are unaffected, which was
checked separately.

**Write-ahead logging must stay off.** Enabling it overflows the log during the
compaction pass, which rewrites every leaf page in one burst.

**Mini pages are not supported** and are rejected by an assertion.

**The profiling interval must divide the measurement duration**, or the engine
crashes on exit. With `-d 120 -p 10` each run reports eleven per-interval
figures.
