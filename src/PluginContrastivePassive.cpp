// SPDX-FileCopyrightText: 2026 Cameron Brooks
// SPDX-License-Identifier: GPL-3.0-only
//
// Contrastive Passive EQ: the DPF plugin. Holds the 50 panel parameters, maps them (with LINK and POWER) to one ChannelSetting per
// channel, and runs one ChannelEngine per channel. See src/dsp/ for the network and its discretisation, and README.md for the control table.
#include "DistrhoPlugin.hpp"
#include <cmath>
#include <cstring>
#include "ContrastivePassiveParams.hpp"
#include "dsp/Engine.hpp"

START_NAMESPACE_DISTRHO

class ContrastivePassivePlugin : public Plugin
{
public:
    ContrastivePassivePlugin()
        : Plugin(cpeq::kNumParams, 0, 0)
    {
        for (int i = 0; i < cpeq::kNumParams; ++i)
            fValues[i] = float(cpeq::paramDefault(cpeq::paramInfo(i).kind));
        fEngine[0] = new cpeq::ChannelEngine();
        fEngine[1] = new cpeq::ChannelEngine();
        fDirty = true;
    }

    ~ContrastivePassivePlugin() override
    {
        delete fEngine[0];
        delete fEngine[1];
    }

protected:
    const char* getLabel() const override { return "ContrastivePassive"; }
    const char* getDescription() const override
    {
        return "Four-band parallel passive equaliser, stereo, with every control of a stepped passive mastering EQ panel: per channel four "
               "bands (BOOST/OUT/CUT, SHELF/BELL, 16-step gain, 16-step bandwidth, 11-position frequency), IN, output gain trim, low pass "
               "and high pass; LINK and POWER. The bands share one passive divider, so boosts combine instead of stacking. A behavioural "
               "model fitted to published response curves; minimum phase, zero latency.";
    }
    const char* getMaker() const override { return "Cameron Brooks"; }
    const char* getHomePage() const override { return "https://github.com/brookcs3/contrastive-passive-eq"; }
    const char* getLicense() const override { return "GPL-3.0-only"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('C', 'P', 'e', 'q'); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& p) override
    {
        const cpeq::ParamInfo pi = cpeq::paramInfo(int(index));
        char buf[64];
        cpeq::paramName(int(index), buf, sizeof(buf));
        p.name = buf;
        p.shortName = buf;
        p.symbol = buf;
        p.unit = cpeq::paramUnit(pi.kind);
        const int steps = cpeq::paramSteps(pi.kind);
        p.hints = kParameterIsAutomatable | kParameterIsInteger;
        if (pi.kind == cpeq::kPIn || pi.kind == cpeq::kPLink || pi.kind == cpeq::kPPower)
            p.hints |= kParameterIsBoolean;
        p.ranges.min = 0.0f;
        p.ranges.max = float(steps - 1);
        p.ranges.def = float(cpeq::paramDefault(pi.kind));
        ParameterEnumerationValue* ev = new ParameterEnumerationValue[steps];
        for (int v = 0; v < steps; ++v) {
            cpeq::positionLabel(pi.kind, pi.band < 0 ? 0 : pi.band, v, buf, sizeof(buf));
            ev[v].value = float(v);
            ev[v].label = buf;
        }
        p.enumValues.count = uint8_t(steps);
        p.enumValues.restrictedMode = true;
        p.enumValues.values = ev;
        p.enumValues.deleteLater = true;
        switch (pi.kind) {
        case cpeq::kPMode: p.description = "BOOST / OUT / CUT toggle of the band (OUT is the band's hard bypass)"; break;
        case cpeq::kPType: p.description = "SHELF / BELL switch of the band (bands 1-2 low shelves, bands 3-4 high shelves)"; break;
        case cpeq::kPGainStep: p.description = "dB knob detent, 0 (fully CCW, flat) to 15 (fully CW); the dB per step depends on the bandwidth"; break;
        case cpeq::kPBandwidth: p.description = "BANDWIDTH knob detent, 0 (fully CCW, wide) to 15 (fully CW, narrow); in shelf mode it adds the opposite bell"; break;
        case cpeq::kPFreq: p.description = "FREQUENCY HZ switch, eleven positions"; break;
        case cpeq::kPIn: p.description = "channel IN switch (OFF bypasses the channel's EQ, filters and gain trim)"; break;
        case cpeq::kPTrim: p.description = "output GAIN trim, 11 positions of 0.5 dB"; break;
        case cpeq::kPLowPass: p.description = "LOW PASS selector (18 dB/oct; 52K is 30 dB/oct)"; break;
        case cpeq::kPHighPass: p.description = "HIGH PASS selector (18 dB/oct)"; break;
        case cpeq::kPLink: p.description = "LINK: the right channel follows every left-channel control"; break;
        case cpeq::kPPower: p.description = "POWER: OFF is a global bypass in this plugin (the hardware passes no audio when off)"; break;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < uint32_t(cpeq::kNumParams) ? fValues[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= uint32_t(cpeq::kNumParams)) return;
        const int steps = cpeq::paramSteps(cpeq::paramInfo(int(index)).kind);
        float v = std::round(value);
        if (v < 0.0f) v = 0.0f;
        if (v > float(steps - 1)) v = float(steps - 1);
        if (v != fValues[index]) { fValues[index] = v; fDirty = true; }
    }

    void activate() override
    {
        fEngine[0]->prepare(getSampleRate());
        fEngine[1]->prepare(getSampleRate());
        fDirty = true;
    }

    void sampleRateChanged(double newSampleRate) override
    {
        fEngine[0]->prepare(newSampleRate);
        fEngine[1]->prepare(newSampleRate);
        fDirty = true;
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const cpeq::DenormalGuard guard;
        if (fDirty) {
            fDirty = false;
            cpeq::ChannelSetting cs[2];
            buildSettings(cs);
            fEngine[0]->setTarget(cs[0]);
            fEngine[1]->setTarget(cs[1]);
        }
        fEngine[0]->process(inputs[0], outputs[0], int(frames));
        fEngine[1]->process(inputs[1], outputs[1], int(frames));
    }

private:
    int iv(int kind, int ch, int band) const { return int(fValues[cpeq::paramIndex(kind, ch, band)]); }

    void buildSettings(cpeq::ChannelSetting* cs) const
    {
        const bool power = iv(cpeq::kPPower, -1, -1) != 0;
        const bool link = iv(cpeq::kPLink, -1, -1) != 0;
        for (int ch = 0; ch < 2; ++ch) {
            const int src = (link && ch == 1) ? 0 : ch;   // LINK: the right channel takes the left channel's controls
            cpeq::ChannelSetting& c = cs[ch];
            for (int b = 0; b < cpeq::kBands; ++b) {
                c.band[b].mode = iv(cpeq::kPMode, src, b);
                c.band[b].type = iv(cpeq::kPType, src, b);
                c.band[b].gainStep = iv(cpeq::kPGainStep, src, b);
                c.band[b].bwStep = iv(cpeq::kPBandwidth, src, b);
                c.band[b].freqIdx = iv(cpeq::kPFreq, src, b);
            }
            c.eqIn = power && iv(cpeq::kPIn, src, -1) != 0;   // POWER off = global bypass
            c.trimIdx = iv(cpeq::kPTrim, src, -1);
            c.lowPassIdx = iv(cpeq::kPLowPass, src, -1);
            c.highPassIdx = iv(cpeq::kPHighPass, src, -1);
        }
    }

    float fValues[cpeq::kNumParams];
    cpeq::ChannelEngine* fEngine[2];
    bool fDirty;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContrastivePassivePlugin)
};

Plugin* createPlugin()
{
    return new ContrastivePassivePlugin();
}

END_NAMESPACE_DISTRHO
