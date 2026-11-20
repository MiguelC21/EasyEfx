#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <memory>
#include <cmath>

namespace EasyDSP
{

// The 12 effects, in signal-chain order (also the order shown in the UI grid).
enum class EffectType
{
    FilterLP = 0,
    FilterHP,
    Saturation,
    Bitcrusher,
    Chorus,
    Phaser,
    Disperser,
    Rewind,
    Shutter,
    AutoPan,
    Particles,
    TapeStop,
    BuildUp,
    Delay,
    Reverb,
    Widener,        // appended last so the parameter IDs of the existing effects don't move
    Pitch,

    NumEffects
};

constexpr int kNumEffects = static_cast<int>(EffectType::NumEffects);
constexpr int kMaxSubParams = 10;

// One "advanced" parameter, shown in the settings panel (not on the main tile).
// If `options` is non-empty it is a choice parameter (value = option index).
struct ParamConfig
{
    juce::String name;
    float minVal, maxVal, defaultVal;
    juce::String unit;
    juce::StringArray options = {};
    float skew = 1.0f;
};

struct EffectInfo
{
    EffectType type;
    juce::String name;
    juce::String category;

    // Main knob. At 0 the effect is completely off (and costs no CPU).
    juce::String macro1Label;
    // Secondary knob (shape of the effect).
    juce::String macro2Label;
    float macro2Default;

    juce::uint32 accent;      // ARGB accent colour used by the UI
    juce::String tooltip;

    std::vector<ParamConfig> params; // max kMaxSubParams

    // Main knob value at which the effect is off (0 for most, 1 for the low-pass filter)
    float macro1Off = 0.0f;
};

const EffectInfo& getEffectInfo(int index);

// Text helpers shared by the processor (host automation display) and the UI.
juce::String formatParamValue(const ParamConfig& cfg, float value);
// which: 1 = main knob, 2 = secondary knob. subParams: current advanced values (may be nullptr).
juce::String formatMacroValue(int fxIndex, int which, float value, const float* subParams);

namespace detail
{
    struct DelayLine
    {
        std::vector<float> buf;
        int w = 0;
        int size = 2;

        void init(int maxDelay)
        {
            size = juce::jmax(4, maxDelay + 2);
            buf.assign(static_cast<size_t>(size), 0.0f);
            w = 0;
        }
        void clear() { std::fill(buf.begin(), buf.end(), 0.0f); w = 0; }
        inline void push(float x) noexcept
        {
            buf[static_cast<size_t>(w)] = x;
            if (++w >= size) w = 0;
        }
        // d >= 1 : the sample pushed d pushes ago
        inline float tap(int d) const noexcept
        {
            int i = w - d;
            if (i < 0) i += size;
            return buf[static_cast<size_t>(i)];
        }
        inline float tapFrac(float d) const noexcept
        {
            const int di = static_cast<int>(d);
            const float f = d - static_cast<float>(di);
            const float a = tap(di);
            const float b = tap(di + 1);
            return a + f * (b - a);
        }
    };

    struct Allpass
    {
        DelayLine dl;
        int len = 1;
        float g = 0.5f;
        void init(int n, float gain) { len = n; g = gain; dl.init(n); }
        void clear() { dl.clear(); }
        inline float process(float x) noexcept
        {
            const float d = dl.tap(len);
            const float v = x + g * d;
            dl.push(v);
            return d - g * v;
        }
    };

    struct ModAllpass
    {
        DelayLine dl;
        int len = 1;
        float g = 0.5f;
        void init(int n, int maxMod, float gain) { len = n; g = gain; dl.init(n + maxMod + 4); }
        void clear() { dl.clear(); }
        inline float process(float x, float mod) noexcept
        {
            const float d = dl.tapFrac(static_cast<float>(len) + mod);
            const float v = x + g * d;
            dl.push(v);
            return d - g * v;
        }
    };

    // Keeps an output as loud as its input. Used per effect ("=" button) and for the global Output Match.
    //  - energy averages are bias-corrected, so right after switching on they are accurate at once
    //  - the gain converges fast at the start, then moves slowly (~0.3 s) to follow the average, not the beat
    //  - silent input (e.g. a reverb tail) never lowers or raises the level
    //  - while a knob moves the boost is capped (+1 dB over the input) so releasing a filter can't blast
    struct LevelMatcher
    {
        double energyIn = 0.0, energyOut = 0.0, samples = 0.0;
        float gain = 1.0f;

        void reset() noexcept { energyIn = energyOut = samples = 0.0; gain = 1.0f; }

