#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Module.h"

class PrismRackProcessor : public juce::AudioProcessor
{
public:
    PrismRackProcessor();
    ~PrismRackProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Prism Rack"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // --- editor <-> audio thread communication ---
    std::atomic<float> displayBpm { 120.0f };
    std::atomic<bool> autoRequested { false }, autoDone { false };
    std::atomic<int> autoResult { 2 };
    std::atomic<float> autoProgress { 0.0f };

    static constexpr int kStateVersion = 1;

private:
    struct ModuleParams
    {
        juce::AudioParameterChoice* type = nullptr;
        std::atomic<float>* amount = nullptr, *knobB = nullptr, *sec = nullptr, *vol = nullptr, *bypass = nullptr, *alwaysOn = nullptr;
    };
    std::array<ModuleParams, prism::NumModules> mp;
    std::atomic<float>* pMix, *pOutput, *pHeadroom, *pOrder, *pBypass, *pTrails, *pDriftMacro, *pSync, *pBpm, *pFilterMode, *pRevSpeed;

    std::array<std::unique_ptr<prism::Module>, prism::NumModules> modules;
    juce::AudioBuffer<float> work;
    std::vector<float> hgBuf;
    prism::dsp::DelayLine dryDelay[2];
    int totalLatency = 0;
    juce::SmoothedValue<float> headroomS, mixS, outS, bypassS;
    bool wasPlaying = false;
    uint32_t sanitizeCount = 0;

    // auto-headroom measurement
    int autoSamplesLeft = 0; float autoPeak = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismRackProcessor)
};
