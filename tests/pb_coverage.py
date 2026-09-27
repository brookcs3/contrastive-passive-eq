#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Cameron Brooks
# SPDX-License-Identifier: GPL-3.0-only
"""Control coverage through Pedalboard: every knob and switch at every position, on both channels, plus LINK and POWER (833 renders,
204 checks: read-back of every position, BOOST/OUT/CUT, SHELF/BELL, all 16 gain and bandwidth detents, all 11 frequencies, IN, trim,
both filters, LINK, channel independence, POWER, bit-exact null tests, latency). Then the realisation check over the same renders: each
channel's magnitude against the analog network (tests/ref_model.py), 20 Hz to min(20 kHz, 0.45 fs), 1,658 channel responses.
The renders are held in memory (about 0.5 GB).
usage: python3 tests/pb_coverage.py <bundle> [out json]"""
import sys, os, json, time, numpy as np, pedalboard
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ref_model as ref
FREQS = ref.FREQS
MODES = ["BOOST", "OUT", "CUT"]; TYPES = ["SHELF", "BELL"]; LPS = ["OFF", "52K", "40K", "27K", "20K", "15K"]; HPS = ["OFF", "12", "16", "23", "30", "39"]
TRIMS = [-2.5 + 0.5 * i for i in range(11)]
def default_state():
    s = {}
    for ch in "lr":
        for k in range(1, 5):
            s[f"{ch}_band{k}_mode"] = "OUT"; s[f"{ch}_band{k}_type"] = "BELL"; s[f"{ch}_band{k}_gain_step"] = 0.0
            s[f"{ch}_band{k}_bandwidth"] = 7.0; s[f"{ch}_band{k}_freq_hz"] = float(FREQS[k][5])
        s[f"{ch}_in"] = True; s[f"{ch}_gain_db"] = 0.0; s[f"{ch}_lowpass"] = "OFF"; s[f"{ch}_highpass"] = "OFF"
    s["link"] = False; s["power"] = True
    return s
def job(jid, over, sr=48000, n=65536, signal="impulse"):
    s = default_state(); s.update(over)
    # set modes last so a band is never briefly live with a half-set state (irrelevant with reset=True, kept tidy)
    items = [[k, v] for k, v in s.items() if not k.endswith("_mode")] + [[k, v] for k, v in s.items() if k.endswith("_mode")]
    return {"id": jid, "sr": sr, "n": n, "set": items, "fresh": False, "signal": signal}
def band(ch, k, mode="BOOST", typ="BELL", g=8, bw=8, fidx=5):
    return {f"{ch}_band{k}_mode": mode, f"{ch}_band{k}_type": typ, f"{ch}_band{k}_gain_step": float(g), f"{ch}_band{k}_bandwidth": float(bw),
            f"{ch}_band{k}_freq_hz": float(FREQS[k][fidx])}
