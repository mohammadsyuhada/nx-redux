#!/usr/bin/env python3
"""Summarise scripts/bench/results/<plat>/<TAG>-<id>.log into a pass table.

Pass (spec §4.4, fix round 2): fps mean >= 99% req, fps min >= 95% req,
buf_free never 0, and both drops and worst frame within baseline-relative bounds.
`frame_drops` counts any frame over 1.1x the display period (jitter included), so
a healthy run at full fps still logs a handful of "drops" per second; a run that
cannot keep up logs hundreds with cpu ~100%. The bound is therefore
drops <= max(2*n, 1.5*base_drops) where n is the number of analysed rows (seconds)
for the config and base_drops is the same TAG's `base` run. The worst-frame bound
is likewise baseline-relative: worst <= max(2.5*period, 1.25*base_worst), because
some cores carry a periodic scene/display hitch (a single 48-69 ms frame every
config, uncapped included, with fps at target and cpu 60-80%) that a flat
2.5-period bound would misread as CPU starvation. If no base log exists the bounds
fall back to 2*n and 2.5*period alone (a warning is printed).
Prints markdown and the cheapest passing config (by mean kHz of the cluster
that was running -- cpu4 is only counted when the run's online mask includes 4
and the config is not a little/A-cluster config).
"""
import glob, os, re, sys

BENCH = re.compile(r"\[bench\] t=(\d+) fps=([\d.]+) req=([\d.]+) avg_ms=([\d.]+) max_ms=([\d.]+) drops=(\d+) buf_free=(-?\d+) buf_target=(-?\d+) cpu=(\d+) khz0=(\d+) khz4=(\d+)")
CFG = re.compile(r"\[driver\] cfg=\S+ online=(\S+)")

def summarise(path, cid):
    grp = []; mask = None
    for l in open(path):
        m = BENCH.match(l)
        if m:
            grp.append(m.groups()); continue
        c = CFG.search(l)
        if c:
            mask = c.group(1)
    rows = grp[3:]  # drop the first seconds after the config change
    if not rows:
        return None
    fps = [float(r[1]) for r in rows]; req = float(rows[-1][2])
    worst = max(float(r[4]) for r in rows)
    drops = int(rows[-1][5]) - int(rows[0][5])
    buf_min = min(int(r[6]) for r in rows)
    khz0 = [int(r[9]) for r in rows]; khz4 = [int(r[10]) for r in rows]
    cpu = sum(int(r[8]) for r in rows) / len(rows)
    # cpu4 counts only when the online mask includes core 4 and this is not a
    # little/A-cluster run (an offline cpu4 keeps reporting its parked min freq).
    use_big = (mask is not None and '4' in mask) and not cid.startswith(('little', 'A'))
    active = khz4 if (use_big and sum(khz4) > sum(khz0)) else khz0
    return dict(n=len(rows), fps_mean=sum(fps) / len(fps), fps_min=min(fps), req=req,
                drops=drops, buf_min=buf_min, worst=worst, cpu=cpu,
                khz=sum(active) / len(active))

def worst_period(s):
    return 1000.0 / s['req'] if s['req'] > 0 else 16.7

def passes(s, drop_bound, worst_bound):
    return (s['fps_mean'] >= 0.99 * s['req'] and s['fps_min'] >= 0.95 * s['req']
            and s['drops'] <= drop_bound and s['buf_min'] > 0
            and s['worst'] <= worst_bound)

def main(plat, tag):
    here = os.path.dirname(os.path.abspath(__file__))
    logs = sorted(glob.glob(f"{here}/results/{plat}/{tag}-*.log"))
    base_path = f"{here}/results/{plat}/{tag}-base.log"
    base_s = summarise(base_path, "base") if os.path.exists(base_path) else None
    base_drops = base_s['drops'] if base_s else None
    base_worst = base_s['worst'] if base_s else None
    if base_s is None:
        print(f"warning: no {tag}-base.log; drop bound = 2*n, worst bound = 2.5*period (no baseline)")
    print(f"| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    best = None
    for p in logs:
        cid = os.path.basename(p)[len(tag) + 1:-4]
        s = summarise(p, cid)
        if not s:
            print(f"| {cid} | no samples | | | | | | | | |"); continue
        drop_bound = max(2 * s['n'], 1.5 * base_drops) if base_drops is not None else 2 * s['n']
        worst_bound = (max(2.5 * worst_period(s), 1.25 * base_worst)
                       if base_worst is not None else 2.5 * worst_period(s))
        ok = passes(s, drop_bound, worst_bound)
        print(f"| {cid} | {s['fps_mean']:.1f}/{s['fps_min']:.1f} ({s['req']:.1f}) | {s['drops']} | {drop_bound:.0f} | {s['buf_min']} | {s['worst']:.1f} | {worst_bound:.1f} | {s['cpu']:.0f} | {s['khz']:.0f} | {'yes' if ok else 'no'} |")
        if ok and (best is None or s['khz'] < best[1]['khz']):
            best = (cid, s)
    print()
    print(f"cheapest passing: {best[0]} (mean {best[1]['khz']:.0f} kHz)" if best else "no passing config")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
