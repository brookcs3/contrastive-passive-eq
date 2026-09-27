#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Cameron Brooks
# SPDX-License-Identifier: GPL-3.0-only
"""Load the bundle with Pedalboard in this process and print every parameter: its Python name, the raw VST3 name, the kind Pedalboard
gives it, and its valid values. Fails unless the plugin reports 50 parameters and zero latency.
usage: python3 tests/pb_load.py <bundle dir> [out.json]"""
import sys, json, pedalboard
path = sys.argv[1]
p = pedalboard.load_plugin(path)
print("loaded:", p.name, "| pedalboard", pedalboard.__version__, "| parameters:", len(p.parameters))
out = {}
for name, prm in p.parameters.items():
    vv = None
    try:
        vv = list(prm.valid_values)
    except Exception:
        pass
    info = {"raw_name": getattr(prm, "name", name), "type": type(prm.raw_value).__name__, "python_type": getattr(prm, "type", None).__name__ if getattr(prm, "type", None) else None,
            "value": str(getattr(p, name)), "valid_values": [str(v) for v in vv] if vv is not None else None,
            "range": [str(prm.range[0]), str(prm.range[1]), str(prm.range[2])] if hasattr(prm, "range") else None, "units": getattr(prm, "units", ""),
            "num_steps": getattr(prm, "num_steps", None)}
    out[name] = info
    print(f"{name:22s} = {info['value']:>8s}  steps {str(info['num_steps']):>4s}  valid {info['valid_values']}")
if len(sys.argv) > 2:
    json.dump(out, open(sys.argv[2], "w"), indent=1)
lat = getattr(p, "reported_latency_samples", 0)
ok = len(p.parameters) == 50 and lat == 0
print("reported latency:", lat)
print("PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