def make_jobs():
    J = [job("flat_default", {})]
    for ch in "lr":
        for k in range(1, 5):
            for m in MODES: J.append(job(f"{ch}_b{k}_mode_{m}", band(ch, k, mode=m)))
            for t in TYPES: J.append(job(f"{ch}_b{k}_type_{t}", band(ch, k, typ=t, g=10, bw=4)))
            for g in range(16):
                J.append(job(f"{ch}_b{k}_gain_{g}_boost", band(ch, k, g=g, bw=15)))
                J.append(job(f"{ch}_b{k}_gain_{g}_cut", band(ch, k, mode="CUT", g=g, bw=15)))
                J.append(job(f"{ch}_b{k}_gain_{g}_shelf", band(ch, k, typ="SHELF", g=g, bw=0)))
            for bw in range(16):
                J.append(job(f"{ch}_b{k}_bw_{bw}_bell", band(ch, k, g=15, bw=bw)))
                J.append(job(f"{ch}_b{k}_bw_{bw}_shelf", band(ch, k, typ="SHELF", g=15, bw=bw)))
            for fi in range(11): J.append(job(f"{ch}_b{k}_freq_{fi}", band(ch, k, g=15, bw=15, fidx=fi), sr=96000))
        for v in (False, True): J.append(job(f"{ch}_in_{v}", {**band(ch, 3, g=12, bw=10), f"{ch}_in": v, f"{ch}_lowpass": "20K", f"{ch}_highpass": "30", f"{ch}_gain_db": 1.5}))
        for t in TRIMS: J.append(job(f"{ch}_trim_{t:+.1f}", {f"{ch}_gain_db": t}))
        for lp in LPS: J.append(job(f"{ch}_lp_{lp}", {f"{ch}_lowpass": lp}, sr=192000))
        for hp in HPS: J.append(job(f"{ch}_hp_{hp}", {f"{ch}_highpass": hp}, sr=48000, n=131072))
    L = band("l", 3, g=12, bw=10, fidx=7); R = band("r", 2, mode="CUT", g=9, bw=5, fidx=3)
    J.append(job("link_off_LR_different", {**L, **R, "l_lowpass": "27K", "r_highpass": "23", "l_gain_db": 1.0, "r_gain_db": -0.5, "link": False}))
    J.append(job("link_on_LR_different", {**L, **R, "l_lowpass": "27K", "r_highpass": "23", "l_gain_db": 1.0, "r_gain_db": -0.5, "link": True}))
    J.append(job("link_on_R_changed", {**L, **band("r", 4, typ="SHELF", g=15, bw=15, fidx=8), "l_lowpass": "27K", "l_gain_db": 1.0, "r_lowpass": "15K", "r_gain_db": 2.5, "r_in": False, "link": True}))
    J.append(job("indep_L_with_R", {**L, **R, "link": False}))
    J.append(job("link_on_L_changed", {**band("l", 1, typ="SHELF", g=6, bw=3, fidx=3), **R, "link": True}))
    J.append(job("link_on_L_in_off", {**L, **R, "l_in": False, "link": True}))
    J.append(job("indep_L_only", {**L}))
    J.append(job("indep_R_only", {**R}))
    J.append(job("power_off_active", {**L, **R, "l_lowpass": "15K", "r_highpass": "39", "power": False}))
    J.append(job("power_on_active", {**L, **R, "l_lowpass": "15K", "r_highpass": "39", "power": True}))
    J.append(job("power_off_noise", {**L, **R, "l_lowpass": "15K", "r_highpass": "39", "power": False}, signal="noise"))
    # null tests with a noise input: flat default; every band BOOST at gain 0; IN off with bands active; trim 0 filters OFF
    J.append(job("null_default_noise", {}, signal="noise"))
    z = {}
    for ch in "lr":
        for k in range(1, 5): z.update(band(ch, k, mode=["BOOST", "CUT"][k % 2], typ=["BELL", "SHELF"][k % 2], g=0, bw=15, fidx=10))
    J.append(job("null_all_bands_gain0_noise", z, signal="noise"))
    J.append(job("null_in_off_noise", {**L, **R, "l_in": False, "r_in": False, "l_lowpass": "15K", "r_highpass": "39"}, signal="noise"))
    return J
def mag_db(y, f, fs):
    n = np.arange(len(y)); return 20 * np.log10(np.abs(np.exp(-2j * np.pi * np.outer(f, n[:8192]) / fs) @ y[:8192]))
def spec(y, fs, nfft=1 << 17):
    Y = np.fft.rfft(y, nfft); f = np.fft.rfftfreq(nfft, 1 / fs); return f, 20 * np.log10(np.abs(Y) + 1e-300)
