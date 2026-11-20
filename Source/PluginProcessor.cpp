#include "PluginProcessor.h"
#include "PluginEditor.h"

EasyEfxAudioProcessor::EasyEfxAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    matchParam = apvts.getRawParameterValue(getMatchParamID());
    bypassParam = apvts.getRawParameterValue(getBypassParamID());

    for (int i = 0; i < EasyDSP::kNumEffects; ++i)
    {
        macro1Params[i] = apvts.getRawParameterValue(getMacro1ParamID(i));
        macro2Params[i] = apvts.getRawParameterValue(getMacro2ParamID(i));
        fxMatchParams[i] = apvts.getRawParameterValue(getFxMatchParamID(i));

        const int numSub = static_cast<int>(EasyDSP::getEffectInfo(i).params.size());
        for (int p = 0; p < numSub; ++p)
            subParamsArray[i][p] = apvts.getRawParameterValue(getSubParamID(i, p));
    }
}

EasyEfxAudioProcessor::~EasyEfxAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout EasyEfxAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    for (int i = 0; i < EasyDSP::kNumEffects; ++i)
    {
        const auto& info = EasyDSP::getEffectInfo(i);
        const int fx = i;

        // Main knob: 0 = effect off. This is the one to automate in the playlist.
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(getMacro1ParamID(i), 1),
            info.name + " " + info.macro1Label,
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f),
            info.macro1Off,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                [fx](float v, int) { return EasyDSP::formatMacroValue(fx, 1, v, nullptr); })));

        // Secondary knob (shape of the effect)
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(getMacro2ParamID(i), 1),
            info.name + " " + info.macro2Label,
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f),
            info.macro2Default,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                [fx](float v, int) { return EasyDSP::formatMacroValue(fx, 2, v, nullptr); })));

        // Advanced parameters (settings panel)
        for (int p = 0; p < static_cast<int>(info.params.size()); ++p)
        {
            const auto cfg = info.params[static_cast<size_t>(p)];
            const juce::String pid = getSubParamID(i, p);
            const juce::String pname = info.name + " " + cfg.name;

            if (! cfg.options.isEmpty())
            {
                params.push_back(std::make_unique<juce::AudioParameterChoice>(
                    juce::ParameterID(pid, 1), pname, cfg.options,
                    juce::roundToInt(cfg.defaultVal)));
            }
            else
            {
                params.push_back(std::make_unique<juce::AudioParameterFloat>(
                    juce::ParameterID(pid, 1), pname,
                    juce::NormalisableRange<float>(cfg.minVal, cfg.maxVal, 0.0f, cfg.skew),
                    cfg.defaultVal,
                    juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                        [cfg](float v, int) { return EasyDSP::formatParamValue(cfg, v); })));
            }
        }
    }

    // Per-effect level match toggles ("=" button on each tile)
    // On by default for the effects that change the loudness the most: saturate, bitcrush, chorus,
    // phaser and disperser.
    for (int i = 0; i < EasyDSP::kNumEffects; ++i)
    {
        using T = EasyDSP::EffectType;
        const auto type = EasyDSP::getEffectInfo(i).type;
        const bool onByDefault = type == T::Saturation || type == T::Bitcrusher || type == T::Chorus
                              || type == T::Phaser || type == T::Disperser;

        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(getFxMatchParamID(i), 1),
            EasyDSP::getEffectInfo(i).name + " Level Match", onByDefault));
    }

    // Global bypass: the plugin lets the input through untouched
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(getBypassParamID(), 1), "Bypass", false));

    // Global: match the output loudness to the input loudness
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(getMatchParamID(), 1), "Output Match", false));

    return { params.begin(), params.end() };
}

const juce::String EasyEfxAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool EasyEfxAudioProcessor::acceptsMidi() const { return false; }
bool EasyEfxAudioProcessor::producesMidi() const { return false; }
bool EasyEfxAudioProcessor::isMidiEffect() const { return false; }
double EasyEfxAudioProcessor::getTailLengthSeconds() const
{
    // The host may stop calling us once this time has passed after the input ends, which would cut a
    // long reverb tail. Report what is actually active (a hall can ring for about 12 seconds).
    using T = EasyDSP::EffectType;
    auto active = [this](T t)
    {
        auto* p = macro1Params[static_cast<int>(t)];
        return p != nullptr && p->load() > 0.002f;
    };

    if (active(T::Reverb)) return 15.0;
    if (active(T::Delay) || active(T::Particles) || active(T::BuildUp)) return 8.0;
    return 1.0;
}

int EasyEfxAudioProcessor::getNumPrograms() { return 1; }
int EasyEfxAudioProcessor::getCurrentProgram() { return 0; }
void EasyEfxAudioProcessor::setCurrentProgram(int /*index*/) {}
const juce::String EasyEfxAudioProcessor::getProgramName(int /*index*/) { return {}; }
void EasyEfxAudioProcessor::changeProgramName(int /*index*/, const juce::String& /*newName*/) {}

void EasyEfxAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    matcher.reset();
    matchGainDb.store(0.0f);
    dryCopy.setSize(2, juce::jmax(samplesPerBlock, 64));
    bypassMix = (bypassParam != nullptr && bypassParam->load() > 0.5f) ? 1.0f : 0.0f;   // no fade at load
    bypassCleared = false;
    engine.prepare(sampleRate, samplesPerBlock);
}

void EasyEfxAudioProcessor::releaseResources()
{
    engine.reset();
}

bool EasyEfxAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void EasyEfxAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto ch = totalNumInputChannels; ch < totalNumOutputChannels; ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    // Host tempo (used by shutter, autopan, stutter and delay)
    double hostBpm = 120.0;
    double hostPpq = -1.0;    // position in quarter notes; lets Build Up and Rewind lock to the beat grid
    if (auto* ph = getPlayHead())
    {
        if (auto posOpt = ph->getPosition())
        {
            if (auto bpmOpt = posOpt->getBpm())
                hostBpm = *bpmOpt;
            if (auto ppqOpt = posOpt->getPpqPosition())
                hostPpq = *ppqOpt;
        }
    }
    currentBpm = hostBpm;
    engine.setBpm(hostBpm);
    engine.setPpq(hostPpq);

    // Push automation / knob values to the DSP (smoothed inside each effect)
    for (int i = 0; i < EasyDSP::kNumEffects; ++i)
    {
        if (auto* fx = engine.getEffect(i))
        {
            fx->setMacro1(macro1Params[i]->load());
            fx->setMacro2(macro2Params[i]->load());
            fx->setLevelMatch(fxMatchParams[i]->load() > 0.5f);

            for (int p = 0; p < EasyDSP::kMaxSubParams; ++p)
                if (subParamsArray[i][p] != nullptr)
                    fx->setParam(p, subParamsArray[i][p]->load());
        }
    }

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // ---- Global bypass: a 12 ms cross-fade to the untouched input, then the effects stop running ----
    const bool bypass = bypassParam != nullptr && bypassParam->load() > 0.5f;
    if (bypass && bypassMix >= 1.0f)
    {
        if (! bypassCleared)
        {
            engine.reset();           // clean slate (no stale tails) for when bypass is switched off
            matcher.reset();
            matchGainDb.store(0.0f);
            bypassCleared = true;
        }
        return;                       // the buffer is left exactly as it came in
    }
    bypassCleared = false;

    const bool crossfading = bypass || bypassMix > 0.0f;
    if (crossfading && numSamples > 0)
    {
        dryCopy.setSize(numChannels, numSamples, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch)
            dryCopy.copyFrom(ch, 0, buffer, ch, 0, numSamples);
    }

    // ---- Output Match: measure the input loudness before the effects ----
    const bool match = matchParam != nullptr && matchParam->load() > 0.5f;

    auto meanPower = [&]() -> double
    {
        if (numSamples == 0 || numChannels == 0) return 0.0;
        double sum = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* d = buffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i) sum += static_cast<double>(d[i]) * d[i];
        }
        return sum / static_cast<double>(numSamples * numChannels);
    };

    const double powerIn = match ? meanPower() : 0.0;
    const bool knobMoving = match && engine.anyKnobMoving();

    engine.process(buffer);

    if (match && numSamples > 0)
    {
        float startGain = matcher.gain;
        const float newGain = matcher.update(powerIn, meanPower(), numSamples, currentSampleRate, knobMoving, startGain);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* d = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                const float g = startGain + (newGain - startGain) * static_cast<float>(i + 1) / static_cast<float>(numSamples);
                float y = d[i] * g;

                // soft knee near full scale: the boost must not clip the master
                const float ay = std::abs(y);
                if (ay > 0.9f) y = std::copysign(0.9f + 0.1f * std::tanh((ay - 0.9f) / 0.1f), y);
                d[i] = y;
            }
        }

        matchGainDb.store(juce::Decibels::gainToDecibels(newGain));
    }
    else if (matcher.gain != 1.0f && numSamples > 0)
    {
        // switched off: ramp back to unity gain over this block (no click)
        const float from = matcher.gain;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* d = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
                d[i] *= from + (1.0f - from) * static_cast<float>(i + 1) / static_cast<float>(numSamples);
        }
        matcher.reset();
        matchGainDb.store(0.0f);
    }
    else if (! match)
    {
        matcher.reset();
        matchGainDb.store(0.0f);
    }

    // ---- finish the bypass cross-fade (processed -> dry, or dry -> processed) ----
    if (crossfading && numSamples > 0)
    {
        const float step = 1.0f / static_cast<float>(0.012 * currentSampleRate);
        const float goal = bypass ? 1.0f : 0.0f;
        float m = bypassMix;
        for (int i = 0; i < numSamples; ++i)
        {
            if (m < goal)      m = juce::jmin(goal, m + step);
            else if (m > goal) m = juce::jmax(goal, m - step);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* d = buffer.getWritePointer(ch);
                d[i] = d[i] * (1.0f - m) + dryCopy.getSample(ch, i) * m;
            }
        }
        bypassMix = m;
    }
}

bool EasyEfxAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* EasyEfxAudioProcessor::createEditor()
{
    return new EasyEfxAudioProcessorEditor(*this);
}

void EasyEfxAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void EasyEfxAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyEfxAudioProcessor();
}
