#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Cameron Brooks
# SPDX-License-Identifier: GPL-3.0-only
"""Runs the Python examples in README.md exactly as written, against a built bundle, and checks what they and the notes under them claim.
The examples load "~/.vst3/ContrastivePassive.vst3" and read "mix.wav". This test copies the bundle under test into a scratch HOME
(build/readme-examples/.vst3/) and writes a test mix.wav there (a 32-bit float stereo impulse, 0.5 on the left and 0.25 on the right),
so the text of the examples runs unmodified. usage: python3 tests/readme_examples.py <bundle>"""
import contextlib, io, os, re, runpy, shutil, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
import ref_model as M
import pedalboard
from pedalboard.io import AudioFile

bundle = os.path.abspath(sys.argv[1])
blocks = re.findall(r"```python\n(.*?)```", open(os.path.join(ROOT, "README.md")).read(), re.S)
assert len(blocks) == 2, f"expected two python examples in README.md, found {len(blocks)}"
work = os.path.join(ROOT, "build", "readme-examples")
shutil.rmtree(work, ignore_errors=True); os.makedirs(os.path.join(work, ".vst3"))
shutil.copytree(bundle, os.path.join(work, ".vst3", "ContrastivePassive.vst3"))
for i, b in enumerate(blocks, start=1):
    open(os.path.join(work, f"readme_example_{i}.py"), "w").write(b)
fs = 48000; x = np.zeros((2, 1 << 16), np.float32); x[0, 0] = 0.5; x[1, 0] = 0.25
with AudioFile(os.path.join(work, "mix.wav"), "w", fs, 2, bit_depth=32) as f:   # 32-bit float: reads back exactly
    f.write(x)
os.environ["HOME"] = work; os.chdir(work)
fails = []
def check(name, ok, detail=""):
    print(f"[{'PASS' if ok else 'FAIL'}] {name}{': ' + detail if detail else ''}")
    if not ok: fails.append(name)
def resp_db(y, x0, f, sr):
    n = np.arange(len(y)); return 20 * np.log10(np.abs(np.exp(-2j * np.pi * np.outer(f, n) / sr) @ y.astype(float)) / x0)
def flat():
    return {"bands": [{"mode": 1, "type": 1, "gain_step": 0, "bw_step": 7, "freq_idx": 5} for _ in range(4)], "eq_in": True, "trim_idx": 5, "lp_idx": 0, "hp_idx": 0}

# example 1, as written
buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    ns = runpy.run_path(os.path.join(work, "readme_example_1.py"))
printed = buf.getvalue(); print("example 1 printed:", printed.strip())
check("example 1 prints the name and the parameter count", printed.strip() == "Contrastive Passive EQ 50", repr(printed.strip()))
audio, sr, out = ns["audio"], ns["sr"], ns["out"]
check("example 1 reads mix.wav unchanged", sr == fs and np.array_equal(audio, x), f"sr {sr}")
check("example 1 keeps the length (zero latency)", out.shape == audio.shape, str(out.shape))
check("example 1 writes mix_eq.wav", os.path.getsize(os.path.join(work, "mix_eq.wav")) > 0)
s1 = flat(); s1["bands"][2] = {"mode": 0, "type": 1, "gain_step": 4, "bw_step": 7, "freq_idx": 7}; s1["trim_idx"] = 4; s1["hp_idx"] = 4
F = np.array([30.0, 100.0, 1000.0, 3300.0, 10000.0])
got = resp_db(out[0], 0.5, F, sr); want = 20 * np.log10(np.abs(M.analog_H(s1, F)))
check("example 1 response equals the analog network (within 0.01 dB)", bool(np.all(np.abs(got - want) < 0.01)),
      " ".join(f"{f:.0f} Hz {g:+.3f} (network {w:+.3f})" for f, g, w in zip(F, got, want)))
fg = np.geomspace(1000, 10000, 400); peak = fg[np.argmax(resp_db(out[0], 0.5, fg, sr))]
check("example 1 peaks at 3.3 kHz, +3.6 dB before the trim", abs(peak / 3300 - 1) < 0.02 and abs(got[3] + 0.5 - 3.6) < 0.05,
      f"peak at {peak:.0f} Hz, {got[3] + 0.5:+.2f} dB before the trim")
check("example 1: the 30 Hz high pass is 3 dB down at 30 Hz", abs((got[0] + 0.5) + 3.0) < 0.1, f"{got[0] + 0.5:+.2f} dB before the trim")
check("example 1: LINK on, the right channel is the left channel's filter", float(np.max(np.abs(out[1] * 2 - out[0]))) < 1e-7)

# example 2, as written, continuing from example 1's variables
ns2 = runpy.run_path(os.path.join(work, "readme_example_2.py"), init_globals=ns)
mid, side, mid_eq, side_out, out_ms = ns2["mid"], ns2["side"], ns2["mid_eq"], ns2["side_out"], ns2["out_ms"]
check("example 2: the side comes back bit for bit", np.array_equal(side_out, side))
s2 = flat(); s2["bands"][3] = {"mode": 0, "type": 0, "gain_step": 3, "bw_step": 0, "freq_idx": 9}
F2 = np.array([1000.0, 5000.0, 8000.0, 12000.0, 16000.0, 20000.0])
g2 = resp_db(mid_eq, 0.375, F2, sr); w2 = 20 * np.log10(np.abs(M.analog_H(s2, F2)))
check("example 2: the mid gets the 16K shelf of the analog network (within 0.01 dB)", bool(np.all(np.abs(g2 - w2) < 0.01)),
      " ".join(f"{f:.0f} Hz {g:+.3f} (network {w:+.3f})" for f, g, w in zip(F2, g2, w2)))
check("example 2: back to left/right", out_ms.shape == audio.shape and np.array_equal(out_ms[0], mid_eq + side_out))

# the note on saving state: raw_state read before any processing holds the previous values; after one buffer it round-trips exactly
p = pedalboard.load_plugin(bundle); p.l_band1_mode = "CUT"; p.l_band1_gain_step = 9
q = pedalboard.load_plugin(bundle); q.raw_state = p.raw_state
early = (q.l_band1_mode, float(q.l_band1_gain_step))
p(np.zeros((2, 512), np.float32), fs)
q = pedalboard.load_plugin(bundle); q.raw_state = p.raw_state
late = (q.l_band1_mode, float(q.l_band1_gain_step))
check("note: raw_state before any processing holds the previous values", early == ("OUT", 0.0), str(early))
check("note: raw_state after one buffer restores exactly", late == ("CUT", 9.0), str(late))

os.chdir(ROOT); shutil.rmtree(work, ignore_errors=True)
print("PASS" if not fails else f"FAIL ({len(fails)})")
sys.exit(0 if not fails else 1)
