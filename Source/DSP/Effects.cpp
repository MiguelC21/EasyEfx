#include "Effects.h"

namespace EasyDSP
{

namespace
{
    constexpr float kPi    = juce::MathConstants<float>::pi;
    constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

    // Rhythm tables (in beats, 1 beat = 1/4 note)
    const float kShutterBeats[] = { 2.0f, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f, 1.0f / 12.0f, 0.0625f };
    const char* kShutterNames[] = { "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
    constexpr int kShutterSteps = 9;

    const float kPanBeats[] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };
    const char* kPanNames[] = { "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
    constexpr int kPanSteps = 7;

    // Free-running rate ranges (Hz)
    inline float shutterFreeHz(float v) { return 0.5f * std::pow(300.0f, v); }   // 0.5 .. 150 Hz
    inline float panFreeHz(float v)     { return 0.1f * std::pow(500.0f, v); }   // 0.1 .. 50 Hz

    const float kRewindBeats[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    const char* kRewindNames[] = { "1/16", "1/8", "1/4", "1/2", "1 bar" };

    const float kDelayBeats[] = { 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 1.0f, 2.0f };

    struct Ch
    {
        float* p[2] = { nullptr, nullptr };
        int n = 0;
    };

    inline Ch getCh(juce::AudioBuffer<float>& b)
    {
        Ch c;
        c.n = juce::jmin(2, b.getNumChannels());
        for (int i = 0; i < c.n; ++i) c.p[i] = b.getWritePointer(i);
        return c;
    }

    inline float onePole(float hz, double sr) { return 1.0f - std::exp(-kTwoPi * hz / static_cast<float>(sr)); }

    inline float hermite(const float* buf, int size, float pos) noexcept
    {
        const int i1 = static_cast<int>(pos);
        const float f = pos - static_cast<float>(i1);
        const int i0 = (i1 - 1 + size) % size;
        const int i2 = (i1 + 1) % size;
        const int i3 = (i1 + 2) % size;
        const float y0 = buf[i0], y1 = buf[i1], y2 = buf[i2], y3 = buf[i3];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    inline double logcosh(double x) noexcept
    {
        x = std::abs(x);
        return x + std::log1p(std::exp(-2.0 * x)) - 0.6931471805599453;
    }

    // First-order antiderivative anti-aliased waveshapers
    inline float adaa(int model, float xf, float xpf) noexcept
    {
        const double x = xf, xp = xpf, dx = x - xp;
        const bool small = std::abs(dx) < 1.0e-5;
        switch (model)
        {
            case 0: // tape: tanh
                if (small) return static_cast<float>(std::tanh(0.5 * (x + xp)));
                return static_cast<float>((logcosh(x) - logcosh(xp)) / dx);
            case 1: // tube: asymmetric tanh (even harmonics), DC removed afterwards
            {
                const double b = 0.3, tb = std::tanh(b);
                if (small) return static_cast<float>(std::tanh(0.5 * (x + xp) + b) - tb);
                return static_cast<float>((logcosh(x + b) - logcosh(xp + b)) / dx - tb);
            }
            default: // hard clip
            {
                auto F = [](double v) { const double a = std::abs(v); return a <= 1.0 ? 0.5 * v * v : a - 0.5; };
                if (small) return static_cast<float>(juce::jlimit(-1.0, 1.0, 0.5 * (x + xp)));
                return static_cast<float>((F(x) - F(xp)) / dx);
            }
        }
    }

    juce::String hzText(float f)
    {
        return f >= 1000.0f ? juce::String(f / 1000.0f, 1) + " kHz" : juce::String(juce::roundToInt(f)) + " Hz";
    }
}

// =========================================================================================
// Effect catalogue
// =========================================================================================
static const std::vector<EffectInfo>& infoTable()
{
    static const std::vector<EffectInfo> t =
    {
        {
            EffectType::FilterLP, "LP FILTER", "FILTER", "Cutoff", "Reso", 0.20f, 0xff2ee6ff,
            "Low-pass filter. Maximum = open (off). Turn down to close the filter.",
            { { "Slope", 0.0f, 1.0f, 1.0f, "", { "12 dB", "24 dB" } } },
            1.0f
        },
        {
            EffectType::FilterHP, "HP FILTER", "FILTER", "Cutoff", "Reso", 0.20f, 0xff4ec9ff,
            "High-pass filter. Minimum = off. Turn up to thin out the sound.",
            { { "Slope", 0.0f, 1.0f, 1.0f, "", { "12 dB", "24 dB" } } }
        },
        {
            EffectType::Saturation, "SATURATE", "COLOR", "Drive", "Tone", 0.75f, 0xffffa347,
            "Analog-style saturation. Minimum = clean.",
            {
                { "Model", 0.0f, 2.0f, 0.0f, "", { "Tape", "Tube", "Hard Clip" } },
                { "Output", -12.0f, 6.0f, 0.0f, "dB" }
            }
        },
        {
            EffectType::Bitcrusher, "BITCRUSH", "COLOR", "Crush", "Tone", 0.80f, 0xffff7849,
            "Lo-fi bit and sample-rate reduction. Minimum = clean.",
            {
                { "Min Bits", 2.0f, 12.0f, 4.0f, "bits" },
                { "Max Downsample", 4.0f, 64.0f, 24.0f, "x", {}, 0.5f }
            }
        },
        {
            EffectType::Chorus, "CHORUS", "MOD", "Amount", "Rate", 0.45f, 0xffb68cff,
            "Lush stereo chorus. Minimum = off.",
            {
                { "Depth", 0.5f, 8.0f, 3.0f, "ms" },
                { "Width", 0.0f, 1.0f, 0.8f, "%" }
            }
        },
        {
            EffectType::Phaser, "PHASER", "MOD", "Amount", "Rate", 0.40f, 0xffd78cff,
            "Sweeping all-pass phaser. Minimum = off.",
            {
                { "Stages", 0.0f, 2.0f, 1.0f, "", { "4", "6", "8" } },
                { "Feedback", 0.0f, 0.9f, 0.55f, "%" },
                { "Spread", 0.0f, 1.0f, 0.5f, "%" }
            }
        },
        {
            EffectType::Disperser, "DISPERSER", "PHASE", "Amount", "Freq", 0.45f, 0xffd0f04a,
            "Phase disperser: smears transients into zappy, falling-pitch sweeps. Minimum = off.",
            {
                { "Spread", 0.0f, 1.0f, 0.35f, "%" },
                { "Feedback", 0.0f, 0.8f, 0.0f, "%" },
                { "Mix", 0.0f, 1.0f, 1.0f, "%" }
            }
        },
        {
            EffectType::Rewind, "REWIND", "GLITCH", "Mix", "Slice", 0.40f, 0xffff4ad8,
            "Plays the last slice of sound backwards, in time with the tempo. Slice = length. Minimum = off.",
            {
                { "Crossfade", 2.0f, 60.0f, 12.0f, "ms", {}, 0.6f },
                { "Feedback", 0.0f, 0.85f, 0.0f, "%" }
            }
        },
        {
            EffectType::Shutter, "SHUTTER", "RHYTHM", "Speed", "Depth", 0.60f, 0xffff5c93,
            "Rhythmic volume chop (also chops the delay and reverb tails). Minimum = off.",
            {
                { "Sync", 0.0f, 1.0f, 0.0f, "", { "Free", "Tempo" } },
                { "Shape", 0.0f, 2.0f, 2.0f, "", { "Sine", "Triangle", "Square" } },
                { "Duty", 0.1f, 0.9f, 0.5f, "%" }
            }
        },
        {
            EffectType::AutoPan, "AUTOPAN", "RHYTHM", "Depth", "Rate", 0.50f, 0xffff7aa8,
            "Pans the sound left and right. Minimum = off.",
            {
                { "Sync", 0.0f, 1.0f, 1.0f, "", { "Free", "Tempo" } },
                { "Shape", 0.0f, 1.0f, 0.0f, "", { "Sine", "Triangle" } }
            }
        },
        {
            EffectType::Particles, "PARTICLES", "GRAIN", "Amount", "Size", 0.45f, 0xff7dff6a,
            "Granular cloud with pitch (fixed or random), reverse, plus its own delay and reverb. Minimum = off.",
            {
                { "Density", 4.0f, 80.0f, 24.0f, "/s", {}, 0.6f },
                { "Pitch", -24.0f, 24.0f, 0.0f, "st" },
                { "Pitch Random", 0.0f, 24.0f, 0.0f, "rand" },
                { "Reverse", 0.0f, 1.0f, 0.25f, "%" },
                { "Spread", 0.0f, 1.0f, 0.8f, "%" },
                { "Freeze", 0.0f, 1.0f, 0.0f, "", { "Off", "On" } },
                { "Delay", 0.0f, 1.0f, 0.25f, "%" },
                { "Delay Time", 0.0f, 4.0f, 1.0f, "", { "1/16", "1/8T", "1/8", "1/8D", "1/4" } },
                { "Reverb", 0.0f, 1.0f, 0.25f, "%" }
            }
        },
        {
            EffectType::TapeStop, "TAPE STOP", "RHYTHM", "Brake", "Time", 0.70f, 0xffff6e5c,
            "Slows the tape down. Maximum = full stop, back to minimum = spin up.",
            {
                { "Curve", 0.5f, 3.0f, 1.5f, "" },
                { "Filter Drop", 0.0f, 1.0f, 0.7f, "%" },
                { "Return Time", 0.0f, 2.0f, 0.3f, "s", {}, 0.5f }
            }
        },
        {
            EffectType::BuildUp, "BUILD UP", "RISER", "Build", "Curve", 0.50f, 0xfffff176,
            "One knob thins your sound with filters, narrower stereo, delay and reverb. Pick a preset, automate Build up.",
            {
                { "Preset", 0.0f, 5.0f, 0.0f, "", { "Classic", "Water", "Phone", "Tunnel", "Wash", "Choke" } },
                { "High Pass", 0.0f, 1.0f, 1.0f, "%" },
                { "Low Pass", 0.0f, 1.0f, 1.0f, "%" },
                { "Stereo", 0.0f, 1.0f, 1.0f, "%" },
                { "Delay", 0.0f, 1.0f, 1.0f, "%" },
                { "Reverb", 0.0f, 1.0f, 1.0f, "%" }
            }
        },
        {
            EffectType::Delay, "DELAY", "SPACE", "Mix", "Feedback", 0.40f, 0xff6ea8ff,
            "Tempo-synced echo. Minimum = off.",
            {
                { "Time", 0.0f, 5.0f, 3.0f, "", { "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/2" } },
                { "Mode", 0.0f, 1.0f, 1.0f, "", { "Stereo", "Ping-Pong" } },
                { "Low Cut", 20.0f, 2000.0f, 20.0f, "Hz", {}, 0.3f },
                { "High Cut", 800.0f, 20000.0f, 8000.0f, "Hz", {}, 0.4f },
                { "Width", 0.0f, 2.0f, 1.0f, "%" }
            }
        },
        {
            EffectType::Reverb, "REVERB", "SPACE", "Mix", "Decay", 0.60f, 0xff8a8cff,
            "Room, hall or plate reverb. At 100% it is almost only reverb. Minimum = off.",
            {
                { "Type", 0.0f, 2.0f, 1.0f, "", { "Room", "Hall", "Plate" } },
                { "Pre-Delay", 0.0f, 1000.0f, 12.0f, "ms", {}, 0.4f },
                { "Damping", 0.0f, 1.0f, 0.5f, "%" },
                { "Low Cut", 20.0f, 2000.0f, 120.0f, "Hz", {}, 0.3f },
                { "High Cut", 800.0f, 20000.0f, 12000.0f, "Hz", {}, 0.4f },
                { "Modulation", 0.0f, 1.0f, 0.5f, "%" },
                { "Width", 0.0f, 2.0f, 1.0f, "%" }
            }
        },
        {
            EffectType::Widener, "STEREO", "WIDTH", "Width", "Bass", 0.56f, 0xff3de0c8,
            "Centre = off. Turn left to make the sound mono, right to widen it. Below the Bass frequency nothing changes.",
            {
                { "Widen Mode", 0.0f, 1.0f, 1.0f, "", { "Wide", "Haas" } },
                { "Haas Time", 1.0f, 40.0f, 18.0f, "ms" },
                { "Haas Side", 0.0f, 1.0f, 1.0f, "", { "Left", "Right" } }
            },
            0.5f
        },
        {
            EffectType::Pitch, "PITCH", "PITCH", "Pitch", "Mix", 1.0f, 0xffffc233,
            "Shifts the pitch up or down, free or in whole semitones. Centre = off. Range and grain in settings.",
            {
                { "Mode", 0.0f, 1.0f, 0.0f, "", { "Free", "Semitones" } },
                { "Range", 0.0f, 1.0f, 0.0f, "", { "+/-12 st", "+/-24 st" } },
                { "Grain", 0.0f, 2.0f, 1.0f, "", { "Fast", "Normal", "Smooth" } }
            },
            0.5f
        }
    };
    return t;
}

const EffectInfo& getEffectInfo(int index)
{
    jassert(index >= 0 && index < kNumEffects);
    return infoTable()[static_cast<size_t>(juce::jlimit(0, kNumEffects - 1, index))];
}

juce::String formatParamValue(const ParamConfig& cfg, float v)
{
    if (! cfg.options.isEmpty())
        return cfg.options[juce::jlimit(0, cfg.options.size() - 1, juce::roundToInt(v))];

    if (cfg.unit == "%")  return juce::String(juce::roundToInt(v * 100.0f)) + "%";
    if (cfg.unit == "Hz") return hzText(v);
    if (cfg.unit == "ms") return v >= 995.0f ? juce::String("1.00 s")
                                : (v >= 100.0f ? juce::String(juce::roundToInt(v)) + " ms" : juce::String(v, 1) + " ms");
    if (cfg.unit == "dB") return juce::String(v, 1) + " dB";
    if (cfg.unit == "st") return juce::String(v, 1) + " st";
    if (cfg.unit == "rand") return v < 0.05f ? juce::String("Off") : "Rand +/-" + juce::String(juce::roundToInt(v)) + " st";
    if (cfg.unit == "/s") return juce::String(juce::roundToInt(v)) + "/s";
    if (cfg.unit == "s")  return v < 0.005f ? juce::String("Instant") : juce::String(v, 2) + " s";
    if (cfg.unit == "bits") return juce::String(v, 1) + " bits";
    if (cfg.unit == "x")  return juce::String(juce::roundToInt(v)) + "x";
    return juce::String(v, 2);
}

juce::String formatMacroValue(int fx, int which, float v, const float* sub)
{
    // "Off" is the minimum for most effects, the maximum for the low-pass and the centre for pitch
    if (which == 1 && std::abs(v - getEffectInfo(fx).macro1Off) < 0.002f)
        return "OFF";

    auto pct = [](float x) { return juce::String(juce::roundToInt(x * 100.0f)) + "%"; };
    auto subv = [&](int i, float def) { return sub != nullptr ? sub[i] : def; };

    switch (static_cast<EffectType>(fx))
    {
        case EffectType::FilterLP:
            return which == 1 ? hzText(20000.0f * std::pow(110.0f / 20000.0f, 1.0f - v)) : pct(v);
        case EffectType::FilterHP:
            return which == 1 ? hzText(20.0f * std::pow(250.0f, v)) : pct(v);

        case EffectType::Chorus:
            return which == 1 ? pct(v) : juce::String(0.08f * std::pow(60.0f, v), 1) + " Hz";
        case EffectType::Phaser:
            return which == 1 ? pct(v) : juce::String(0.05f * std::pow(160.0f, v), 1) + " Hz";

        case EffectType::Shutter:
            if (which == 1)
            {
                if (subv(0, 0.0f) > 0.5f)
                    return kShutterNames[juce::jlimit(0, kShutterSteps - 1, static_cast<int>(v * static_cast<float>(kShutterSteps)))];
                const float f = shutterFreeHz(v);
                return (f < 10.0f ? juce::String(f, 1) : juce::String(juce::roundToInt(f))) + " Hz";
            }
            return pct(v);

        case EffectType::AutoPan:
            if (which == 2)
            {
                if (subv(0, 0.0f) > 0.5f)
                    return kPanNames[juce::jlimit(0, kPanSteps - 1, static_cast<int>(v * static_cast<float>(kPanSteps)))];
                const float f = panFreeHz(v);
                return (f < 10.0f ? juce::String(f, 1) : juce::String(juce::roundToInt(f))) + " Hz";
            }
            return pct(v);

        case EffectType::Particles:
            return which == 1 ? pct(v) : juce::String(juce::roundToInt(20.0f * std::pow(20.0f, v))) + " ms";

        case EffectType::Disperser:
            return which == 1 ? pct(v) : hzText(40.0f * std::pow(200.0f, v));

        case EffectType::Widener:
            if (which == 1)
            {
                const float a = std::abs(v - 0.5f) * 2.0f;
                if (v < 0.5f) return "Mono " + pct(a);
                return (subv(0, 1.0f) > 0.5f ? "Haas " : "Wide ") + pct(a);
            }
            return hzText(20.0f * std::pow(25.0f, v));

        case EffectType::Pitch:
            if (which == 1)
            {
                const bool semitones = subv(0, 0.0f) > 0.5f;
                const float range = subv(1, 0.0f) > 0.5f ? 24.0f : 12.0f;
                float st = (v - 0.5f) * 2.0f * range;
                if (semitones) st = std::round(st);
                const juce::String num = semitones ? juce::String(juce::roundToInt(st)) : juce::String(st, 2);
                return (st > 0.0f ? "+" : "") + num + " st";
            }
            return pct(v);

        case EffectType::Rewind:
            return which == 1 ? pct(v) : kRewindNames[juce::jlimit(0, 4, static_cast<int>(v * 5.0f))];

        case EffectType::TapeStop:
            return which == 1 ? pct(v) : juce::String(0.03f * std::pow(150.0f, v), 1) + " s";

        default:
            return pct(v);
    }
}

// =========================================================================================
// EffectBase
// =========================================================================================
void EffectBase::prepare(double sr, int maxBlockSize)
{
    sampleRate = sr;
    maxBlock = juce::jmax(64, maxBlockSize);
    sm1.reset(sr, 0.02);
    sm2.reset(sr, 0.03);
    sm1.setCurrentAndTargetValue(offValue()); // start from "off" (max for the low-pass)
    running = false;
    matcher.reset();
    matchEngage = 0.0f;
    onPrepare();
}

void EffectBase::reset()
{
    onReset();
    matcher.reset();
    matchEngage = 0.0f;
}

namespace
{
    double meanPower(const juce::AudioBuffer<float>& b)
    {
        const int n = b.getNumSamples(), chs = b.getNumChannels();
        if (n == 0 || chs == 0) return 0.0;
        double sum = 0.0;
        for (int ch = 0; ch < chs; ++ch)
        {
            const float* d = b.getReadPointer(ch);
            for (int i = 0; i < n; ++i) sum += static_cast<double>(d[i]) * d[i];
        }
        return sum / static_cast<double>(n * chs);
    }
}

void EffectBase::processBlock(juce::AudioBuffer<float>& buffer)
{
    const bool run = needsContinuousProcessing()
                  || ! isOffValue(sm1.getTargetValue())
                  || ! isOffValue(sm1.getCurrentValue());

    if (! run)
    {
        if (running)
        {
            onReset();
            sm1.setCurrentAndTargetValue(offValue());
            running = false;
        }
        matcher.reset();
        matchEngage = 0.0f;
        return;
    }

    running = true;

    // Nothing to compensate: plain processing (this is the normal case)
    if (! levelMatch && matcher.gain == 1.0f)
    {
        process(buffer);
        return;
    }

    // ---- Level match: keep this effect's output as loud as its input ----
    const int ns = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    const double powerIn = levelMatch ? meanPower(buffer) : 0.0;
    const bool knobMoving = sm1.isSmoothing();      // the main knob is changing during this block

    process(buffer);

    float newGain = 1.0f;
    float startGain = matcher.gain;
    if (levelMatch && ns > 0)
    {
        newGain = matcher.update(powerIn, meanPower(buffer), ns, sampleRate, knobMoving, startGain);
    }
    else
    {
        matcher.energyIn = matcher.energyOut = 0.0;     // switched off: glide back to unity
        matcher.samples = 0.0;
    }

    // The compensation fades with how engaged the effect is, so turning the knob back to "off"
    // never leaves a leftover boost (no volume blast when an effect is released).
    const float newEngage = juce::jlimit(0.0f, 1.0f, std::abs(sm1.getCurrentValue() - offValue()) * 20.0f);

    if (ns > 0)
    {
        for (int i = 0; i < ns; ++i)
        {
            const float t = static_cast<float>(i + 1) / static_cast<float>(ns);
            const float g = startGain + (newGain - startGain) * t;
            const float e = matchEngage + (newEngage - matchEngage) * t;
            const float k = 1.0f + (g - 1.0f) * e;
            for (int ch = 0; ch < nch; ++ch)
                buffer.getWritePointer(ch)[i] *= k;
        }
    }

    matcher.gain = newGain;
    matchEngage = newEngage;
}

// =========================================================================================
// 1/2. Filter
// =========================================================================================
void FXFilter::onPrepare() { onReset(); }

void FXFilter::onReset()
{
    for (int c = 0; c < 2; ++c) { s1[c].reset(); s2[c].reset(); }
}

void FXFilter::updateCoefs(float a, float r, bool steep)
{
    // HP: knob up = higher cutoff.  LP: knob up = more open (max = off).
    const float fc = isHP ? 20.0f * std::pow(250.0f, a)
                          : 20000.0f * std::pow(110.0f / 20000.0f, 1.0f - a);
    const float q = 0.707f + r * r * 7.0f;
    for (int c = 0; c < 2; ++c)
    {
        s1[c].set(fc, q, sampleRate);
        if (steep) s2[c].set(fc, 0.707f, sampleRate);
    }
}

void FXFilter::process(juce::AudioBuffer<float>& buffer)
{
    auto c = getCh(buffer);
    const int ns = buffer.getNumSamples();
    const bool steep = subParams[0] > 0.5f;

    for (int i = 0; i < ns; ++i)
    {
        const float a = sm1.getNextValue();
        const float r = sm2.getNextValue();
        if ((i & 15) == 0) updateCoefs(a, r, steep);

        for (int ch = 0; ch < c.n; ++ch)
        {
            float lp, hp;
            s1[ch].tick(c.p[ch][i], lp, hp);
            float y = isHP ? hp : lp;
            if (steep)
            {
                s2[ch].tick(y, lp, hp);
                y = isHP ? hp : lp;
            }
            c.p[ch][i] = y;
        }
    }
}

// =========================================================================================
// 3. Saturation
// =========================================================================================
void FXSaturation::onPrepare() { onReset(); }

void FXSaturation::onReset()
{
    for (int c = 0; c < 2; ++c) { xPrev[c] = dcX[c] = dcY[c] = toneState[c] = 0.0f; }
}

void FXSaturation::process(juce::AudioBuffer<float>& buffer)
{
    auto c = getCh(buffer);
    const int ns = buffer.getNumSamples();
    const int model = juce::jlimit(0, 2, juce::roundToInt(subParams[0]));
    const float outGain = juce::Decibels::decibelsToGain(subParams[1]);

    for (int i = 0; i < ns; ++i)
    {
        const float a = sm1.getNextValue();
        const float t = sm2.getNextValue();
        if ((i & 15) == 0) toneA = onePole(2000.0f * std::pow(10.0f, t), sampleRate);

        const float drive = std::exp(a * 3.4539f);           // 0..30 dB
        const float comp = 1.0f / std::sqrt(drive);
        const float w = juce::jmin(1.0f, a * 10.0f);

        for (int ch = 0; ch < c.n; ++ch)
        {
            const float dry = c.p[ch][i];
            const float xin = dry * drive;
            float y = adaa(model, xin, xPrev[ch]);
            xPrev[ch] = xin;

            const float dc = y - dcX[ch] + 0.9995f * dcY[ch];
            dcX[ch] = y; dcY[ch] = dc;

            toneState[ch] += toneA * (dc * comp - toneState[ch]);
            c.p[ch][i] = dry * (1.0f - w) + toneState[ch] * outGain * w;
        }
    }
}

// =========================================================================================
// 4. Bitcrusher
// =========================================================================================
void FXBitcrusher::onPrepare() { onReset(); }

void FXBitcrusher::onReset()
{
    counter = 0.0f;
    for (int c = 0; c < 2; ++c) { hold[c] = 0.0f; lpState[c] = 0.0f; }
}

void FXBitcrusher::process(juce::AudioBuffer<float>& buffer)
{
    auto c = getCh(buffer);
    const int ns = buffer.getNumSamples();
    const float minBits = subParams[0];
    const float maxDs = subParams[1];

    for (int i = 0; i < ns; ++i)
    {
        const float a = sm1.getNextValue();
        const float t = sm2.getNextValue();
        if ((i & 15) == 0) toneA = onePole(2000.0f * std::pow(10.0f, t), sampleRate);

        const float bits = 16.0f - a * (16.0f - minBits);
        const float levels = std::exp2(bits - 1.0f);
        const float factor = 1.0f + std::pow(a, 1.5f) * (maxDs - 1.0f);
        const float w = juce::jmin(1.0f, a * 10.0f);

        counter += 1.0f;
        const bool update = counter >= factor;
        if (update) counter = juce::jmax(0.0f, counter - factor);

        for (int ch = 0; ch < c.n; ++ch)
        {
            const float x = c.p[ch][i];
            if (update) hold[ch] = std::round(x * levels) / levels;
            lpState[ch] += toneA * (hold[ch] - lpState[ch]);
            c.p[ch][i] = x * (1.0f - w) + lpState[ch] * w;
        }
    }
}

// =========================================================================================
// 5. Chorus
// =========================================================================================
void FXChorus::onPrepare()
{
    size = static_cast<int>(sampleRate * 0.06) + 8;
    for (auto& b : buf) b.assign(static_cast<size_t>(size), 0.0f);
    onReset();
}

void FXChorus::onReset()
{
    for (auto& b : buf) std::fill(b.begin(), b.end(), 0.0f);
    w = 0; phase = 0.0f;
}

void FXChorus::process(juce::AudioBuffer<float>& buffer)
{
    auto c = getCh(buffer);
    const int ns = buffer.getNumSamples();
    const float depthMs = subParams[0];
    const float width = subParams[1];
    const float msToSmp = static_cast<float>(sampleRate) * 0.001f;

    for (int i = 0; i < ns; ++i)
    {
        const float a = sm1.getNextValue();
        const float r = sm2.getNextValue();
        if ((i & 15) == 0) inc = kTwoPi * 0.08f * std::pow(60.0f, r) / static_cast<float>(sampleRate);
        phase += inc;
        if (phase >= kTwoPi) phase -= kTwoPi;

        for (int ch = 0; ch < c.n; ++ch)
        {
            const float x = c.p[ch][i];
            buf[ch][static_cast<size_t>(w)] = x;

            const float ph = phase + (ch == 1 ? width * kPi * 0.5f : 0.0f);
            const float dA = (11.0f + depthMs * std::sin(ph)) * msToSmp;
            const float dB = (16.0f + depthMs * std::sin(ph + 2.1f)) * msToSmp;

            float pA = static_cast<float>(w) - dA; if (pA < 0.0f) pA += static_cast<float>(size);
            float pB = static_cast<float>(w) - dB; if (pB < 0.0f) pB += static_cast<float>(size);

            const float wet = 0.5f * (hermite(buf[ch].data(), size, pA) + hermite(buf[ch].data(), size, pB));
            c.p[ch][i] = x * (1.0f - 0.3f * a) + wet * 0.85f * a;
        }
        if (++w >= size) w = 0;
    }
}

// =========================================================================================
// 6. Phaser
// =========================================================================================
void FXPhaser::onPrepare() { onReset(); }

void FXPhaser::onReset()
{
    for (auto& ch : z) for (auto& v : ch) v = 0.0f;
    fbState[0] = fbState[1] = 0.0f;
    phase = 0.0f;
}

void FXPhaser::process(juce::AudioBuffer<float>& buffer)
{
    auto c = getCh(buffer);
    const int ns = buffer.getNumSamples();
    const int stages = 4 + 2 * juce::jlimit(0, 2, juce::roundToInt(subParams[0]));
    const float fbAmt = subParams[1];
    const float spread = subParams[2] * kPi * 0.5f;
    const float srf = static_cast<float>(sampleRate);

    for (int i = 0; i < ns; ++i)
    {
        const float a = sm1.getNextValue();
        const float r = sm2.getNextValue();
        if ((i & 15) == 0) inc = kTwoPi * 0.05f * std::pow(160.0f, r) / srf;
        phase += inc;
        if (phase >= kTwoPi) phase -= kTwoPi;

        for (int ch = 0; ch < c.n; ++ch)
        {
            const float in = c.p[ch][i];
            const float lfo = 0.5f + 0.5f * std::sin(phase + (ch == 1 ? spread : 0.0f));
            const float fc = 250.0f * std::exp(lfo * 2.9957f); // 250 Hz .. 5 kHz
            const float tv = std::tan(kPi * fc / srf);
            const float coef = (tv - 1.0f) / (tv + 1.0f);

            float x = in + fbState[ch] * fbAmt;
            for (int s = 0; s < stages; ++s)
            {
                const float y = coef * x + z[ch][s];
                z[ch][s] = x - coef * y;
                x = y;
            }
            fbState[ch] = std::tanh(x);
            c.p[ch][i] = in * (1.0f - 0.5f * a) + x * 0.5f * a;
        }
    }
}

// =========================================================================================
// 7. Shutter
// =========================================================================================
void FXShutter::onPrepare() { onReset(); }

void FXShutter::onReset() { phase = 0.0f; gateSm = 1.0f; }

void FXShutter::process(juce::AudioBuffer<float>& buffer)
{
    auto c = getCh(buffer);
    const int ns = buffer.getNumSamples();
    const bool sync = subParams[0] > 0.5f;
    const int shape = juce::jlimit(0, 2, juce::roundToInt(subParams[1]));
    const float duty = subParams[2];
    const float gateCoef = 1.0f - std::exp(-1.0f / (0.0015f * static_cast<float>(sampleRate)));
    const float srf = static_cast<float>(sampleRate);

    float tempoRate = 4.0f;
    if (sync)
    {
        const int idx = juce::jlimit(0, kShutterSteps - 1, static_cast<int>(sm1.getTargetValue() * static_cast<float>(kShutterSteps)));
        tempoRate = static_cast<float>(currentBpm / 60.0) / kShutterBeats[idx];
    }

    for (int i = 0; i < ns; ++i)
    {
        const float a = sm1.getNextValue();
        const float d = sm2.getNextValue();
        rateHz = sync ? tempoRate : shutterFreeHz(a);

        phase += rateHz / srf;
        if (phase >= 1.0f) phase -= 1.0f;

        float mod;
        if (shape == 0)      mod = 0.5f + 0.5f * std::cos(kTwoPi * phase);
        else if (shape == 1) mod = std::abs(1.0f - 2.0f * phase);
        else                 mod = 1.0f; // computed below

        gateSm += gateCoef * ((phase < duty ? 1.0f : 0.0f) - gateSm);
        if (shape == 2) mod = gateSm;

        const float act = juce::jmin(1.0f, a * 25.0f);
        const float gain = 1.0f - d * act * (1.0f - mod);

        for (int ch = 0; ch < c.n; ++ch)
            c.p[ch][i] *= gain;
    }
}

// =========================================================================================
// 8. AutoPan
// =========================================================================================
void FXAutoPan::onPrepare() { onReset(); }

void FXAutoPan::onReset() { phase = 0.0f; }

void FXAutoPan::process(juce::AudioBuffer<float>& buffer)
{
    auto c = getCh(buffer);
    if (c.n < 2) return;

    const int ns = buffer.getNumSamples();
    const bool sync = subParams[0] > 0.5f;
    const bool tri = subParams[1] > 0.5f;
    const float srf = static_cast<float>(sampleRate);

    float tempoRate = 1.0f;
    if (sync)
    {
        const int idx = juce::jlimit(0, kPanSteps - 1, static_cast<int>(sm2.getTargetValue() * static_cast<float>(kPanSteps)));
        tempoRate = static_cast<float>(currentBpm / 60.0) / kPanBeats[idx];
    }

    for (int i = 0; i < ns; ++i)
    {
        const float depth = sm1.getNextValue();
        const float r = sm2.getNextValue();
        rateHz = sync ? tempoRate : panFreeHz(r);

        phase += rateHz / srf;
        if (phase >= 1.0f) phase -= 1.0f;

        float lfo;
        if (tri)
        {
            float f = phase + 0.75f; f -= std::floor(f);
            lfo = 2.0f * std::abs(2.0f * f - 1.0f) - 1.0f;
        }
        else lfo = std::sin(kTwoPi * phase);

        const float pan = lfo * depth;
        const float ang = (pan + 1.0f) * 0.25f * kPi;
        c.p[0][i] *= std::cos(ang) * 1.41421356f;
        c.p[1][i] *= std::sin(ang) * 1.41421356f;
    }
}

// =========================================================================================
// 7b. Disperser (32 all-pass biquads: frequency dependent delay)
// =========================================================================================
void FXDisperser::onPrepare() { onReset(); }

void FXDisperser::onReset()
{
    for (auto& ch : z1) for (auto& v : ch) v = 0.0f;
    for (auto& ch : z2) for (auto& v : ch) v = 0.0f;
    fbState[0] = fbState[1] = 0.0f;
}

void FXDisperser::updateCoefs(float f0, float spread)
{
    const float srf = static_cast<float>(sampleRate);
    const float spreadOct = spread * 4.0f;
    constexpr float Q = 0.7f;

    for (int k = 0; k < kStages; ++k)
    {
        float f = f0 * std::exp2(spreadOct * (static_cast<float>(k) / static_cast<float>(kStages - 1) - 0.5f));
        f = juce::jlimit(20.0f, 0.45f * srf, f);
        const float w0 = kTwoPi * f / srf;
        const float alpha = std::sin(w0) / (2.0f * Q);
        const float a0 = 1.0f + alpha;
        a1[k] = -2.0f * std::cos(w0) / a0;
        a2[k] = (1.0f - alpha) / a0;
    }
}

void FXDisperser::process(juce::AudioBuffer<float>& buffer)
{
    auto c = getCh(buffer);
    const int ns = buffer.getNumSamples();
    const float spread = subParams[0];
    const float fb = subParams[1];
    const float mix = subParams[2];

    for (int i = 0; i < ns; ++i)
    {
        const float a = sm1.getNextValue();
        const float f = sm2.getNextValue();
        if ((i & 31) == 0) updateCoefs(40.0f * std::pow(200.0f, f), spread);

        const float stagesF = a * static_cast<float>(kStages);
        const float w = juce::jmin(1.0f, a * 10.0f) * mix;

        for (int ch = 0; ch < c.n; ++ch)
        {
            const float in = c.p[ch][i];
            float y = in + fb * fbState[ch];

            // Every stage always runs (keeps its state consistent); stages above "Amount" are blended out
            for (int s = 0; s < kStages; ++s)
            {
                const float out = a2[s] * y + z1[ch][s];
                z1[ch][s] = a1[s] * (y - out) + z2[ch][s];
                z2[ch][s] = y - a2[s] * out;
                const float g = juce::jlimit(0.0f, 1.0f, stagesF - static_cast<float>(s));
                y += g * (out - y);
            }

            fbState[ch] = std::tanh(y);

            // Soft knee above 0.8 so fast frequency sweeps can't produce loud spikes
            const float ay = std::abs(y);
            if (ay > 0.8f)
                y = std::copysign(0.8f + 0.2f * std::tanh((ay - 0.8f) / 0.2f), y);

            c.p[ch][i] = in * (1.0f - w) + y * w;
        }
    }
}

// =========================================================================================
// 7c. Rewind (tempo-synced reverse slices)
// =========================================================================================
void FXRewind::onPrepare()
{
    size = static_cast<int>(sampleRate * 7.0);
    for (auto& slot : buf) for (auto& b : slot) b.assign(static_cast<size_t>(size), 0.0f);
    onReset();
}

void FXRewind::onReset()
{
    for (auto& slot : buf) for (auto& b : slot) std::fill(b.begin(), b.end(), 0.0f);
    recSlot = 0; writePos = 0; lenPlay = 1; playPos = 0;
    havePrev = false;
    lastWet[0] = lastWet[1] = 0.0f;
    stepValid = false;
}

void FXRewind::process(juce::AudioBuffer<float>& buffer)
{
    auto c = getCh(buffer);
    if (c.n == 0) return;
    const int ns = buffer.getNumSamples();
    const float srf = static_cast<float>(sampleRate);

    const int xf = juce::jmax(8, static_cast<int>(subParams[0] * 0.001f * srf));
    const float fb = subParams[1];
    const int idx = juce::jlimit(0, 4, static_cast<int>(sm2.getTargetValue() * 5.0f));
    const double beats = kRewindBeats[idx];
    const int freeLen = juce::jlimit(256, size - 8, static_cast<int>(beats * 60.0 / currentBpm * sampleRate));
    const bool grid = blockPpq >= 0.0;           // slices follow the host's beat grid when known
    const double beatsPerSample = currentBpm / 60.0 / sampleRate;

    for (int i = 0; i < ns; ++i)
    {
        const float a = sm1.getNextValue();
        sm2.getNextValue();

        float in[2] = { c.p[0][i], c.n > 1 ? c.p[1][i] : c.p[0][i] };

        bool boundary = false;
        if (grid)
        {
            const long long s = static_cast<long long>(std::floor((blockPpq + i * beatsPerSample) / beats));
            if (! stepValid) { stepValid = true; lastStep = s; }
            else if (s != lastStep) { lastStep = s; boundary = true; }
        }
        else if (writePos >= freeLen)
        {
            boundary = true;
        }
        if (writePos >= size - 2) boundary = true;

        if (boundary && writePos > 32)
        {
            lenPlay = writePos;      // the slice we just recorded becomes the one we play backwards
            recSlot ^= 1;
            writePos = 0;
            playPos = 0;
            havePrev = true;
        }

        for (int ch = 0; ch < c.n; ++ch)
        {
            buf[recSlot][ch][static_cast<size_t>(writePos)] = in[ch] + fb * lastWet[ch];

            float wet;
            if (havePrev && playPos < lenPlay)
            {
                const int fi = juce::jmin(playPos, lenPlay - 1 - playPos);
                const float env = fi < xf ? static_cast<float>(fi) / static_cast<float>(xf) : 1.0f;
                wet = buf[1 - recSlot][ch][static_cast<size_t>(lenPlay - 1 - playPos)] * env;
            }
            else
            {
                wet = havePrev ? 0.0f : in[ch];
            }

            lastWet[ch] = wet;
            c.p[ch][i] = in[ch] * (1.0f - a) + wet * a;
        }

        ++writePos;
        ++playPos;
    }
}

// =========================================================================================
// 9. Particles (granular cloud)
// =========================================================================================
void FXParticles::onPrepare()
{
    size = static_cast<int>(sampleRate * 3.5);
    for (auto& b : ring) b.assign(static_cast<size_t>(size), 0.0f);

    delay.setBpm(currentBpm);
    delay.prepare(sampleRate, maxBlock);
    reverb.prepare(sampleRate, maxBlock);
    cloud.setSize(2, maxBlock);
    revBuf.setSize(2, maxBlock);
    onReset();
}

void FXParticles::onReset()
{
    for (auto& b : ring) std::fill(b.begin(), b.end(), 0.0f);
    w = 0;
    untilSpawn = 0.0f;
    for (auto& g : grains) g.active = false;
    delay.reset();
    reverb.reset();
    fxActive = reverbOn = false;
    freezeGain = 1.0f;
    silentCount = 0;
}

void FXParticles::spawn(int len, float pitchSt, float pitchRandSt, float reverseProb, float spread, float offsetMaxSec)
{
    for (auto& g : grains)
    {
        if (g.active) continue;

        // Fixed pitch, plus an optional random amount (whole semitones) around it
        float st = pitchSt;
        if (pitchRandSt > 0.01f)
            st += std::round((rand01() * 2.0f - 1.0f) * pitchRandSt);
        st = juce::jlimit(-24.0f, 24.0f, st);

        const float speed = std::exp2(st / 12.0f);
        const bool reverse = rand01() < reverseProb;
        const float offset = rand01() * offsetMaxSec * static_cast<float>(sampleRate);
        // keep the read head behind the write head for the whole length of the grain
        const float back = reverse ? offset + 64.0f
                                   : offset + static_cast<float>(len) * juce::jmax(1.0f, speed) + 64.0f;

        float pos = static_cast<float>(w) - back;
        while (pos < 0.0f) pos += static_cast<float>(size);

        g.pos = pos;
        g.step = reverse ? -speed : speed;
        g.len = len;
        g.age = 0;

        const float pan = (rand01() * 2.0f - 1.0f) * spread;
        const float ang = (pan + 1.0f) * 0.25f * kPi;
        g.gl = std::cos(ang) * 1.41421356f;
        g.gr = std::sin(ang) * 1.41421356f;
        g.active = true;
        return;
    }
}

void FXParticles::process(juce::AudioBuffer<float>& buffer)
{
    auto c = getCh(buffer);
    if (c.n == 0) return;
    const int ns = buffer.getNumSamples();
    const float srf = static_cast<float>(sampleRate);

    const float density = subParams[0];
    const float pitchSt = subParams[1];
    const float pitchRandSt = subParams[2];
    const float reverseProb = subParams[3];
    const float spread = subParams[4];
    const bool freeze = subParams[5] > 0.5f;
    const float delayAmt = subParams[6];
    const int delayTime = juce::jlimit(0, 4, juce::roundToInt(subParams[7]));
    const float reverbAmt = subParams[8];
    const float fsize = static_cast<float>(size);

    // When frozen the grains may pick from a wider part of the frozen audio (less correlated = less buzz)
    const float offsetMaxSec = freeze ? 1.6f : 0.8f;
    // A frozen cloud must not drone forever: if the input has been silent for 1 s (e.g. playback
    // stopped) it fades out, and comes back as soon as sound returns.
    const int silenceLimit = static_cast<int>(1.0f * srf);
    const float freezeCoef = 1.0f - std::exp(-1.0f / (0.25f * srf));

    auto softLimit = [](float x) noexcept
    {
        const float ax = std::abs(x);
        return ax > 0.7f ? std::copysign(0.7f + 0.3f * std::tanh((ax - 0.7f) / 0.3f), x) : x;
    };

    // The grain cloud is rendered into its own buffer so delay and reverb act on the cloud only
    if (ns > cloud.getNumSamples())
    {
        cloud.setSize(2, ns, false, false, true);
        revBuf.setSize(2, ns, false, false, true);
    }
    float* cloudL = cloud.getWritePointer(0);
    float* cloudR = cloud.getWritePointer(1);
    const bool active = ! isOffValue(sm1.getTargetValue()) || ! isOffValue(sm1.getCurrentValue());

    for (int i = 0; i < ns; ++i)
    {
        const float a = sm1.getNextValue();
        const float sz = sm2.getNextValue();

        const float sizeSec = 0.02f * std::pow(20.0f, sz);
        const int len = juce::jmax(64, static_cast<int>(sizeSec * srf));

        const float dryL = c.p[0][i];
        const float dryR = c.n > 1 ? c.p[1][i] : dryL;

        if (std::abs(dryL) + std::abs(dryR) < 1.0e-4f) { if (silentCount < 1000000000) ++silentCount; }
        else silentCount = 0;
        const float freezeTarget = (freeze && silentCount > silenceLimit) ? 0.0f : 1.0f;
        freezeGain += freezeCoef * (freezeTarget - freezeGain);

        if (! freeze)
        {
            ring[0][static_cast<size_t>(w)] = dryL;
            ring[1][static_cast<size_t>(w)] = dryR;
            if (++w >= size) w = 0;
        }

        untilSpawn -= 1.0f;
        if (untilSpawn <= 0.0f)
        {
            if (a > 0.001f) spawn(len, pitchSt, pitchRandSt, reverseProb, spread, offsetMaxSec);
            untilSpawn += srf / density * (0.5f + rand01());
        }

        float cl = 0.0f, cr = 0.0f;
        for (auto& g : grains)
        {
            if (! g.active) continue;

            const float env = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(g.age) / static_cast<float>(g.len));
            int i0 = static_cast<int>(g.pos);
            if (i0 >= size) i0 -= size;          // float rounding can land exactly on `size`
            if (i0 < 0) i0 = 0;
            const float f = g.pos - static_cast<float>(i0);
            const int i1 = (i0 + 1) % size;
            const float sl = ring[0][static_cast<size_t>(i0)] + f * (ring[0][static_cast<size_t>(i1)] - ring[0][static_cast<size_t>(i0)]);
            const float sr2 = ring[1][static_cast<size_t>(i0)] + f * (ring[1][static_cast<size_t>(i1)] - ring[1][static_cast<size_t>(i0)]);
            cl += sl * env * g.gl;
            cr += sr2 * env * g.gr;

            g.pos += g.step;
            if (g.pos >= fsize) g.pos -= fsize;
            if (g.pos < 0.0f) g.pos += fsize;
            if (++g.age >= g.len) g.active = false;
        }

        const float gain = 1.2f / std::sqrt(juce::jmax(1.0f, density * sizeSec));
        cl *= gain; cr *= gain;

        const float dryGain = 1.0f - 0.8f * a;
        c.p[0][i] = dryL * dryGain;
        if (c.n > 1) c.p[1][i] = dryR * dryGain;

        const float cloudGain = a * freezeGain;
        if (c.n > 1) { cloudL[i] = cl * cloudGain; cloudR[i] = cr * cloudGain; }
        else         { cloudL[i] = cloudR[i] = 0.5f * (cl + cr) * cloudGain; }
    }

    // ---- delay and reverb on the cloud ----
    if (! active)
    {
        if (fxActive) { delay.reset(); reverb.reset(); fxActive = false; reverbOn = false; }
        return;
    }
    fxActive = true;

    juce::AudioBuffer<float> cloudView(cloud.getArrayOfWritePointers(), 2, ns);
    juce::AudioBuffer<float> revView(revBuf.getArrayOfWritePointers(), 2, ns);

    // Echoes: the cloud plus its repeats
    delay.setBpm(currentBpm);
    delay.setMacro1(delayAmt);
    delay.setMacro2(0.45f / 0.92f);          // feedback 45%
    delay.setParam(0, static_cast<float>(delayTime));
    delay.setParam(1, 1.0f);                 // ping-pong
    delay.setParam(2, 150.0f);               // low cut
    delay.setParam(3, 8000.0f);              // high cut
    delay.setParam(4, 1.0f);                 // width
    delay.processBlock(cloudView);

    // Reverb: parallel send of the cloud (pure wet), added on top
    const bool useReverb = reverbAmt > 0.001f;
    if (! useReverb && reverbOn) { reverb.reset(); reverbOn = false; }
    if (useReverb)
    {
        reverbOn = true;
        for (int ch = 0; ch < 2; ++ch) revView.copyFrom(ch, 0, cloudView, ch, 0, ns);
        reverb.setMacro1(1.0f);
        reverb.setMacro2(0.6f);
        reverb.setParam(0, 1.0f);            // hall
        reverb.setParam(1, 15.0f);           // pre-delay
        reverb.setParam(2, 0.5f);            // damping
        reverb.setParam(3, 150.0f);          // low cut
        reverb.setParam(4, 12000.0f);        // high cut
        reverb.setParam(5, 0.5f);            // modulation
        reverb.setParam(6, 1.0f);            // width
        reverb.processBlock(revView);
    }

    for (int i = 0; i < ns; ++i)
    {
        float l = cloudL[i], r = cloudR[i];
        if (useReverb)
        {
            l += revView.getSample(0, i) * reverbAmt;
            r += revView.getSample(1, i) * reverbAmt;
        }

        // Soft knee: overlapping grains can add up, this keeps the cloud from ever spiking
        l = softLimit(l);
        r = softLimit(r);

        if (c.n > 1) { c.p[0][i] += l; c.p[1][i] += r; }
        else         { c.p[0][i] += 0.5f * (l + r); }
    }
}

// =========================================================================================
// 12b. Build Up (macro): one knob combines high-pass, low-pass, stereo width, delay and reverb
//      on the incoming sound, following a preset. It generates no sounds of its own.
// =========================================================================================
namespace
{
    struct BuildPreset
    {
        float hpHz;       // high-pass cutoff reached at Build = 100%   (20 = no high-pass)
        float lpHz;       // low-pass cutoff reached at Build = 100%    (20000 = no low-pass)
        float width;      // stereo width reached (0 = mono, 1 = unchanged, >1 = wider)
        float delayMix;   // delay level reached
        float delayFb;    // delay feedback
        int delayDiv;     // index into the delay time table (1/16, 1/8T, 1/8, 1/8D, 1/4, 1/2)
        float reverbMix;  // reverb level reached
        float reverbDecay;
    };

    const BuildPreset kBuildPresets[] =
    {
        //   hp     lp      width  dMix   dFb    dDiv  rMix   rDecay
        {  2500.f, 20000.f, 0.25f, 0.30f, 0.45f, 2,   0.35f, 0.60f },   // Classic       - thin, narrow, echo + hall
        {    20.f,   450.f, 0.00f, 0.00f, 0.00f, 2,   0.50f, 0.70f },   // Underwater    - closes the highs, mono, wet
        {   900.f,  3200.f, 0.00f, 0.15f, 0.30f, 3,   0.00f, 0.50f },   // Telephone     - band-limited and mono
        {  1800.f, 20000.f, 0.30f, 0.60f, 0.75f, 0,   0.20f, 0.50f },   // Echo Tunnel   - thin, lots of repeats
        {  4000.f, 20000.f, 1.80f, 0.20f, 0.50f, 4,   0.85f, 0.85f },   // Wash Out      - widens into a huge reverb
        {  1500.f,  1800.f, 0.00f, 0.00f, 0.00f, 2,   0.00f, 0.50f }    // Choke         - squeezes to a narrow mono band
    };
    constexpr int kNumBuildPresets = static_cast<int>(sizeof(kBuildPresets) / sizeof(kBuildPresets[0]));
}

void FXBuildUp::onPrepare()
{
    delay.setBpm(currentBpm);
    delay.prepare(sampleRate, 512);
    reverb.prepare(sampleRate, 512);
    onReset();
}

void FXBuildUp::onReset()
{
    for (int c = 0; c < 2; ++c) { hp1[c].reset(); hp2[c].reset(); lp1[c].reset(); lp2[c].reset(); }
    delay.reset();
    reverb.reset();
}

void FXBuildUp::process(juce::AudioBuffer<float>& buffer)
{
    auto c = getCh(buffer);
    const int ns = buffer.getNumSamples();

    const auto& P = kBuildPresets[juce::jlimit(0, kNumBuildPresets - 1, juce::roundToInt(subParams[0]))];
    const float amtHP = subParams[1], amtLP = subParams[2], amtW = subParams[3];
    const float amtD = subParams[4], amtR = subParams[5];

    float width = 1.0f;
    float bp = 0.0f;

    for (int i = 0; i < ns; ++i)
    {
        const float b = sm1.getNextValue();
        const float cv = sm2.getNextValue();

        if ((i & 15) == 0)
        {
            // Curve: 0 = the effects arrive early, 50% = linear, 100% = they arrive late and hit hard
            const float curveExp = std::pow(3.0f, (cv - 0.5f) * 2.0f);
            bp = std::pow(b, curveExp);

            const float fcHp = 20.0f * std::pow(P.hpHz / 20.0f, bp * amtHP);
            const float fcLp = 20000.0f * std::pow(P.lpHz / 20000.0f, bp * amtLP);
            for (int ch = 0; ch < 2; ++ch)
            {
                hp1[ch].set(fcHp, 0.707f, sampleRate);
                hp2[ch].set(fcHp, 0.707f, sampleRate);
                lp1[ch].set(fcLp, 0.707f, sampleRate);
                lp2[ch].set(fcLp, 0.707f, sampleRate);
            }
            width = 1.0f + (P.width - 1.0f) * bp * amtW;
        }

        float s[2] = { 0.0f, 0.0f };
        for (int ch = 0; ch < c.n; ++ch)
        {
            float lp, hp, y = c.p[ch][i];
            hp1[ch].tick(y, lp, hp); y = hp;     // 24 dB/oct high-pass
            hp2[ch].tick(y, lp, hp); y = hp;
            lp1[ch].tick(y, lp, hp); y = lp;     // 24 dB/oct low-pass
            lp2[ch].tick(y, lp, hp); y = lp;
            s[ch] = y;
        }

        if (c.n == 2)
        {
            const float mid = 0.5f * (s[0] + s[1]);
            const float side = 0.5f * (s[0] - s[1]) * width;
            s[0] = mid + side;
            s[1] = mid - side;
        }
        for (int ch = 0; ch < c.n; ++ch)
            c.p[ch][i] = s[ch];
    }

    // Send effects follow the same (curved) Build amount; they smooth their own levels
    const float curveExpT = std::pow(3.0f, (sm2.getTargetValue() - 0.5f) * 2.0f);
    const float bT = std::pow(sm1.getTargetValue(), curveExpT);

    delay.setBpm(currentBpm);
    delay.setMacro1(P.delayMix * bT * amtD);
    delay.setMacro2(P.delayFb / 0.92f);
    delay.setParam(0, static_cast<float>(P.delayDiv));
    delay.setParam(1, 1.0f);        // ping-pong
    delay.setParam(2, 150.0f);      // low cut
    delay.setParam(3, 9000.0f);     // high cut
    delay.setParam(4, 1.0f);        // width
    delay.processBlock(buffer);

    reverb.setMacro1(P.reverbMix * bT * amtR);
    reverb.setMacro2(P.reverbDecay);
    reverb.setParam(0, 1.0f);       // hall
    reverb.setParam(1, 15.0f);      // pre-delay
    reverb.setParam(2, 0.5f);       // damping
    reverb.setParam(3, 100.0f);     // low cut
    reverb.setParam(4, 12000.0f);   // high cut
    reverb.setParam(5, 0.5f);       // modulation
    reverb.setParam(6, 1.0f);       // width
    reverb.processBlock(buffer);
}

// =========================================================================================
// 10. Tape stop
// =========================================================================================
void FXTapeStop::onPrepare()
{
    size = static_cast<int>(sampleRate * 4.0);
    for (auto& b : ring) b.assign(static_cast<size_t>(size), 0.0f);
    onReset();
}

void FXTapeStop::onReset()
{
    for (auto& b : ring) std::fill(b.begin(), b.end(), 0.0f);
    w = 0; lag = 0.0f; speed = 1.0f; liveMix = 0.0f;
    lpState[0] = lpState[1] = 0.0f;
}

void FXTapeStop::process(juce::AudioBuffer<float>& buffer)
{
    auto c = getCh(buffer);
    const int ns = buffer.getNumSamples();
    const float curve = subParams[0];
    const float drop = subParams[1];
    const float cap = static_cast<float>(size - 8);
    const float srf = static_cast<float>(sampleRate);
    // Return Time = how long the tape takes to spin back up once the brake is released (0 = instant)
    const float returnTime = subParams[2];
    const bool instantReturn = returnTime < 0.005f;
    const float xfadeInc = 1.0f / ((instantReturn ? 0.005f : 0.03f) * srf);
    spCoefUp = instantReturn ? 1.0f : 1.0f - std::exp(-3.0f / (returnTime * srf));

    for (int i = 0; i < ns; ++i)
    {
        const float m = sm1.getNextValue();
        const float t = sm2.getNextValue();

        if ((i & 15) == 0)
        {
            const float tau = 0.01f * std::pow(150.0f, t);
            spCoef = 1.0f - std::exp(-1.0f / (tau * srf));
        }

        float in[2] = { 0, 0 };
        for (int ch = 0; ch < c.n; ++ch)
        {
            in[ch] = c.p[ch][i];
            ring[ch][static_cast<size_t>(w)] = in[ch];
        }

        if (m < 0.01f && lag <= 0.0f)
        {
            // Fully live: dry passes through untouched
            speed = 1.0f; liveMix = 0.0f; lag = 0.0f;
            for (int ch = 0; ch < c.n; ++ch) lpState[ch] = in[ch];
        }
        else
        {
            const float target = std::pow(juce::jmax(0.0f, 1.0f - m), curve);
            speed += (target - speed) * (target > speed ? spCoefUp : spCoef);
            lag = juce::jmin(lag + (1.0f - speed), cap);

            if (m < 0.01f && speed > 0.97f) liveMix += xfadeInc;
            else                            liveMix -= xfadeInc;
            liveMix = juce::jlimit(0.0f, 1.0f, liveMix);

            if (liveMix >= 1.0f)
            {
                lag = 0.0f; liveMix = 0.0f; speed = 1.0f;
            }
            else
            {
                if ((i & 7) == 0)
                {
                    const float cutoff = 20000.0f + drop * ((300.0f + 19700.0f * speed * speed) - 20000.0f);
                    lpA = onePole(cutoff, sampleRate);
                }
                const float gain = speed < 0.12f ? speed / 0.12f : 1.0f;

                float pos = static_cast<float>(w) - lag;
                if (pos < 0.0f) pos += static_cast<float>(size);
                int i0 = static_cast<int>(pos);
                const float f = pos - static_cast<float>(i0);
                if (i0 >= size) i0 -= size;
                const int i1 = (i0 + 1) % size;

                for (int ch = 0; ch < c.n; ++ch)
                {
                    const float y = ring[ch][static_cast<size_t>(i0)]
                                  + f * (ring[ch][static_cast<size_t>(i1)] - ring[ch][static_cast<size_t>(i0)]);
                    lpState[ch] += lpA * (y - lpState[ch]);
                    c.p[ch][i] = lpState[ch] * gain * (1.0f - liveMix) + in[ch] * liveMix;
                }
            }
        }

        if (++w >= size) w = 0;
    }
}

// =========================================================================================
// 11. Delay
// =========================================================================================
void FXDelay::onPrepare()
{
    size = static_cast<int>(sampleRate * 3.0);
    for (auto& b : ring) b.assign(static_cast<size_t>(size), 0.0f);
    onReset();
    delaySm = 0.0f;
}

void FXDelay::onReset()
{
    for (auto& b : ring) std::fill(b.begin(), b.end(), 0.0f);
    w = 0;
    for (int c = 0; c < 2; ++c) lp1[c] = lp2[c] = hp1[c] = hp2[c] = 0.0f;
}

void FXDelay::process(juce::AudioBuffer<float>& buffer)
{
    auto c = getCh(buffer);
    const int ns = buffer.getNumSamples();
    const int timeIdx = juce::jlimit(0, 5, juce::roundToInt(subParams[0]));
    const bool pingPong = subParams[1] > 0.5f && c.n == 2;
    const float hpA = onePole(juce::jmax(20.0f, subParams[2]), sampleRate);   // Low Cut
    const float lpA = onePole(subParams[3], sampleRate);                      // High Cut
    const float width = subParams[4];                                         // Stereo width
    const float srf = static_cast<float>(sampleRate);

    const float targetDelay = juce::jlimit(32.0f, static_cast<float>(size - 8),
                                           static_cast<float>(kDelayBeats[timeIdx] * 60.0 / currentBpm * sampleRate));
    if (delaySm < 1.0f) delaySm = targetDelay;
    const float delayCoef = 1.0f - std::exp(-1.0f / (0.05f * srf));

    for (int i = 0; i < ns; ++i)
    {
        const float a = sm1.getNextValue();
        const float fb = 0.92f * sm2.getNextValue();
        delaySm += (targetDelay - delaySm) * delayCoef;

        float in[2] = { 0, 0 }, d[2] = { 0, 0 };
        float pos = static_cast<float>(w) - delaySm;
        if (pos < 0.0f) pos += static_cast<float>(size);

        for (int ch = 0; ch < c.n; ++ch)
        {
            in[ch] = c.p[ch][i];
            float x = hermite(ring[ch].data(), size, pos);

            // High cut (2 x one-pole = 12 dB/oct), then low cut (same)
            lp1[ch] += lpA * (x - lp1[ch]);
            lp2[ch] += lpA * (lp1[ch] - lp2[ch]);
            x = lp2[ch];
            hp1[ch] += hpA * (x - hp1[ch]);
            hp2[ch] += hpA * (x - hp1[ch] - hp2[ch]);
            d[ch] = x - hp1[ch] - hp2[ch];
        }

        // The filtered echo is both what you hear and what feeds back, so each repeat gets filtered again
        float fbSig[2] = { d[0], d[1] };

        if (pingPong)
        {
            const float mono = 0.5f * (in[0] + in[1]);
            ring[0][static_cast<size_t>(w)] = mono + std::tanh(fb * fbSig[1]);
            ring[1][static_cast<size_t>(w)] = std::tanh(fb * fbSig[0]);
        }
        else
        {
            for (int ch = 0; ch < c.n; ++ch)
                ring[ch][static_cast<size_t>(w)] = in[ch] + std::tanh(fb * fbSig[ch]);
        }

        // Stereo width of what you hear (0 = mono, 100% = as is, 200% = extra wide).
        // The feedback path keeps its own stereo image, so ping-pong still bounces inside.
        float outWet[2] = { d[0], d[1] };
        if (c.n == 2)
        {
            const float mid = 0.5f * (d[0] + d[1]);
            const float side = 0.5f * (d[0] - d[1]) * width;
            outWet[0] = mid + side;
            outWet[1] = mid - side;
        }

        for (int ch = 0; ch < c.n; ++ch)
            c.p[ch][i] = in[ch] + outWet[ch] * a;

        if (++w >= size) w = 0;
    }
}

// =========================================================================================
// 12. Reverb (Dattorro tank: Room / Hall / Plate)
// =========================================================================================
namespace
{
    constexpr float kMaxTankSize = 1.8f; // buffers are allocated for the biggest room (Hall)
}

void FXReverb::onPrepare()
{
    scale = static_cast<float>(sampleRate / 29761.0);
    auto S = [this](int n) { return juce::jmax(4, static_cast<int>(std::lround(n * scale))); };
    auto SM = [&](int n) { return S(static_cast<int>(n * kMaxTankSize)); };

    pre.init(static_cast<int>(sampleRate * 1.05));   // pre-delay up to 1 second

    inAp[0].init(S(142), 0.75f);
    inAp[1].init(S(107), 0.75f);
    inAp[2].init(S(379), 0.625f);
    inAp[3].init(S(277), 0.625f);

    const int maxMod = S(24);
    apModL.init(SM(672), maxMod, 0.7f);
    apModR.init(SM(908), maxMod, 0.7f);

    dL1.init(SM(4453)); dL2.init(SM(3720)); dR1.init(SM(4217)); dR2.init(SM(3163));
    apL2.init(SM(1800), 0.5f);
    apR2.init(SM(2656), 0.5f);

    curType = -1; // re-tuned on the first processed block
    setType(1);   // Hall by default
    onReset();
}

// Re-tunes the tank lengths without touching the allocated buffers
void FXReverb::setType(int type)
{
    curType = type;
    typeSize = type == 0 ? 0.45f : (type == 1 ? 1.7f : 1.0f);

    auto L = [this](int n, int allocated) { return juce::jlimit(4, allocated, static_cast<int>(std::lround(n * scale * typeSize))); };

    lenL1 = L(4453, dL1.size - 2);  lenL2 = L(3720, dL2.size - 2);
    lenR1 = L(4217, dR1.size - 2);  lenR2 = L(3163, dR2.size - 2);
    apL2.len = L(1800, apL2.dl.size - 2);
    apR2.len = L(2656, apR2.dl.size - 2);
    const int modHeadroom = static_cast<int>(std::lround(24.0f * scale)) + 6;
    apModL.len = L(672, apModL.dl.size - modHeadroom);
    apModR.len = L(908, apModR.dl.size - modHeadroom);

    auto T = [this](int n, int len) { return juce::jlimit(1, len - 1, static_cast<int>(std::lround(n * scale * typeSize))); };
    tapsL[0] = T(266, lenR1);  tapsL[1] = T(2974, lenR1);
    tapsL[2] = T(1190, apR2.len); tapsL[3] = T(1996, lenR2);
    tapsL[4] = T(1990, lenL1); tapsL[5] = T(187, apL2.len); tapsL[6] = T(1066, lenL2);
    tapsR[0] = T(353, lenL1);  tapsR[1] = T(3627, lenL1);
    tapsR[2] = T(1228, apL2.len); tapsR[3] = T(2111, lenL2);
    tapsR[4] = T(335, lenR1);  tapsR[5] = T(121, apR2.len); tapsR[6] = T(1066, lenR2);
}

void FXReverb::onReset()
{
    pre.clear();
    for (auto& a : inAp) a.clear();
    apModL.clear(); apModR.clear(); apL2.clear(); apR2.clear();
    dL1.clear(); dL2.clear(); dR1.clear(); dR2.clear();
    dampL = dampR = 0.0f;
    for (int c = 0; c < 2; ++c) hpL1[c] = hpL2[c] = lpL1[c] = lpL2[c] = 0.0f;
    lfoL = lfoR = 0.0f;
}

void FXReverb::process(juce::AudioBuffer<float>& buffer)
{
    auto c = getCh(buffer);
    if (c.n == 0) return;
    const int ns = buffer.getNumSamples();
    const float srf = static_cast<float>(sampleRate);

    const int type = juce::jlimit(0, 2, juce::roundToInt(subParams[0]));
    if (type != curType)
    {
        for (auto* d : { &dL1, &dL2, &dR1, &dR2 }) d->clear();
        apL2.clear(); apR2.clear(); apModL.clear(); apModR.clear();
        dampL = dampR = 0.0f;
        setType(type);
    }

    const int preSmp = juce::jlimit(1, pre.size - 3, static_cast<int>(subParams[1] * 0.001f * srf));
    const float dampC = juce::jlimit(0.05f, 0.95f, 0.1f + 0.85f * subParams[2] + (type == 0 ? 0.1f : 0.0f));
    const float hpA = onePole(juce::jmax(20.0f, subParams[3]), sampleRate);
    const float lpA = onePole(subParams[4], sampleRate);
    const float excursion = subParams[5] * 10.0f * scale * typeSize;
    const float width = subParams[6];
    const float incL = kTwoPi * 0.9f / srf;
    const float incR = kTwoPi * 1.13f / srf;
    constexpr float outScale = 0.62f;

    // Decay range per room type
    const float decMin = type == 0 ? 0.20f : (type == 1 ? 0.45f : 0.35f);
    const float decSpan = type == 0 ? 0.55f : (type == 1 ? 0.52f : 0.60f);

    for (int i = 0; i < ns; ++i)
    {
        const float a = sm1.getNextValue();
        const float dec = decMin + decSpan * sm2.getNextValue();

        const float inL = c.p[0][i];
        const float inR = c.n > 1 ? c.p[1][i] : inL;

        // Mix works as a SEND: it scales what goes INTO the reverb. The tank output is never scaled by it,
        // so lowering Mix stops feeding the reverb but the tail keeps ringing out naturally.
        pre.push(0.5f * (inL + inR) * a);
        float x = pre.tap(preSmp);

        for (auto& ap : inAp) x = ap.process(x);

        const float lateL = dL2.tap(lenL2);
        const float lateR = dR2.tap(lenR2);

        // Left branch
        float xl = x + dec * lateR;
        xl = apModL.process(xl, std::sin(lfoL) * excursion);
        dL1.push(xl);
        const float l1 = dL1.tap(lenL1);
        dampL = l1 * (1.0f - dampC) + dampL * dampC;
        dL2.push(apL2.process(dampL * dec));

        // Right branch
        float xr = x + dec * lateL;
        xr = apModR.process(xr, std::sin(lfoR) * excursion);
        dR1.push(xr);
        const float r1 = dR1.tap(lenR1);
        dampR = r1 * (1.0f - dampC) + dampR * dampC;
        dR2.push(apR2.process(dampR * dec));

        lfoL += incL; if (lfoL > kTwoPi) lfoL -= kTwoPi;
        lfoR += incR; if (lfoR > kTwoPi) lfoR -= kTwoPi;

        float wet[2];
        wet[0] = outScale * (dR1.tap(tapsL[0]) + dR1.tap(tapsL[1]) - apR2.dl.tap(tapsL[2])
                           + dR2.tap(tapsL[3]) - dL1.tap(tapsL[4]) - apL2.dl.tap(tapsL[5])
                           - dL2.tap(tapsL[6]));
        wet[1] = outScale * (dL1.tap(tapsR[0]) + dL1.tap(tapsR[1]) - apL2.dl.tap(tapsR[2])
                           + dL2.tap(tapsR[3]) - dR1.tap(tapsR[4]) - apR2.dl.tap(tapsR[5])
                           - dR2.tap(tapsR[6]));

        // Low cut / high cut on the reverb only (2 x one-pole each = 12 dB/oct)
        for (int ch = 0; ch < 2; ++ch)
        {
            float v = wet[ch];
            lpL1[ch] += lpA * (v - lpL1[ch]);
            lpL2[ch] += lpA * (lpL1[ch] - lpL2[ch]);
            v = lpL2[ch];
            hpL1[ch] += hpA * (v - hpL1[ch]);
            hpL2[ch] += hpA * (v - hpL1[ch] - hpL2[ch]);
            wet[ch] = v - hpL1[ch] - hpL2[ch];
        }

        // Stereo width of the reverb (0 = mono, 100% = as is, 200% = extra wide)
        {
            const float mid = 0.5f * (wet[0] + wet[1]);
            const float side = 0.5f * (wet[0] - wet[1]) * width;
            wet[0] = mid + side;
            wet[1] = mid - side;
        }

        // Cross-fade: at 100% almost only reverb is left
        const float dryGain = std::pow(1.0f - a, 1.5f);

        if (c.n > 1)
        {
            c.p[0][i] = inL * dryGain + wet[0];
            c.p[1][i] = inR * dryGain + wet[1];
        }
        else
        {
            c.p[0][i] = inL * dryGain + 0.5f * (wet[0] + wet[1]);
        }
    }
}

// =========================================================================================
// 13. Stereo (Wide / Haas / Mono), bass kept untouched below the crossover
// =========================================================================================
void FXWidener::onPrepare()
{
    size = static_cast<int>(sampleRate * 0.06) + 8;
    for (auto& b : ring) b.assign(static_cast<size_t>(size), 0.0f);
    onReset();
}

void FXWidener::onReset()
{
    for (int c = 0; c < 2; ++c) { lpA[c].reset(); lpB[c].reset(); hpA[c].reset(); hpB[c].reset(); }
    for (auto& b : ring) std::fill(b.begin(), b.end(), 0.0f);
    w = 0;
    delaySm = 0.0f;
}

void FXWidener::process(juce::AudioBuffer<float>& buffer)
{
    auto c = getCh(buffer);
    if (c.n < 2) return;   // needs a stereo signal

    const int ns = buffer.getNumSamples();
    const float srf = static_cast<float>(sampleRate);
    const bool haas = subParams[0] > 0.5f;          // how the RIGHT half of the knob widens
    const float haasMaxMs = subParams[1];
    const int delayedCh = subParams[2] > 0.5f ? 1 : 0;          // 0 = Left, 1 = Right
    const float smoothCoef = 1.0f - std::exp(-1.0f / (0.02f * srf));

    for (int i = 0; i < ns; ++i)
    {
        // Knob: 50% = off, left = towards mono, right = wider
        const float knob = (sm1.getNextValue() - 0.5f) * 2.0f;      // -1 .. +1
        const float a = std::abs(knob);
        const bool towardsMono = knob < 0.0f;
        const float bass = sm2.getNextValue();

        if ((i & 15) == 0)
        {
            const float fc = 20.0f * std::pow(25.0f, bass);      // 20 Hz .. 500 Hz
            for (int ch = 0; ch < 2; ++ch)
            {
                lpA[ch].set(fc, 0.707f, sampleRate);
                lpB[ch].set(fc, 0.707f, sampleRate);
                hpA[ch].set(fc, 0.707f, sampleRate);
                hpB[ch].set(fc, 0.707f, sampleRate);
            }
        }

        // Split into low (left alone) and high (processed). Two cascaded 12 dB filters = LR4.
        float low[2], high[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            const float x = c.p[ch][i];
            float lp, hp, lp2, hp2;
            lpA[ch].tick(x, lp, hp);
            lpB[ch].tick(lp, lp2, hp2);
            low[ch] = lp2;

            hpA[ch].tick(x, lp, hp);
            hpB[ch].tick(hp, lp2, hp2);
            high[ch] = hp2;
        }

        float outHigh[2] = { high[0], high[1] };

        if (haas && ! towardsMono)
        {
            // Haas: delay one side of the high band by up to "Haas Time"
            ring[0][static_cast<size_t>(w)] = high[0];
            ring[1][static_cast<size_t>(w)] = high[1];

            const float target = juce::jmax(4.0f, a * haasMaxMs * 0.001f * srf);
            delaySm += (target - delaySm) * smoothCoef;
            if (delaySm < 4.0f) delaySm = 4.0f;

            float pos = static_cast<float>(w) - delaySm;
            if (pos < 0.0f) pos += static_cast<float>(size);
            outHigh[delayedCh] = hermite(ring[delayedCh].data(), size, pos);

            if (++w >= size) w = 0;
        }
        else
        {
            // Right of centre: side gain 1x -> 2x.   Left of centre: side gain 1x -> 0x (mono).
            const float width = towardsMono ? 1.0f - a : 1.0f + a;
            const float mid = 0.5f * (high[0] + high[1]);
            const float side = 0.5f * (high[0] - high[1]) * width;
            outHigh[0] = mid + side;
            outHigh[1] = mid - side;
        }

        c.p[0][i] = low[0] + outHigh[0];
        c.p[1][i] = low[1] + outHigh[1];
    }
}

// =========================================================================================
// 14. Pitch (three overlapping delay-line grains, free or whole semitones)
// =========================================================================================
void FXPitch::onPrepare()
{
    size = static_cast<int>(sampleRate * 0.14) + 16;
    for (auto& b : ring) b.assign(static_cast<size_t>(size), 0.0f);
    onReset();
}

void FXPitch::onReset()
{
    for (auto& b : ring) std::fill(b.begin(), b.end(), 0.0f);
    w = 0;
    phase = 0.0f;
    ratioSm = 1.0f;
    engageSm = 0.0f;
}

void FXPitch::process(juce::AudioBuffer<float>& buffer)
{
    auto c = getCh(buffer);
    if (c.n == 0) return;

    const int ns = buffer.getNumSamples();
    const float srf = static_cast<float>(sampleRate);

    const bool semitones = subParams[0] > 0.5f;
    const float rangeSt = subParams[1] > 0.5f ? 24.0f : 12.0f;
    static const float grainMs[] = { 30.0f, 55.0f, 90.0f };
    const float grainLen = grainMs[juce::jlimit(0, 2, juce::roundToInt(subParams[2]))] * 0.001f * srf;

    const float ratioCoef = 1.0f - std::exp(-1.0f / (0.012f * srf));
    const float engageCoef = 1.0f - std::exp(-1.0f / (0.03f * srf));

    for (int i = 0; i < ns; ++i)
    {
        const float m = sm1.getNextValue();
        const float mix = sm2.getNextValue();

        float st = (m - 0.5f) * 2.0f * rangeSt;
        if (semitones) st = std::round(st);

        ratioSm += (std::exp2(st / 12.0f) - ratioSm) * ratioCoef;

        // fade the wet signal in/out around "no shift" so leaving the centre never clicks
        const float engageTarget = std::abs(st) > 0.02f ? 1.0f : 0.0f;
        engageSm += (engageTarget - engageSm) * engageCoef;
        if (engageTarget == 0.0f && engageSm < 1.0e-4f) engageSm = 0.0f;

        for (int ch = 0; ch < c.n; ++ch)
            ring[ch][static_cast<size_t>(w)] = c.p[ch][i];

        // the read delay sweeps at (1 - ratio) samples per sample: that is the pitch shift
        phase += (1.0f - ratioSm) / grainLen;
        phase -= std::floor(phase);

        float win[3], delay[3];
        for (int k = 0; k < 3; ++k)
        {
            float p = phase + static_cast<float>(k) / 3.0f;
            if (p >= 1.0f) p -= 1.0f;
            win[k] = 0.5f - 0.5f * std::cos(kTwoPi * p);      // three Hann windows 1/3 apart sum to 1.5
            delay[k] = 4.0f + p * grainLen;
        }

        const float wetMix = engageSm * mix;
        for (int ch = 0; ch < c.n; ++ch)
        {
            float wet = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                float pos = static_cast<float>(w) - delay[k];
                if (pos < 0.0f) pos += static_cast<float>(size);
                wet += hermite(ring[ch].data(), size, pos) * win[k];
            }
            wet *= (1.0f / 1.5f);

            const float dry = c.p[ch][i];
            c.p[ch][i] = dry * (1.0f - wetMix) + wet * wetMix;
        }

        if (++w >= size) w = 0;
    }
}

// =========================================================================================
// Engine
// =========================================================================================
MultiFXEngine::MultiFXEngine()
{
    effects.push_back(std::make_unique<FXFilter>(false));
    effects.push_back(std::make_unique<FXFilter>(true));
    effects.push_back(std::make_unique<FXSaturation>());
    effects.push_back(std::make_unique<FXBitcrusher>());
    effects.push_back(std::make_unique<FXChorus>());
    effects.push_back(std::make_unique<FXPhaser>());
    effects.push_back(std::make_unique<FXDisperser>());
    effects.push_back(std::make_unique<FXRewind>());
    effects.push_back(std::make_unique<FXShutter>());
    effects.push_back(std::make_unique<FXAutoPan>());
    effects.push_back(std::make_unique<FXParticles>());
    effects.push_back(std::make_unique<FXTapeStop>());
    effects.push_back(std::make_unique<FXBuildUp>());
    effects.push_back(std::make_unique<FXDelay>());
    effects.push_back(std::make_unique<FXReverb>());
    effects.push_back(std::make_unique<FXWidener>());
    effects.push_back(std::make_unique<FXPitch>());

    // Signal-chain order (differs from the UI order on purpose): the stereo widener comes after
    // the delay and reverb so it widens their tails too, and the Shutter runs LAST so it also
    // chops those tails.
    using T = EffectType;
    for (auto t : { T::FilterLP, T::FilterHP, T::Saturation, T::Bitcrusher, T::Chorus, T::Phaser,
                    T::Disperser, T::Rewind, T::Particles, T::TapeStop, T::Pitch, T::Delay, T::Reverb,
                    T::BuildUp, T::Widener, T::AutoPan, T::Shutter })
        order.push_back(static_cast<int>(t));
}

void MultiFXEngine::prepare(double sampleRate, int samplesPerBlock)
{
    for (auto& fx : effects)
    {
        fx->setBpm(currentBpm);
        fx->prepare(sampleRate, samplesPerBlock);
    }
}

void MultiFXEngine::reset()
{
    for (auto& fx : effects) fx->reset();
}

void MultiFXEngine::setBpm(double bpm)
{
    currentBpm = bpm;
    for (auto& fx : effects) fx->setBpm(bpm);
}

void MultiFXEngine::setPpq(double ppq)
{
    for (auto& fx : effects) fx->setPpq(ppq);
}

bool MultiFXEngine::anyKnobMoving() const
{
    for (const auto& fx : effects)
        if (fx->isMacroMoving()) return true;
    return false;
}

void MultiFXEngine::process(juce::AudioBuffer<float>& buffer)
{
    for (int idx : order)
        effects[static_cast<size_t>(idx)]->processBlock(buffer);

    // Safety net: never let a NaN/Inf reach the host
    bool bad = false;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (! std::isfinite(d[i]))
            {
                d[i] = 0.0f;
                bad = true;
            }
        }
    }
    if (bad) reset();
}

EffectBase* MultiFXEngine::getEffect(int index)
{
    if (index >= 0 && index < static_cast<int>(effects.size()))
        return effects[static_cast<size_t>(index)].get();
    return nullptr;
}

} // namespace EasyDSP
