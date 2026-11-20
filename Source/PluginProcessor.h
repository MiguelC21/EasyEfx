#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/Effects.h"

class EasyEfxAudioProcessor : public juce::AudioProcessor
{
public:
    EasyEfxAudioProcessor();
    ~EasyEfxAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    EasyDSP::MultiFXEngine& getEngine() { return engine; }
    double getCurrentBpm() const noexcept { return currentBpm; }

    // Global "Output Match": keeps the output loudness equal to the input loudness
    static juce::String getMatchParamID() { return "global_match"; }
    static juce::String getBypassParamID() { return "global_bypass"; }
    // Per-effect "=" button: keeps that effect's output as loud as its input
    static juce::String getFxMatchParamID(int fxIndex) { return "fx_" + juce::String(fxIndex) + "_match"; }
    float getMatchGainDb() const noexcept { return matchGainDb.load(); }

    // Main knob (0 = effect off) - this is the one to automate in the playlist
    static juce::String getMacro1ParamID(int fxIndex) { return "fx_" + juce::String(fxIndex) + "_macro1"; }
    static juce::String getMacro2ParamID(int fxIndex) { return "fx_" + juce::String(fxIndex) + "_macro2"; }
    static juce::String getSubParamID(int fxIndex, int subIdx) { return "fx_" + juce::String(fxIndex) + "_p" + juce::String(subIdx); }

private:
    juce::AudioProcessorValueTreeState apvts;
    EasyDSP::MultiFXEngine engine;
    double currentBpm = 120.0;

    // Output Match state
    std::atomic<float>* matchParam = nullptr;
    std::atomic<float> matchGainDb { 0.0f };
    double currentSampleRate = 44100.0;
    EasyDSP::detail::LevelMatcher matcher;

    // Global bypass state (0 = effects, 1 = untouched input)
    std::atomic<float>* bypassParam = nullptr;
    float bypassMix = 0.0f;
    bool bypassCleared = false;
    juce::AudioBuffer<float> dryCopy;

    std::atomic<float>* fxMatchParams[EasyDSP::kNumEffects] = {};
    std::atomic<float>* macro1Params[EasyDSP::kNumEffects] = {};
    std::atomic<float>* macro2Params[EasyDSP::kNumEffects] = {};
    std::atomic<float>* subParamsArray[EasyDSP::kNumEffects][EasyDSP::kMaxSubParams] = {};

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EasyEfxAudioProcessor)
};