def analyse(R, X, idx):
    J = {j["id"]: j for j in make_jobs()}
    Y = lambda jid: R[jid].astype(float)
    imp = lambda n: np.eye(1, n)[0]
    res = {"checks": [], "fail": []}
    def check(name, ok, detail=""):
        res["checks"].append({"name": name, "ok": bool(ok), "detail": detail})
        if not ok: res["fail"].append(name + " " + detail)
    # readback: every set value reads back as that position's legend
    rb_bad = 0
    for jid, j in J.items():
        for k, v in j["set"]:
            s = idx[jid]["readback"][k]
            ok = (s == ("ON" if v else "OFF")) if isinstance(v, bool) else (s == v) if isinstance(v, str) else abs(float(s) - float(v)) < 1e-6
            rb_bad += (not ok)
    check("readback of every set position", rb_bad == 0, f"{rb_bad} mismatches over {sum(len(j['set']) for j in J.values())} sets")
    y0 = Y("flat_default"); check("default state is bit-transparent (both channels)", np.array_equal(y0, np.vstack([imp(y0.shape[1])] * 2)))
    ident = lambda y: np.array_equal(y, imp(len(y)))
    for c, ch in enumerate("lr"):
        o = 1 - c
        for k in range(1, 5):
            f0 = FREQS[k][5]
            yb, yo, yc = Y(f"{ch}_b{k}_mode_BOOST"), Y(f"{ch}_b{k}_mode_OUT"), Y(f"{ch}_b{k}_mode_CUT")
            gb, gc = mag_db(yb[c], [f0], 48000)[0], mag_db(yc[c], [f0], 48000)[0]
            check(f"{ch} band{k} mode BOOST boosts at {f0} Hz", gb > 1.0, f"{gb:+.2f} dB")
            check(f"{ch} band{k} mode OUT is a hard bypass (bit-identical)", ident(yo[c]))
            check(f"{ch} band{k} mode CUT cuts at {f0} Hz", gc < -1.0, f"{gc:+.2f} dB")
            for jid in (f"{ch}_b{k}_mode_BOOST", f"{ch}_b{k}_mode_CUT"):
                check(f"{jid}: other channel untouched", ident(Y(jid)[o]))
            ys, yl = Y(f"{ch}_b{k}_type_SHELF"), Y(f"{ch}_b{k}_type_BELL")
            F = np.array([20.0, f0, 20000.0]); ms, ml = mag_db(ys[c], F, 48000), mag_db(yl[c], F, 48000)
            if k <= 2: shelf_ok = ms[0] > ms[2] + 3 and ms[0] > 3
            else: shelf_ok = ms[2] > ms[0] + 3 and ms[2] > 3
            check(f"{ch} band{k} type SHELF is a {'low' if k <= 2 else 'high'} shelf", shelf_ok, f"20 Hz {ms[0]:+.2f}, f0 {ms[1]:+.2f}, 20 kHz {ms[2]:+.2f} dB")
            Fb = np.array([f0 / 8, f0, min(f0 * 8, 23000.0)]); mb = mag_db(yl[c], Fb, 48000)
            check(f"{ch} band{k} type BELL is a bell (peak at f0, falls >= 3 dB an octave-triple either side)", mb[1] > 1.0 and mb[0] < mb[1] - 3 and mb[2] < mb[1] - 3, f"f0/8 {mb[0]:+.2f}, f0 {mb[1]:+.2f}, 8 f0 {mb[2]:+.2f} dB; shelf at same f {mag_db(ys[c], Fb, 48000).round(2).tolist()}")
            # gain steps: boost and cut monotonic, every step distinct, step 0 bit-transparent
            for kind, sign in (("boost", 1), ("cut", -1), ("shelf", 1)):
                pk = []
                for g in range(16):
                    y = Y(f"{ch}_b{k}_gain_{g}_{kind}")[c]
                    if kind == "shelf":
                        fe = 20.0 if k <= 2 else 20000.0; pk.append(mag_db(y, [fe], 48000)[0])
                    else:
                        pk.append(mag_db(y, [f0], 48000)[0])
                    if g == 0: check(f"{ch} band{k} gain step 0 ({kind}) bit-transparent", ident(y))
                d_ = np.diff(sign * np.array(pk))
                check(f"{ch} band{k} gain_step 0..15 ({kind}) strictly monotonic, all 16 live", np.all(d_ > 0.02), "steps dB " + " ".join(f"{v:+.2f}" for v in pk))
                res.setdefault("gain_tables", {})[f"{ch}_b{k}_{kind}"] = [round(float(v), 3) for v in pk]
            # bandwidth: 16 positions each distinct; bell narrows and peak rises toward CW
            pk, w3 = [], []
            fgrid = np.geomspace(max(10, f0 / 20), min(23000, f0 * 20), 1200)
            for bw in range(16):
                y = Y(f"{ch}_b{k}_bw_{bw}_bell")[c]; m = mag_db(y, fgrid, 48000); p = m.max(); pk.append(p)
                above = fgrid[m >= p - 3.0]; w3.append(np.log2(above.max() / above.min()))
            check(f"{ch} band{k} bandwidth 0..15 (bell): peak rises toward CW (narrow), all 16 live", np.all(np.diff(pk) > 0.02), "peak dB " + " ".join(f"{v:.2f}" for v in pk))
            check(f"{ch} band{k} bandwidth 0..15 (bell): -3 dB width shrinks toward CW", np.all(np.diff(w3) < 0), "oct " + " ".join(f"{v:.2f}" for v in w3))
            res.setdefault("bw_tables", {})[f"{ch}_b{k}_bell_peak"] = [round(float(v), 3) for v in pk]
            res["bw_tables"][f"{ch}_b{k}_bell_w3oct"] = [round(float(v), 3) for v in w3]
            prev = None; distinct = True; dips = []
            for bw in range(16):
                y = Y(f"{ch}_b{k}_bw_{bw}_shelf")[c]
                if prev is not None and np.max(np.abs(y - prev)) < 1e-6: distinct = False
                prev = y; dips.append(mag_db(y, [f0], 48000)[0])
            check(f"{ch} band{k} bandwidth 0..15 (shelf): all 16 positions change the response", distinct, "dB at f0 " + " ".join(f"{v:+.2f}" for v in dips))
            res["bw_tables"][f"{ch}_b{k}_shelf_at_f0"] = [round(float(v), 3) for v in dips]
            # frequency: 11 positions, bell peak at the switch frequency (96 kHz renders)
            errs = []
            for fi in range(11):
                fn = FREQS[k][fi]; fg = np.geomspace(fn / 1.6, min(fn * 1.6, 47000), 800); y = Y(f"{ch}_b{k}_freq_{fi}")[c]
                m = mag_db(y, fg, 96000); errs.append(100 * (fg[np.argmax(m)] / fn - 1))
            check(f"{ch} band{k} freq_hz 11 positions: bell peak at each switch frequency (+/-2 %)", np.all(np.abs(errs) < 2.0), "peak error % " + " ".join(f"{e:+.2f}" for e in errs))
        # IN
        yi0, yi1 = Y(f"{ch}_in_False"), Y(f"{ch}_in_True")
        check(f"{ch} IN off bypasses bands, filters and trim (bit-identical)", ident(yi0[c]))
        check(f"{ch} IN on applies the EQ", not ident(yi1[c]))
        # trim
        tr = []
        for t in TRIMS:
            y = Y(f"{ch}_trim_{t:+.1f}")[c]; m = mag_db(y, np.array([30.0, 1000.0, 15000.0]), 48000); tr.append(m)
            check(f"{ch} gain_db {t:+.1f}: flat and exact", np.all(np.abs(m - t) < 0.001), " ".join(f"{v:+.4f}" for v in m))
        # low pass (192 kHz): -3 dB near nominal
        for i, lp in enumerate(LPS):
            y = Y(f"{ch}_lp_{lp}")[c]
            if lp == "OFF": check(f"{ch} lowpass OFF bit-transparent", ident(y)); continue
            fn = float(lp[:-1]) * 1000; f, m = spec(y, 192000); s = (f > 1000) & (f < 95000)
            f3 = f[s][np.argmax(m[s] < -3.0)] if np.any(m[s] < -3.0) else None
            a2 = np.interp(min(2 * fn, 95000), f, m)
            check(f"{ch} lowpass {lp}: -3 dB within 3 % of nominal", f3 is not None and abs(f3 / fn - 1) < 0.03, f"-3 dB at {f3} Hz; at {min(2*fn,95000):.0f} Hz {a2:.1f} dB; at 20 kHz {np.interp(20000, f, m):+.3f} dB")
            res.setdefault("lp", {})[f"{ch}_{lp}"] = {"f3": f3, "db_at_2fc_or_95k": float(a2), "db_20k": float(np.interp(20000, f, m)), "db_16k": float(np.interp(16000, f, m))}
        for hp in HPS:
            y = Y(f"{ch}_hp_{hp}")[c]
            if hp == "OFF": check(f"{ch} highpass OFF bit-transparent", ident(y)); continue
            fn = float(hp); f, m = spec(y, 48000, 1 << 20); s = (f > 1) & (f < 2000)
            f3 = f[s][np.argmax(m[s] > -3.0)]
            sl = np.interp(fn / 4, f, m) - np.interp(fn / 8, f, m)
            check(f"{ch} highpass {hp}: -3 dB within 3 % of nominal, 18 dB/oct below", abs(f3 / fn - 1) < 0.03 and 16.5 < sl < 19.5, f"-3 dB at {f3:.2f} Hz; slope fn/8..fn/4 {sl:.2f} dB/oct; at 20 Hz {np.interp(20, f, m):+.2f} dB")
            res.setdefault("hp", {})[f"{ch}_{hp}"] = {"f3": float(f3), "slope_db_oct": float(sl), "db_20": float(np.interp(20, f, m))}
    # LINK and independence
    a, b = Y("link_off_LR_different"), Y("link_on_LR_different")
    check("LINK off: L and R independent (different outputs)", not np.array_equal(a[0], a[1]))
    check("LINK on: R output bit-identical to L output", np.array_equal(b[0], b[1]))
    check("LINK on: L output unchanged by LINK", np.array_equal(a[0], b[0]))
    c_ = Y("link_on_R_changed"); check("LINK on: moving R controls (incl. R IN off) has no effect; R follows L", np.array_equal(c_[1], c_[0]) and np.array_equal(c_[0], b[0]))
    d_ = Y("link_on_L_changed"); check("LINK on: moving L controls moves both channels", np.array_equal(d_[0], d_[1]) and not np.array_equal(d_[0], b[0]))
    e_ = Y("link_on_L_in_off"); check("LINK on: L IN off bypasses both channels", ident(e_[0]) and ident(e_[1]))
    li, ri = Y("indep_L_only"), Y("indep_R_only")
    check("unlinked: an L-only setting leaves R bit-identical", ident(li[1]) and not ident(li[0]))
    check("unlinked: an R-only setting leaves L bit-identical", ident(ri[0]) and not ident(ri[1]))
    lr = Y("indep_L_with_R")
    check("unlinked: L output is the same whatever R is set to (channels do not interact)", np.array_equal(lr[0], li[0]) and np.array_equal(lr[1], ri[1]))
    po, pn = Y("power_off_active"), Y("power_on_active")
    check("POWER off: global bypass, both channels bit-identical", ident(po[0]) and ident(po[1]))
    check("POWER on: the EQ is applied", not ident(pn[0]) and not ident(pn[1]))
    for jid in ("power_off_noise", "null_default_noise", "null_all_bands_gain0_noise", "null_in_off_noise"):
        y = Y(jid); x = X[jid].astype(float)
        check(f"null test {jid}: output bit-identical to the noise input", np.array_equal(y, x), f"max |diff| {np.max(np.abs(y - x)):.3g}")
    check("every job: reported latency 0 and finite output", all(v["latency"] == 0 and v["finite"] for v in idx.values()))
    res["n_jobs"] = len(J); res["n_checks"] = len(res["checks"]); res["n_fail"] = len(res["fail"])
    return res
