# Third-party notices

Contrastive Passive EQ is licensed under GPL-3.0-only (see `LICENSE`). It contains, or is compiled together with, the third-party code
below. Each piece keeps its own licence, and each of those licences is compatible with GPL-3.0. The release zips carry this file and
`LICENSE` inside the bundle, in `ContrastivePassive.vst3/Contents/Resources/`, because the ISC and BSD-3-Clause licences ask that binary
copies reproduce their notices.

## 1. DPF, the DISTRHO Plugin Framework (ISC)

The plugin is built on DPF, https://github.com/DISTRHO/DPF, at commit `4238e1c7f0351bbe488d79f0899c540543ac7583` (fetched by
`scripts/build.sh`; it is not stored in this repository). DPF's plugin wrapper and its VST3 interface headers ("travesty", by the same
author; no Steinberg SDK is used) are compiled into the plugin binary. DPF asks to be credited in attribution.

```
Copyright (C) 2012-2025 Filipe Coelho <falktx@falktx.com>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
```

### Parts of DPF based on juce-core (ISC)

Two DPF headers compiled into the binary, `distrho/extra/LeakDetector.hpp` and `distrho/extra/ScopedPointer.hpp`, are based on juce-core
classes and carry this notice:

```
Copyright (C) 2013 Raw Material Software Ltd.

Permission is granted to use this software under the terms of the ISC license
http://www.isc.org/downloads/software-support-policy/isc-license/

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH REGARD
TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS. IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT,
OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
OF THIS SOFTWARE.
```

DPF's `distrho/extra/String.hpp` also carries base64 code credited to Rene Nyffenegger. This plugin does not use it, and it is not in the
binaries.

## 2. pocketfft (BSD-3-Clause)

`src/third_party/pocketfft/pocketfft_hdronly.h` is the header-only C++ pocketfft by Martin Reinecke and Peter Bell,
https://github.com/mreineck/pocketfft (branch `cpp`, commit `c90e55b3d529f8efa40ed01a20de22405f45fc65`), compiled into the plugin for the
FFTs of its filter design. It carries one small local change, described in `src/third_party/pocketfft/README.md` and offered under the same
BSD-3-Clause terms. Its complete notice, from the top of the header:

```
This file is part of pocketfft.

Copyright (C) 2010-2024 Max-Planck-Society
Copyright (C) 2019-2020 Peter Bell

For the odd-sized DCT-IV transforms:
  Copyright (C) 2003, 2007-14 Matteo Frigo
  Copyright (C) 2003, 2007-14 Massachusetts Institute of Technology

For the prev_good_size search:
  Copyright (C) 2024 Tan Ping Liang, Peter Bell

For the safeguards against integer overflow in good_size search:
  Copyright (C) 2024 Cris Luengo

Authors: Martin Reinecke, Peter Bell

All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.
* Neither the name of the copyright holder nor the names of its contributors may
  be used to endorse or promote products derived from this software without
  specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## 3. JAMA (public domain)

`src/dsp/EigenReal.hpp` is a C++ translation of the eigenvalue path (the methods `orthes` and `hqr2`) of JAMA 1.0.3, the Java Matrix
Package, class `Jama.EigenvalueDecomposition`, https://math.nist.gov/javanumerics/jama/. JAMA was developed by Joe Hicklin, Cleve Moler
and Peter Webb (The MathWorks) and Ronald F. Boisvert, Bruce Miller, Roldan Pozo and Karin Remington (NIST). Its copyright notice:

> This software is a cooperative product of The MathWorks and the National Institute of Standards and Technology (NIST) which has been
> released to the public domain. Neither The MathWorks nor NIST assumes any responsibility whatsoever for its use by other parties, and
> makes no guarantees, expressed or implied, about its quality, reliability, or any other characteristic.

JAMA derives these methods from the Algol procedures of Martin and Wilkinson (Handbook for Automatic Computation, Vol. II, Linear Algebra)
and the corresponding EISPACK Fortran subroutines.

## 4. Not distributed

- The plugin links dynamically against the system's libstdc++ and libgcc_s (GPL-3.0 with the GCC Runtime Library Exception) and glibc
  (LGPL-2.1-or-later). They are not part of the release zips.
- The tests use Pedalboard (GPL-3.0), numpy and scipy (BSD-3-Clause), installed from PyPI; none of them is part of the plugin.
