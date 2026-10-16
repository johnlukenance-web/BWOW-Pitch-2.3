#!/usr/bin/env python3
"""Functional tests for the Bwow Pitch 2 DSP core (runs the real core code via core_test).
Synthetic guitar-like DI signals -> core -> everything is measured from the OUTPUT AUDIO:
 * pitch bend = slope of the local input->output time delay (cross-correlation), valid for chords too
 * triggers    = what the detector fired, compared with the known pick times"""
import subprocess, sys, os, numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, "core_test" + (".exe" if os.name == "nt" else ""))
TMP = os.path.join(HERE, "_tmp"); os.makedirs(TMP, exist_ok=True)
rng = np.random.default_rng(7)
results = []

def check(name, ok, detail=""):
    results.append(bool(ok)); print(("PASS  " if ok else "FAIL  ") + name + (("  [" + detail + "]") if detail else ""))

def pluck(sr, t, f0, dur, amp=0.3, decay=1.2, mute=False, click=True, nh=23):
    n = int(dur * sr); tt = np.arange(n) / sr; y = np.zeros(n)
    for k in range(1, nh + 1):
        if k * f0 > 0.45 * sr: break
        dk = (0.25 if mute else decay) / (1 + 0.35 * k)
        y += (1.0 / k ** 1.05) * np.exp(-tt / dk) * np.sin(2 * np.pi * k * f0 * tt + rng.uniform(0, 6.28))
    y *= np.minimum(1, tt / 0.0006)
    if click:
        cl = int(0.003 * sr); y[:cl] += rng.standard_normal(cl) * np.exp(-np.arange(cl) / (0.0007 * sr)) * 0.35
    fo = min(n, int(0.03 * sr)); y[n - fo:] *= 0.5 * (1 + np.cos(np.linspace(0, np.pi, fo)))
    return amp * y / np.max(np.abs(y))

def mix(sr, total, events):
    x = np.zeros(int(total * sr))
    for t, sig in events:
        i = int(t * sr); seg = sig[: len(x) - i]; x[i:i + len(seg)] += seg
    return x

def run(x, sr, amount=65, decay=143, sens=0, bm=0, channels=1):
    x = np.atleast_2d(x).astype(np.float32)
    fin = os.path.join(TMP, "in.raw"); fout = os.path.join(TMP, "out.raw"); x.tofile(fin)
    r = subprocess.run([EXE, fin, fout, str(sr), str(channels), str(amount), str(decay), str(sens), str(bm)],
                       capture_output=True, text=True, check=True).stdout.split("\n")
    lat = int(r[0].split()[1]); trig = [int(l.split()[1]) / sr for l in r if l.startswith("T ")]
    y = np.fromfile(fout, dtype=np.float32).reshape(x.shape)
    return (y[0] if channels == 1 else y), lat, trig

def delay_at(y, x, sr, tc, W=0.024, lo=0.0035, hi=0.0125):
    """local input->output delay in seconds around time tc (sub-sample, normalised cross-correlation)"""
    n = int(W * sr); i = int(tc * sr) - n // 2; seg = y[i:i + n].astype(np.float64)
    lags = np.arange(int(lo * sr), int(hi * sr) + 1); sc = np.empty(len(lags))
    for k, L in enumerate(lags):
        r = x[i - L:i - L + n].astype(np.float64); sc[k] = np.dot(seg, r) / np.sqrt(np.dot(r, r) + 1e-12)
    k = int(np.argmax(sc)); frac = 0.0
    if 0 < k < len(lags) - 1:
        a, b, c = sc[k - 1], sc[k], sc[k + 1]; d = a - 2 * b + c
        if d != 0: frac = 0.5 * (a - c) / d
    return (lags[k] + frac) / sr

def cents_at(y, x, sr, t, h=0.008):
    """pitch shift (cents) of the output at time t = -slope of delay (valid for chords: whole-signal shift)"""
    slope = (delay_at(y, x, sr, t + h) - delay_at(y, x, sr, t - h)) / (2 * h)
    return 1200 * np.log2(max(1e-3, 1 - slope))

def exp_c(dt, A=65, D=143): return max(0.0, A * (1 - dt * 1000 / D))