def render(bundle, jobs):
    """-> (renders, noise inputs, index): each job sets its parameters in order, reads them back, and processes a two-channel impulse
    (or seeded noise) with reset=True, on one shared plugin instance unless a job asks for a fresh one."""
    R, X, index = {}, {}, {}
    shared = None
    for j in jobs:
        if j.get("fresh", True) or shared is None:
            p = pedalboard.load_plugin(bundle)
            if not j.get("fresh", True): shared = p
        else:
            p = shared
        for name, val in j["set"]:
            setattr(p, name, val)
        rb = {name: p.parameters[name].string_value for name, _ in j["set"]}
        n = int(j["n"]); sr = float(j["sr"])
        if j.get("signal", "impulse") == "impulse":
            x = np.zeros((2, n), np.float32); x[:, 0] = 1.0
        else:
            rng = np.random.default_rng(int(j.get("seed", 1))); x = (0.25 * rng.standard_normal((2, n))).astype(np.float32)
        y = p.process(x, sr, reset=True)
        R[j["id"]] = y.astype(np.float32)
        if j.get("signal") == "noise": X[j["id"]] = x
        index[j["id"]] = {"readback": rb, "latency": int(p.reported_latency_samples), "finite": bool(np.all(np.isfinite(y)))}
    return R, X, index