        // Returns the gain for the END of the block; startGain receives the gain for its first sample.
        float update(double powerIn, double powerOut, int ns, double sr, bool knobMoving, float& startGain) noexcept
        {
            samples += ns;
            const double aE = juce::jmax(1.0 - std::exp(-ns / (0.8 * sr)), static_cast<double>(ns) / samples);
            energyIn += aE * (powerIn - energyIn);
            energyOut += aE * (powerOut - energyOut);

            float target = 1.0f;
            if (powerIn > 1.0e-9 && energyIn > 1.0e-8 && energyOut > 1.0e-12)
                target = juce::jlimit(0.25f, 4.0f, static_cast<float>(std::sqrt(energyIn / energyOut)));

            float newGain = gain;
            if (samples >= 0.05 * sr)   // wait for ~50 ms of audio, the first block alone is too noisy
            {
                const double smooth = samples < 0.5 * sr ? 0.25 : 1.0 - std::exp(-ns / (0.3 * sr));
                newGain = gain + static_cast<float>(smooth) * (target - gain);
            }

            startGain = gain;
            if (knobMoving && powerIn > 1.0e-10 && powerOut > 1.0e-12)
            {
                const float cap = juce::jmax(1.0f, static_cast<float>(std::sqrt(1.26 * powerIn / powerOut)));
                if (newGain > cap) { newGain = cap; startGain = cap; }
            }

            gain = newGain;
            return newGain;
        }
    };

    // Zapdos-style TPT state variable filter (stable under fast modulation)
    struct SVF
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;

        void reset() { ic1 = ic2 = 0.0f; }
        void set(float fc, float q, double sampleRate) noexcept
        {
            fc = juce::jlimit(10.0f, 0.45f * static_cast<float>(sampleRate), fc);
            const float g = std::tan(juce::MathConstants<float>::pi * fc / static_cast<float>(sampleRate));
            k = 1.0f / q;
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        inline void tick(float x, float& lp, float& hp) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            lp = v2;
            hp = x - k * v1 - v2;
        }
    };
} // namespace detail

// -----------------------------------------------------------------------------------------
// Base class. Every effect follows the same rule:
//   macro1 (main knob) == 0  ->  effect is bypassed, zero CPU, buffers cleared.
// -----------------------------------------------------------------------------------------
class EffectBase
{
public:
    virtual ~EffectBase() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset();
    void processBlock(juce::AudioBuffer<float>& buffer);

    void setMacro1(float v) noexcept { sm1.setTargetValue(juce::jlimit(0.0f, 1.0f, v)); }
    void setMacro2(float v) noexcept
    {
        v = juce::jlimit(0.0f, 1.0f, v);
        if (! running) sm2.setCurrentAndTargetValue(v);
        else           sm2.setTargetValue(v);
    }
    void setParam(int i, float v) noexcept { if (i >= 0 && i < kMaxSubParams) subParams[i] = v; }
    void setBpm(double bpm) noexcept { currentBpm = (bpm > 20.0 && bpm < 400.0) ? bpm : 120.0; }
    // Host position in quarter notes at the start of the block (negative = unknown)
    void setPpq(double ppq) noexcept { blockPpq = ppq; }
    // Level match: keeps the volume of THIS effect's output equal to its input (+/-12 dB)
    void setLevelMatch(bool on) noexcept { levelMatch = on; }
    bool isMacroMoving() const noexcept { return sm1.isSmoothing(); }

    static constexpr float kIdle = 0.0005f;

protected:
    virtual void onPrepare() = 0;
    virtual void onReset() = 0;
    virtual void process(juce::AudioBuffer<float>& buffer) = 0;
    // Effects that must keep recording audio while "off" (delay line, stutter capture, tape stop)
    virtual bool needsContinuousProcessing() const noexcept { return false; }
    // Main knob value that means "effect off"
    virtual float offValue() const noexcept { return 0.0f; }
    bool isOffValue(float v) const noexcept { return std::abs(v - offValue()) <= kIdle; }

    juce::SmoothedValue<float> sm1, sm2;
    float subParams[kMaxSubParams] = {};
    double sampleRate = 44100.0;
    double currentBpm = 120.0;
    double blockPpq = -1.0;
    int maxBlock = 512;
    bool running = false;

    // Level match state
    bool levelMatch = false;
    detail::LevelMatcher matcher;
    float matchEngage = 0.0f;
};

