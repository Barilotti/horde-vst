#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "CrowdEngine.h"
#include "ReaperBridge.h"

class HordeProcessor : public juce::AudioProcessor, private juce::AsyncUpdater, private juce::Timer
{
public:
    HordeProcessor();
    ~HordeProcessor() override { stopTimer(); freeCaptureChunks(); }

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

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout (HordeProcessor* owner);
    void processorLayoutsChanged() override { updateOutputMap(); }

    // ---- Output format, chosen by the DAW track (mono / stereo / 5.1)
    horde::OutFormat getOutFormat() const { return (horde::OutFormat) outFormat.load(); }
    static const char* speakerId (int s);       // "L", "R", "C", "LFE", "Ls", "Rs"

    // ---- Files mode (message thread only)
    void addFiles (const juce::StringArray& paths);
    void removeTake (int index);
    void toggleFemale (int index);

    // ---- Crowd groups ("zombies", "screaming crowd", ...). The crowd plays only the active group.
    const juce::StringArray& getGroups() const { return groups; }
    juce::String createGroup (const juce::String& name);          // returns the unique name used
    void renameGroup (int groupIndex, const juce::String& name);  // 0-based
    void deleteGroup (int groupIndex);
    void setTakeGroup (int takeIndex, const juce::String& group); // "" = no group
    int  getActiveGroup() const;                                  // 0 = all files (Custom), k = groups[k-1]
    void setActiveGroup (int g);
    void stepActiveGroup (int delta);
    juce::String getActiveGroupLabel() const;                     // None / Custom / group name
    int  countTakesInGroup (int g) const;
    juce::String groupNameForParam (int v) const;
    const std::vector<std::shared_ptr<horde::Take>>& getTakes() const { return takes; }
    std::function<void()> onTakesChanged;

    // ---- REAPER integration
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return &reaperBridge; }
    ReaperBridge::Result linkChildTracks();
    bool inReaper() const { return reaperBridge.available(); }

    // ---- CAPTURE: record the takes from the plugin input during one playback pass (any DAW)
    enum class CaptureState { idle, armed, recording };
    void toggleCapture();
    CaptureState getCaptureState() const;
    double getCapturedSeconds() const { return (double) capLen.load() / juce::jmax (1.0, getSampleRate()); }
    juce::String lastCaptureMessage;
    std::function<void (const juce::String&)> onCaptureFinished;

private:
    horde::CrowdEngine engine;
    std::atomic<int> numInputsSeen { 0 };
    std::vector<std::shared_ptr<horde::Take>> takes;
    ReaperBridge reaperBridge;
    juce::AudioFormatManager formats;
    void publishTakes();
    void updateOutputMap();
    std::atomic<int> outFormat { (int) horde::OutFormat::stereo };
    std::array<std::atomic<int>, horde::kNumSpeakers> outIndex {};
    juce::StringArray groups;
    juce::StringPairArray pendingTrackGroups;   // track take path -> group, applied when tracks are (re)linked
    mutable juce::SpinLock groupNamesLock;
    juce::StringArray groupNamesForHost;
    void groupsChanged();
    void handleAsyncUpdate() override;
    static std::shared_ptr<horde::Take> makeTake (const juce::AudioBuffer<float>&, double sampleRate,
                                                  const juce::String& name, const juce::String& path, bool female);
    bool linkedToTracks = false;

    void timerCallback() override;
    void finishCapture();
    static constexpr int kCapChunk = 65536;
    static constexpr int kCapMaxChunks = 1024;           // ~23 min at 48 kHz
    struct CapChannel { std::array<std::atomic<float*>, kCapMaxChunks> chunks {}; };
    std::array<CapChannel, horde::kMaxTakes> cap;
    std::atomic<int>  capLen { 0 }, capChannels { 0 };
    std::atomic<bool> capArmed { false }, capRecording { false }, capStopRequest { false }, capDone { false };
    double capSampleRate = 48000.0;
    void freeCaptureChunks();

    struct Raw
    {
        std::atomic<float>* voices; std::atomic<float>* takes; std::atomic<float>* maleTakes;
        std::atomic<float>* balance; std::atomic<float>* delayMin; std::atomic<float>* delaySpread;
        std::atomic<float>* pitchSpread; std::atomic<float>* drift; std::atomic<float>* timbre;
        std::atomic<float>* humanize; std::atomic<float>* distance; std::atomic<float>* depthSpread;
        std::atomic<float>* width; std::atomic<float>* reverb; std::atomic<float>* room;
        std::atomic<float>* dry; std::atomic<float>* wet; std::atomic<float>* seed; std::atomic<float>* roomType; std::atomic<float>* scatter; std::atomic<float>* loose; std::atomic<float>* mix;
        std::atomic<float>* group;
        std::array<std::atomic<float>*, horde::kNumSpeakers> lvl, chMix;
    } raw {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HordeProcessor)
};