def dtft(h, f, fs):
    h = np.asarray(h, float); n = np.arange(len(h)); out = np.empty(len(f), complex)
    nz = np.nonzero(np.abs(h) > 0)[0]; last = nz[-1] + 1 if len(nz) else 1
    hh = h[:last]; nn = n[:last]
    for i in range(0, len(f), 50):
        ff = np.asarray(f[i:i + 50])[:, None]
        out[i:i + 50] = np.exp(-2j * np.pi * ff * nn[None, :] / fs) @ hh
    return out
def to_ref(st, ch):
    src = "l" if (st["link"] and ch == "r") else ch
    if not st["power"] or not st[f"{src}_in"]: return None
    bl = []
    for k in range(1, 5):
        bl.append({"mode": ["BOOST", "OUT", "CUT"].index(st[f"{src}_band{k}_mode"]), "type": ["SHELF", "BELL"].index(st[f"{src}_band{k}_type"]),
                   "gain_step": int(st[f"{src}_band{k}_gain_step"]), "bw_step": int(st[f"{src}_band{k}_bandwidth"]),
                   "freq_idx": FREQS[k].index(int(st[f"{src}_band{k}_freq_hz"]))})
    return {"bands": bl, "eq_in": True, "trim_idx": int(round((st[f"{src}_gain_db"] + 2.5) / 0.5)), "lp_idx": LPS.index(st[f"{src}_lowpass"]), "hp_idx": HPS.index(st[f"{src}_highpass"])}