#define EASY_FX_DECLARE(ClassName) \
    protected: \
        void onPrepare() override; \
        void onReset() override; \
        void process(juce::AudioBuffer<float>& buffer) override;

// 1/2. LP / HP filter (12 or 24 dB/oct, resonant)
class FXFilter : public EffectBase
{
public:
    explicit FXFilter(bool highPass) : isHP(highPass) {}
    EASY_FX_DECLARE(FXFilter)
    float offValue() const noexcept override { return isHP ? 0.0f : 1.0f; }
private:
    void updateCoefs(float amount, float reso, bool steep);
    bool isHP;
    detail::SVF s1[2], s2[2];
};

// 3. Saturation (anti-aliased)
class FXSaturation : public EffectBase
{
public:
    EASY_FX_DECLARE(FXSaturation)
private:
    float xPrev[2] = { 0, 0 }, dcX[2] = { 0, 0 }, dcY[2] = { 0, 0 }, toneState[2] = { 0, 0 };
    float toneA = 0.5f;
};

// 4. Bitcrusher
class FXBitcrusher : public EffectBase
{
public:
    EASY_FX_DECLARE(FXBitcrusher)
private:
    float hold[2] = { 0, 0 }, lpState[2] = { 0, 0 };
    float counter = 0.0f, toneA = 0.5f;
};

// 5. Chorus
class FXChorus : public EffectBase
{
public:
    EASY_FX_DECLARE(FXChorus)
private:
    std::vector<float> buf[2];
    int size = 2, w = 0;
    float phase = 0.0f, inc = 0.0f;
};

// 6. Phaser
class FXPhaser : public EffectBase
{
public:
    EASY_FX_DECLARE(FXPhaser)
private:
    float z[2][8] = {};
    float fbState[2] = { 0, 0 };
    float phase = 0.0f, inc = 0.0f;
};

// 7. Shutter (tempo-synced tremolo / gate)
class FXShutter : public EffectBase
{
public:
    EASY_FX_DECLARE(FXShutter)
private:
    float phase = 0.0f, gateSm = 1.0f, rateHz = 4.0f;
};

// 8. Auto-pan
class FXAutoPan : public EffectBase
{
public:
    EASY_FX_DECLARE(FXAutoPan)
private:
    float phase = 0.0f, rateHz = 2.0f;
};

// Disperser: cascade of 32 all-pass biquads (frequency dependent delay)
class FXDisperser : public EffectBase
{
public:
    EASY_FX_DECLARE(FXDisperser)
private:
    static constexpr int kStages = 32;
    float a1[kStages] = {}, a2[kStages] = {};
    float z1[2][kStages] = {}, z2[2][kStages] = {};
    float fbState[2] = { 0, 0 };
    void updateCoefs(float freq, float spread);
};

// Rewind: plays the previous tempo-synced slice backwards (with feedback for reverse trails)
class FXRewind : public EffectBase
{
public:
    EASY_FX_DECLARE(FXRewind)
    bool needsContinuousProcessing() const noexcept override { return true; }
private:
    std::vector<float> buf[2][2];   // [slot][channel]
    int size = 2, recSlot = 0, writePos = 0, lenRec = 0, lenPlay = 1, playPos = 0;
    bool havePrev = false;
    float lastWet[2] = { 0, 0 };
    long long lastStep = 0;
    bool stepValid = false;
};

// 10. Tape stop (knob = tape speed brake, spins back up on release)
class FXTapeStop : public EffectBase
{
public:
    EASY_FX_DECLARE(FXTapeStop)
    bool needsContinuousProcessing() const noexcept override { return true; }
private:
    std::vector<float> ring[2];
    int size = 2, w = 0;
    float lag = 0.0f, speed = 1.0f, liveMix = 0.0f;
    float lpState[2] = { 0, 0 };
    float lpA = 1.0f, spCoef = 0.001f;
    float spCoefUp = 0.001f;
};

// 11. Delay (tempo synced, stereo / ping-pong)
class FXDelay : public EffectBase
{
public:
    EASY_FX_DECLARE(FXDelay)
    bool needsContinuousProcessing() const noexcept override { return true; }
private:
    std::vector<float> ring[2];
    int size = 2, w = 0;
    float delaySm = 0.0f;
    float lp1[2] = { 0, 0 }, lp2[2] = { 0, 0 }, hp1[2] = { 0, 0 }, hp2[2] = { 0, 0 };
};

