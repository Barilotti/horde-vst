#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "CrowdEngine.h"
#include "ReaperBridge.h"

class HordeProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    HordeProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "HORDE"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    const horde::CrowdEngine& getEngine() const { return engine; }
    int getNumConnectedInputs() const { return numInputsSeen.load(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- Files mode (message thread only)
    void addFiles (const juce::StringArray& paths);
    void removeTake (int index);
    void toggleFemale (int index);
    const std::vector<std::shared_ptr<horde::Take>>& getTakes() const { return takes; }
    std::function<void()> onTakesChanged;

    // ---- REAPER integration
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return &reaperBridge; }
    ReaperBridge::Result linkChildTracks();
    bool inReaper() const { return reaperBridge.available(); }

private:
    horde::CrowdEngine engine;
    std::atomic<int> numInputsSeen { 0 };
    std::vector<std::shared_ptr<horde::Take>> takes;
    ReaperBridge reaperBridge;
    juce::AudioFormatManager formats;
    void publishTakes();
    void handleAsyncUpdate() override;
    static std::shared_ptr<horde::Take> makeTake (const juce::AudioBuffer<float>&, double sampleRate,
                                                  const juce::String& name, const juce::String& path, bool female);
    bool linkedToTracks = false;

    struct Raw
    {
        std::atomic<float>* voices; std::atomic<float>* takes; std::atomic<float>* maleTakes;
        std::atomic<float>* balance; std::atomic<float>* delayMin; std::atomic<float>* delaySpread;
        std::atomic<float>* pitchSpread; std::atomic<float>* drift; std::atomic<float>* timbre;
        std::atomic<float>* humanize; std::atomic<float>* distance; std::atomic<float>* depthSpread;
        std::atomic<float>* width; std::atomic<float>* reverb; std::atomic<float>* room;
        std::atomic<float>* dry; std::atomic<float>* wet; std::atomic<float>* seed; std::atomic<float>* roomType; std::atomic<float>* scatter; std::atomic<float>* loose; std::atomic<float>* mix;
    } raw {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HordeProcessor)
};