print("== 1. pick-triggered bend: +65 c at the attack, linear return over 143 ms ==")
def bend_test(sr, f0, tag):
    t_a = 1.0
    x = mix(sr, 3.0, [(t_a, pluck(sr, 0, f0, 2.0, decay=2.0))])
    y, lat, trig = run(x, sr); t0 = t_a + lat / sr
    check(f"[{tag}] exactly one trigger for one pick", len(trig) == 1, f"{[round(t,4) for t in trig]}")
    for dt in (0.03, 0.05, 0.07, 0.09, 0.11):
        got = cents_at(y, x, sr, t0 + dt); e = exp_c(dt)
        check(f"[{tag}] +{dt*1000:.0f} ms after attack: {got:5.1f} c (expect {e:5.1f})", abs(got - e) <= (9 if f0 < 50 else 6))
    for dt in (0.25, 0.5, 1.0):
        got = cents_at(y, x, sr, t0 + dt); check(f"[{tag}] +{dt*1000:.0f} ms: back at original pitch ({got:+.1f} c)", abs(got) <= 4)
    return y, x, lat
for sr in (44100, 48000, 96000): bend_test(sr, 110.0, f"{sr} Hz, 110 Hz note")
bend_test(48000, 55.0, "48000 Hz, 55 Hz low note"); bend_test(48000, 41.2, "48000 Hz, 41 Hz (8-string F#)")

print("== 2. timing: bend begins exactly when the attack reaches the output ==")
sr = 48000; f0 = 146.8
x = mix(sr, 2.0, [(0.8, pluck(sr, 0, f0, 1.2, decay=2.0))]); y, lat, trig = run(x, sr); t0 = 0.8 + lat / sr
dpost = delay_at(y, x, sr, t0 + 0.012, W=0.010); shift_ms = (lat / sr - dpost) * 1000
check(f"delay shortened by {shift_ms:.2f} ms within 12 ms of the attack reaching the output (expect ~0.43)", 0.30 < shift_ms < 0.55)
d_late = delay_at(y, x, sr, t0 + 0.05, W=0.010); check(f"delay still ~{(lat/sr-d_late)*1000:.2f} ms shorter at +50 ms (expect ~1.58)", 1.4 < (lat / sr - d_late) * 1000 < 1.8)
check(f"trigger fires {(trig[0]-0.8)*1000:.2f} ms after onset", 0 <= trig[0] - 0.8 <= 0.003)
check(f"reported latency {lat} samples = {lat/sr*1000:.2f} ms", abs(lat / sr - 0.010) < 1e-4)

print("== 3. sustained notes do not retrigger ==")
for name, f in (("A1 55 Hz", 55.0), ("D2 73.4 Hz", 73.4), ("A2 110 Hz", 110.0)):
    x = mix(sr, 12.0, [(0.5, pluck(sr, 0, f, 11.0, decay=6.0))])
    tt = np.arange(len(x)) / sr; x = x * (1 + 0.25 * np.sin(2 * np.pi * 5.5 * tt) * (tt > 2))
    y, lat, trig = run(x, sr)
    check(f"{name}: 1 trigger over 10.5 s sustain incl. 5.5 Hz tremolo", len(trig) == 1, f"{len(trig)} triggers")
tt = np.arange(int(8 * sr)) / sr; swell = 0.002 + 0.3 * np.clip((tt - 1) / 4, 0, 1) ** 2
x = swell * (np.sin(2 * np.pi * 73.4 * tt) + 0.5 * np.sin(2 * np.pi * 146.8 * tt) + 0.3 * np.sin(2 * np.pi * 220.2 * tt))
y, lat, trig = run(x, sr); check("volume swell (no pick): no triggers", len(trig) == 0, f"{len(trig)} triggers")

print("== 4. rapid picks retrigger ==")
for gap_ms, nn in ((120, 8), (80, 12), (55, 16), (40, 16)):
    ev = [(0.5 + i * gap_ms / 1000, pluck(sr, 0, 73.4 * (1.0 if i % 3 else 1.122), 0.6, decay=0.5, mute=(i % 2 == 0))) for i in range(nn)]
    x = mix(sr, 0.5 + nn * gap_ms / 1000 + 1.5, ev); y, lat, trig = run(x, sr)
    onset = [0.5 + i * gap_ms / 1000 for i in range(nn)]
    miss = [round(o, 3) for o in onset if not any(-0.0005 <= t - o <= 0.004 for t in trig)]
    check(f"{gap_ms} ms spacing: {nn - len(miss)}/{nn} picks detected, {len(trig)} triggers", not miss and len(trig) == nn, f"missed {miss}")