// 12. Reverb (Dattorro plate with modulation)
class FXReverb : public EffectBase
{
public:
    EASY_FX_DECLARE(FXReverb)
    // Always runs: the tail must ring out after Mix is lowered, and Mix must not skip the tank
    bool needsContinuousProcessing() const noexcept override { return true; }
private:
    detail::DelayLine pre;
    detail::Allpass inAp[4];
    detail::ModAllpass apModL, apModR;
    detail::Allpass apL2, apR2;
    detail::DelayLine dL1, dL2, dR1, dR2;

    // Room / Hall / Plate: the tank is allocated for the largest size and re-tuned at runtime
    void setType(int type);
    int curType = -1;
    float typeSize = 1.0f;

    int lenL1 = 1, lenL2 = 1, lenR1 = 1, lenR2 = 1;
    int tapsL[7] = {}, tapsR[7] = {};
    float dampL = 0, dampR = 0;
    float hpL1[2] = {}, hpL2[2] = {}, lpL1[2] = {}, lpL2[2] = {};  // output Low/High cut (2-pole each)
    float lfoL = 0, lfoR = 0;
    float scale = 1.0f;
};

// Particles: granular cloud of small grains scattered from the recent past.
// It has its own delay and reverb, applied to the grain cloud only (the dry sound stays clean).
class FXParticles : public EffectBase
{
public:
    EASY_FX_DECLARE(FXParticles)
    bool needsContinuousProcessing() const noexcept override { return true; }
private:
    struct Grain
    {
        bool active = false;
        float pos = 0.0f, step = 1.0f, gl = 1.0f, gr = 1.0f;
        int len = 1, age = 0;
    };
    static constexpr int kMaxGrains = 48;
    Grain grains[kMaxGrains];
    std::vector<float> ring[2];
    int size = 2, w = 0;
    float untilSpawn = 0.0f;
    juce::uint32 rng = 12345u;

    FXDelay delay;
    FXReverb reverb;
    juce::AudioBuffer<float> cloud, revBuf;
    bool fxActive = false, reverbOn = false;
    float freezeGain = 1.0f;     // fades the frozen cloud out when the input has been silent for a while
    int silentCount = 0;

    inline float rand01() noexcept
    {
        rng = rng * 1664525u + 1013904223u;
        return static_cast<float>(rng >> 8) * (1.0f / 16777216.0f);
    }
    void spawn(int len, float pitchSt, float pitchRandSt, float reverseProb, float spread, float offsetMaxSec);
};

// Build Up: one knob that thins out YOUR sound with a preset combination of
// high-pass, low-pass, stereo width, delay and reverb (no generated sounds).
class FXBuildUp : public EffectBase
{
public:
    EASY_FX_DECLARE(FXBuildUp)
private:
    detail::SVF hp1[2], hp2[2], lp1[2], lp2[2];
    FXDelay delay;     // internal send effects, driven by the Build knob
    FXReverb reverb;
};

// Stereo: widen (mid/side), Haas delay or mono-ize. A crossover keeps the low end untouched.
class FXWidener : public EffectBase
{
public:
    EASY_FX_DECLARE(FXWidener)
    float offValue() const noexcept override { return 0.5f; }   // centre = off, left = mono, right = wider
private:
    detail::SVF lpA[2], lpB[2], hpA[2], hpB[2];   // Linkwitz-Riley 4th order split (2 x 12 dB/oct)
    std::vector<float> ring[2];
    int size = 2, w = 0;
    float delaySm = 0.0f;
};

// Pitch: shifts the pitch up or down, free (cents) or in whole semitones. Knob centre = off.
class FXPitch : public EffectBase
{
public:
    EASY_FX_DECLARE(FXPitch)
    bool needsContinuousProcessing() const noexcept override { return true; }   // keeps its history so engaging is seamless
    float offValue() const noexcept override { return 0.5f; }
private:
    std::vector<float> ring[2];
    int size = 2, w = 0;
    float phase = 0.0f, ratioSm = 1.0f, engageSm = 0.0f;
};

// -----------------------------------------------------------------------------------------
class MultiFXEngine
{
public:
    MultiFXEngine();

    void prepare(double sampleRate, int samplesPerBlock);
    void reset();
    void process(juce::AudioBuffer<float>& buffer);
    void setBpm(double bpm);
    void setPpq(double ppq);
    bool anyKnobMoving() const;

    EffectBase* getEffect(int index);

private:
    std::vector<std::unique_ptr<EffectBase>> effects;
    std::vector<int> order;   // processing order (indices into `effects`)
    double currentBpm = 120.0;
};

} // namespace EasyDSP
