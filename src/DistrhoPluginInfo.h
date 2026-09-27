// SPDX-FileCopyrightText: 2026 Cameron Brooks
// SPDX-License-Identifier: GPL-3.0-only
//
// Contrastive Passive EQ: a four-band parallel passive equaliser, stereo. DPF plugin description.
// The VST3 class id is { 'DPF ', 'clas', getUniqueId() = 'CPeq', DISTRHO_PLUGIN_BRAND_ID = 'Cbrk' } (DPF builds it from these two).
#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "Cameron Brooks"
#define DISTRHO_PLUGIN_NAME    "Contrastive Passive EQ"
#define DISTRHO_PLUGIN_URI     "https://github.com/brookcs3/contrastive-passive-eq"
#define DISTRHO_PLUGIN_CLAP_ID "io.github.brookcs3.contrastive-passive-eq"

#define DISTRHO_PLUGIN_BRAND_ID  Cbrk
#define DISTRHO_PLUGIN_UNIQUE_ID CPeq

#define DISTRHO_PLUGIN_HAS_UI        0
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_STATE    0

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
