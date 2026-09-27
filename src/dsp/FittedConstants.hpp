// SPDX-FileCopyrightText: 2026 Cameron Brooks
// SPDX-License-Identifier: GPL-3.0-only
//
// The fitted constants of the passive network, in normalised impedance units: the boost divider's flat series conductance is 1.
// They come from fitting the network to the response curves printed in the modelled unit's owner's manual; the fitting scripts and
// data are not part of this repository. tests/ref_model.py reads this file, so the C++ plugin and the Python reference share one copy.
#pragma once
namespace cpeq { namespace K {
static constexpr double p = 67.07375571687305;  // boost divider flat shunt conductance (fitted to the printed 100 Hz bell curves)
static constexpr double Rp = 2.4748587818228702;  // gain pot source-resistance scale, a(1 - a) Rp
static constexpr double X0 = 0.31591395414889833;  // bell reactance, every band (the printed 4.7 kHz band-3 bell has the same reactance as band 1's 100 Hz bell: ratio 0.999)
static constexpr double gain_taper_b = 18.87049526715099;  // reverse-log gain pot taper through a(12:00) = a12
static constexpr double a12 = 0.8128748934025019;  // divider position at the dB knob's 12:00
static constexpr double a_max_mastering = 0.8935156754127167;  // top of the stepped gain pot: the narrowest bell boosts 11 dB there
static constexpr double bell_boost_R[3] = { 0.9733287977615904, 0.3178213274524729, 0.09690665182352179 };  // [CCW wide, 12:00, CW narrow]
static constexpr double bell_cut_R[3] = { 0.9489428904517719, 0.2942624327834011, 0.07634638894247782 };  // [CCW wide, 12:00, CW narrow]
static constexpr double m_cut = 0.8057895561201847;  // cut divider branch scale for bells and shelf dips (fitted to the printed cut curves)
static constexpr double hs_Rs = 0.09390678721994583;  // high shelf R-C branch
static constexpr double hs_Xs = 0.21761858937906164;  // high shelf R-C branch
static constexpr double ls_Rs = 0.09439041793662852;  // low shelf R-L branch
static constexpr double ls_Xs = 0.2188655474334816;  // low shelf R-L branch
static constexpr double hs_dip_R[3] = { 2.2132480264526087, 0.2048725463077732, 0.07508800744371406 };  // [CCW wide, 12:00, CW narrow]
static constexpr double ls_dip_R[3] = { 2.202440838147085, 0.2478439283809118, 0.07684184867342492 };  // [CCW wide, 12:00, CW narrow]
static constexpr double bump_dR = 0.02056026288104397;  // boost-side minus cut-side bell resistance, added to the shelf-cut bump
static constexpr double m_cut_shelf = 1.0;  // cut divider scale for shelf branches
static constexpr double sp22_Rs = 0.09438908513503587;  // 22 Hz special shelf
static constexpr double sp22_Xs = 0.25950181286287854;  // 22 Hz special shelf
static constexpr double sp33_Rs = 0.09369677919832212;  // 33 Hz special shelf
static constexpr double sp33_Xs = 0.27479205714695687;  // 33 Hz special shelf
static constexpr double sp_K[3] = { 0.0, 0.26376998797410184, 0.24255469502309332 };  // [CCW wide, 12:00, CW narrow]
static constexpr double sp_Rx[3] = { 0.0, 3.232485882199915e-20, 0.047682544261626915 };  // [CCW wide, 12:00, CW narrow]
static constexpr double sp_Gh[3] = { 0.0, 0.43597175561622115, 2.6481162411329464 };  // [CCW wide, 12:00, CW narrow]
static constexpr double sp_Xh = 0.5499698535926988;  // 22/33 Hz bandwidth low-cut branch reactance
static constexpr double air_dip_hz = 8000.0;  // 16K/27K shelf dip frequency (these two positions put their dip near 8 kHz)
static constexpr double air_lpf_hz = 50000.0;  // 16K/27K shelf branch 50 kHz low pass, realised as a series L in the branch
static constexpr double filter_ripple_db = 0.14;  // Chebyshev type I ripple, fitted to the flattest printed high passes
} }  // namespace cpeq::K
