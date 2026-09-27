# pocketfft (vendored)

`pocketfft_hdronly.h` is the header-only C++ version of pocketfft by Martin Reinecke and Peter Bell (Max-Planck-Society), BSD-3-Clause. The licence is in `LICENSE.md` (unmodified). The header keeps its own copyright notice and licence text at the top.

| item | value |
|---|---|
| upstream | https://github.com/mreineck/pocketfft, branch `cpp` |
| commit | `c90e55b3d529f8efa40ed01a20de22405f45fc65` (2026-06-30, the branch head when vendored); the header last changed in `84384cdfaeb1dd18ec77ff560f6b4db44f8db59d` |
| upstream `pocketfft_hdronly.h` sha256 | `3e9a05318d8e3b1446bda1c4617e6a103cdd23599ae0a776a92a6e8800e92fdc` |
| upstream `LICENSE.md` sha256 (this copy) | `a85ca13fdf90160b64a0698215868c13b74d835ad0a4e2ba44713b8c5058a056` |
| vendored `pocketfft_hdronly.h` sha256 (with the local change below) | `5c013e84836151dc4b79dad92f5acc7d50f65bcb97453be7d3c7abee52a89672` |

## The one local change

`local-scratch-exec.patch` (3 hunks, 5 changed and 4 added lines, all in class `cfftp`). Upstream, the member function `cfftp::exec` allocates a temporary work array of the transform's length on every call. The patch adds an overload taking a fourth argument, `buf`, that runs the same passes on a work array the caller owns. With it, a transform whose length has only the factors 2, 3, 4, 5, 7, 8 and 11 does not allocate. `src/dsp/Design.hpp` builds its 1024-point plan once, outside the audio callback, and then calls this overload from the audio thread when a control changes.

The arithmetic is untouched: the passes, twiddles and scaling are upstream's. The one edited statement, `c[i] = ch[i]*fct` becoming `c[i] = p1[i]*fct`, reads the same memory in the upstream path (there `p1` is `ch.data()` whenever that branch runs). The original three-argument overload is unchanged. The change is offered under the same BSD-3-Clause terms as the file it modifies, so `pocketfft_hdronly.h` stays a BSD-3-Clause file inside this GPL-3.0 project.

To re-vendor: copy `pocketfft_hdronly.h` and `LICENSE.md` from the upstream commit, apply `local-scratch-exec.patch` (`patch -p1 < local-scratch-exec.patch` in this folder), and check the sha256 values above.

## Configuration used

`POCKETFFT_NO_MULTITHREADING` is defined before the include (one-dimensional transforms only, no thread pool). The plan cache stays at its default (off). Only `pocketfft::detail::cfftp<double>` is used: complex transforms, forward `exp(-j 2 pi k n / N)` unscaled, inverse `exp(+j 2 pi k n / N)` scaled by 1/N.

## Redistribution

BSD-3-Clause asks that source redistributions keep the notice, and that binary redistributions reproduce the copyright notice, the conditions and the disclaimer in their documentation or other materials. The complete notice is the comment block at the top of `pocketfft_hdronly.h`: it names more copyright holders (Max-Planck-Society 2010-2024, Peter Bell, and the holders of parts not compiled here: Matteo Frigo and MIT for the odd-sized DCT-IV, Tan Ping Liang and Peter Bell, Cris Luengo) than the single line in `LICENSE.md` (Max-Planck-Society 2010-2018). So every prebuilt bundle carries a notices file that reproduces that header block in full: `THIRD_PARTY_NOTICES.md` at the repository root, copied into `ContrastivePassive.vst3/Contents/Resources/` by `scripts/package.py`.