gap = 0.08; nn = 8; f = 110.0
ev = [(0.5 + i * gap, pluck(sr, 0, f, gap - 0.004 if i < nn - 1 else 0.7, decay=0.7)) for i in range(nn)]
x = mix(sr, 0.5 + nn * gap + 1.5, ev); y, lat, trig = run(x, sr)
post = [cents_at(y, x, sr, 0.5 + i * gap + lat / sr + 0.030) for i in range(1, nn)]
pre = [cents_at(y, x, sr, 0.5 + i * gap + lat / sr - 0.040) for i in range(1, nn)]
check("80 ms picks: bend restarts every time (cents 30 ms after each arrival; expect ~51)", all(42 < v < 60 for v in post), ", ".join(f"{v:.0f}" for v in post))
check("80 ms picks: previous bend was mid-way before each retrigger (expect ~34 at +40 ms)", all(26 < v < 42 for v in pre), ", ".join(f"{v:.0f}" for v in pre))

print("== 5. chords bend together (whole-signal shift) ==")
fs = (98.0, 130.0, 175.0, 233.0, 311.0)     # five pure partials, no shared harmonics, 0.4 ms strum skew
x = mix(sr, 3.0, [(1.0 + 0.0004 * i, pluck(sr, 0, f, 2.0, decay=2.0, amp=0.1, nh=1)) for i, f in enumerate(fs)])
y, lat, trig = run(x, sr); t0 = 1.0 + lat / sr
check("5-string chord (0.4 ms strum skew): single trigger", len(trig) == 1, f"{len(trig)}")
a_, b_ = int((t0 + 0.03) * sr), int((t0 + 0.09) * sr); N = 1 << 17; fr = np.fft.rfftfreq(N, 1 / sr)
Y = np.abs(np.fft.rfft(y[a_:b_] * np.hanning(b_ - a_), N)); X = np.abs(np.fft.rfft(x[a_ - lat:b_ - lat] * np.hanning(b_ - a_), N))
want = np.mean([exp_c(d) for d in np.linspace(0.03, 0.09, 30)])
for f in fs:
    w = (fr > f * 0.97) & (fr < f * 1.08); mv = 1200 * np.log2(fr[w][np.argmax(Y[w])] / fr[w][np.argmax(X[w])])
    check(f"chord note {f:.0f} Hz moved {mv:.1f} c (window-average expected {want:.1f})", abs(mv - want) < 7)

print("== 6. audio passes through at default settings / is never gated ==")
x = mix(sr, 4.0, [(0.5, pluck(sr, 0, 73.4, 3.0, decay=1.5)), (2.2, pluck(sr, 0, 98.0, 1.7, decay=1.0))])
y, lat, trig = run(x, sr); tr_ = trig
dbr = 20 * np.log10(np.sqrt(np.mean(y ** 2)) / np.sqrt(np.mean(x ** 2)))
check(f"overall RMS out/in = {dbr:+.2f} dB", abs(dbr) < 0.5)
w = int(0.030 * sr); xin = np.concatenate([np.zeros(lat), x])[:len(x)]
ein = np.sqrt(np.convolve(xin ** 2, np.ones(w) / w, "same")); eout = np.sqrt(np.convolve(y ** 2, np.ones(w) / w, "same"))
m = ein > 0.15 * ein.max()
for t_ in tr_: m[max(0, int((t_ + lat / sr - 0.03) * sr)): int((t_ + lat / sr + 0.06) * sr)] = False
dev = 20 * np.log10(np.maximum(eout[m], 1e-9) / ein[m])
check(f"30 ms RMS tracks delayed input away from attack windows (worst {np.max(np.abs(dev)):.2f} dB)", np.max(np.abs(dev)) < 1.5)
quiet = 0.003 * np.sin(2 * np.pi * 110 * np.arange(int(2 * sr)) / sr); yq, _, tq = run(quiet, sr)
check("-50 dBFS sustained tone passes at full level", abs(20 * np.log10(np.sqrt(np.mean(yq[sr:] ** 2)) / np.sqrt(np.mean(quiet[sr:] ** 2)))) < 0.2)
check("silence in -> silence out", np.max(np.abs(run(np.zeros(sr), sr)[0])) == 0.0)
tt = np.arange(sr * 3) / sr; xi = 0.05 * np.clip(tt / 1.5, 0, 1) * np.sin(2 * np.pi * 150 * tt)
y0, lat0, tr0 = run(xi, sr); ideal = np.concatenate([np.zeros(lat0), xi])[:len(xi)]
check("idle (no attack): output = input delayed by exactly the reported latency", len(tr0) == 0 and np.max(np.abs(y0 - ideal)) < 1e-6, f"{len(tr0)} triggers, max err {np.max(np.abs(y0-ideal)):.1e}, latency {lat0} samples")