def realisation(jobs, R):
    """every impulse render, each channel: magnitude against the analog network of the setting that channel runs (unity when bypassed)"""
    wm, wp, n, worst = 0.0, 0.0, 0, None
    for j in jobs:
        if j.get("signal") == "noise": continue
        st = dict((k, v) for k, v in j["set"]); y = R[j["id"]].astype(float); fs = j["sr"]
        F = np.geomspace(20, min(20000, 0.45 * fs), 160)
        for c, ch in enumerate("lr"):
            rp = to_ref(st, ch)
            H = dtft(y[c], F, fs)
            Hn = np.ones_like(H) if rp is None else ref.analog_H(rp, F)
            em = np.max(np.abs(20 * np.log10(np.abs(H) / np.abs(Hn)))); n += 1
            if em > wm: wm, worst = em, (j["id"], ch)
            if rp is None or rp["lp_idx"] == 0:
                ep = np.max(np.abs((np.degrees(np.angle(H / Hn)) + 180) % 360 - 180))
                if fs >= 96000: wp = max(wp, ep)
    return {"n": n, "worst_mag_db": wm, "worst_at": worst, "worst_phase_deg_96k_noLP": wp}

if __name__ == "__main__":
    bundle = sys.argv[1]; outjson = sys.argv[2] if len(sys.argv) > 2 else None
    t0 = time.time(); jobs = make_jobs()
    R, X, idx = render(bundle, jobs)
    print("rendered", len(jobs), "jobs in", round(time.time() - t0, 1), "s")
    r = analyse(R, X, idx)
    print("jobs", r["n_jobs"], "checks", r["n_checks"], "fail", r["n_fail"])
    for f in r["fail"]: print("FAIL", f)
    z = realisation(jobs, R); r["realisation"] = z
    print(f"coverage renders compared: {z['n']} channel responses; worst magnitude error vs the analog network {z['worst_mag_db']:.4f} dB "
          f"({tuple(z['worst_at'])}); worst phase error at fs >= 96 kHz without a low pass {z['worst_phase_deg_96k_noLP']:.2f} deg")
    if outjson: json.dump(r, open(outjson, "w"), indent=1)
    ok = r["n_fail"] == 0 and r["n_checks"] == 204 and z["worst_mag_db"] < 0.01
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)
