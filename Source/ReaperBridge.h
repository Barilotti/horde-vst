#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Talks to REAPER through its VST3 host extension (IReaperHostApplication) so the plugin
// can route the take tracks that sit inside its folder track into its inputs.
class ReaperBridge : public juce::VST3ClientExtensions
{
public:
    struct Result
    {
        bool ok = false;
        juce::String message;
        int males = 0, females = 0;

        struct TrackAudio
        {
            juce::String name;
            bool female = false;
            double sampleRate = 48000.0;
            juce::AudioBuffer<float> mono;
        };
        std::vector<TrackAudio> audio;   // full audio of every take track, read from its items
    };

    void setIHostApplication (Steinberg::FUnknown* host) override;
    bool available() const { return reaper != nullptr; }

    // Message thread only.
    Result linkChildTracks();

private:
    void* getApi (const char* name) const;
    void* reaper = nullptr;   // reaper::IReaperHostApplication*
};