print("== 7. noise bed / hum ==")
hum = 0.002 * rng.standard_normal(int(6 * sr)) + 0.003 * np.sin(2 * np.pi * 60 * np.arange(int(6 * sr)) / sr)
ev = [(1.0 + 0.9 * i, pluck(sr, 0, 73.4, 0.8, decay=0.6)) for i in range(5)]
x = hum + mix(sr, 6.0, ev); y, lat, trig = run(x, sr)
hit = sum(any(-0.0005 <= t - (1.0 + 0.9 * i) <= 0.004 for t in trig) for i in range(5))
check(f"-50 dBFS noise + hum: {hit}/5 picks detected, {len(trig)} triggers", hit == 5 and len(trig) == 5)

print("== 8. host block size independence ==")
x = mix(sr, 3.0, [(0.5 + 0.15 * i, pluck(sr, 0, 98.0, 0.5, decay=0.5)) for i in range(6)])
ya, _, ta = run(x, sr, bm=0); yb, _, tb = run(x, sr, bm=1)
check("identical output and triggers for 512-sample vs random 1..700-sample blocks", np.array_equal(ya, yb) and ta == tb)

print("== 9. stereo ==")
xl = mix(sr, 3.0, [(1.0, pluck(sr, 0, 110.0, 2.0, decay=2.0))]); xr = 0.5 * xl
y2, lat, trig = run(np.stack([xl, xr]), sr, channels=2)
check("stereo: one trigger, both channels bent identically", len(trig) == 1 and abs(cents_at(y2[0], xl, sr, 1 + lat / sr + 0.05) - exp_c(0.05)) < 6 and np.allclose(y2[1], 0.5 * y2[0], atol=1e-6))

print("== 10. controls ==")
x = mix(sr, 3.0, [(1.0, pluck(sr, 0, 110.0, 2.0, decay=2.0))])
y, lat, trig = run(x, sr, amount=120, decay=300); t0 = 1 + lat / sr
check("Amount 120 c / Decay 300 ms: +100 ms -> 80 c", abs(cents_at(y, x, sr, t0 + 0.1) - 120 * (1 - 100 / 300)) < 8, f"{cents_at(y, x, sr, t0+0.1):.1f}")
y, lat, trig = run(x, sr, amount=0); check("Amount 0: no pitch change", abs(cents_at(y, x, sr, 1.06 + lat / sr)) < 3)
xs = mix(sr, 3.0, [(1.0, pluck(sr, 0, 110.0, 2.0, decay=2.0, amp=0.02)), (2.0, pluck(sr, 0, 110.0, 0.9, decay=0.8, amp=0.02))])
_, _, t_auto = run(xs, sr, sens=0); _, _, t_low = run(xs, sr, sens=1); _, _, t_hi = run(xs, sr, sens=10)
check(f"sensitivity: soft picks (-37 dBFS) found by Auto={len(t_auto)}, 10={len(t_hi)}; least sensitive (1) = {len(t_low)} (<=Auto)", len(t_auto) == 2 and len(t_hi) >= 2 and len(t_low) <= len(t_auto))

print("== 11. no clicks from the head recentering / crossfades ==")
ev = [(0.5 + i * 0.09, pluck(sr, 0, 73.4 * (1.0 if i % 3 else 1.122), 0.3, decay=0.5, click=False)) for i in range(12)]
x = mix(sr, 2.5, ev); y, lat, trig = run(x, sr)
check(f"smooth picks: max sample step out/in = {np.max(np.abs(np.diff(y)))/np.max(np.abs(np.diff(x))):.2f} (<1.3), {len(trig)} triggers", np.max(np.abs(np.diff(y))) < 1.3 * np.max(np.abs(np.diff(x))))

print(f"\n{sum(results)}/{len(results)} checks passed")
sys.exit(0 if all(results) else 1)
