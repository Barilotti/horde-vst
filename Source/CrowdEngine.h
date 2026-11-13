#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>
#include <algorithm>
#include <cstdint>

// HORDE crowd engine: turns up to 8 mono "take" inputs into up to 100 virtual voices.
// Each voice = delayed + pitch-shifted (dual-tap rotating delay) + timbre-colored +
// distance-filtered + panned copy of one take. All voices share one stereo reverb.

namespace horde
{
constexpr int kMaxVoices = 100;
constexpr int kMaxTakes  = 8;

// A loaded take file (mono, at its own sample rate) for "files" mode.
struct Take
{
    juce::AudioBuffer<float> audio;     // 1 channel
    double sampleRate = 48000.0;
    std::vector<float> env;             // RMS per 10 ms hop, normalised to 0..1
    int hop = 480;
    juce::String name, path;
    juce::String group;                 // crowd group name, "" = none (message thread only)
    std::atomic<bool> female { false };

    // Speech analysis (see analyseTake): which 10 ms hops carry voice, and phrase boundaries.
    std::vector<uint8_t>  voiced;        // 1 = voice, 0 = silence / noise / breath
    std::vector<uint16_t> silentAhead;   // number of unvoiced hops starting at this hop
    std::vector<std::pair<int, int>> phrases;   // [startHop, endHop)
    int voicedHops = 0;
};

// Finds the useful signal in a take: band-limited (voice band) energy against an adaptive
// noise floor, with hysteresis, gap filling and removal of tiny blips (clicks, breaths).
void analyseTake (Take&);

constexpr int kMaxGroups = 16;

struct Library
{
    std::vector<std::shared_ptr<Take>> takes;
    std::vector<int> groupOf;            // per take: 0 = no group, 1..kMaxGroups
};

// Output channel order used inside the engine.
enum Speaker { spL = 0, spR, spC, spLFE, spLs, spRs, kNumSpeakers };
enum class OutFormat { mono, stereo, surround51 };

struct Params
{
    int   voices      = 40;
    int   takes       = 0;      // 0 = auto (use inputs that carry signal)
    int   maleTakes   = 3;
    float balance     = 0.0f;   // -1 (male only) .. +1 (female only)
    float delayMinMs  = 12.0f;
    float delaySpreadMs = 90.0f;
    float scatterMs   = 700.0f; // extra per-voice time offset so words don't line up
    float loose       = 0.5f;   // slow per-voice tempo wander (pitch-compensated)
    float pitchSpread = 18.0f;  // cents
    float drift       = 6.0f;   // cents
    float timbre      = 0.5f;
    float humanize    = 0.5f;
    float distance    = 0.35f;
    float depthSpread = 0.7f;
    float width       = 1.0f;
    float reverb      = 0.35f;
    float room        = 0.7f;
    int   roomType    = 2;      // 0 Field, 1 Courtyard, 2 Hall, 3 Cathedral
    float dry         = 0.0f;
    float wetDb       = 0.0f;
    int   seed        = 1;
    float mix         = 0.5f;   // files mode: how often voices jump to another take/phrase
    int   group       = 0;      // files mode: 0 = all loaded files, k = only takes of group k
    // output
    OutFormat format  = OutFormat::stereo;
    std::array<int, kNumSpeakers> chanIndex { 0, 1, -1, -1, -1, -1 };   // buffer channel per speaker, -1 = absent
    std::array<float, kNumSpeakers> chanMix  { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };  // 0 dry .. 0.5 both .. 1 crowd
    std::array<float, kNumSpeakers> chanGainDb {};
    // transport (files mode)
    bool   hasTransport = false;
    bool   playing      = true;
    juce::int64 timeInSamples = 0;
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void setPeak (double sr, double f, double q, double gainDb)
    {
        const double A  = std::pow (10.0, gainDb / 40.0);
        const double w0 = juce::MathConstants<double>::twoPi * f / sr;
        const double al = std::sin (w0) / (2.0 * q);
        const double cs = std::cos (w0);
        const double a0 = 1.0 + al / A;
        b0 = (float) ((1.0 + al * A) / a0);
        b1 = (float) ((-2.0 * cs) / a0);
        b2 = (float) ((1.0 - al * A) / a0);
        a1 = (float) ((-2.0 * cs) / a0);
        a2 = (float) ((1.0 - al / A) / a0);
    }

    inline float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setLowpass (double sr, double f, double q)
    {
        const double w0 = juce::MathConstants<double>::twoPi * f / sr;
        const double al = std::sin (w0) / (2.0 * q);
        const double cs = std::cos (w0);
        const double a0 = 1.0 + al;
        b0 = (float) ((1.0 - cs) * 0.5 / a0);
        b1 = (float) ((1.0 - cs) / a0);
        b2 = b0;
        a1 = (float) ((-2.0 * cs) / a0);
        a2 = (float) ((1.0 - al) / a0);
    }

    void setHighpass (double sr, double f, double q)
    {
        const double w0 = juce::MathConstants<double>::twoPi * f / sr;
        const double al = std::sin (w0) / (2.0 * q);
        const double cs = std::cos (w0);
        const double a0 = 1.0 + al;
        b0 = (float) ((1.0 + cs) * 0.5 / a0);
        b1 = (float) (-(1.0 + cs) / a0);
        b2 = b0;
        a1 = (float) ((-2.0 * cs) / a0);
        a2 = (float) ((1.0 - al) / a0);
    }

    void reset() { z1 = z2 = 0; }
};

// Smooth random wander in [-1, 1]: linear segments between random targets.
struct Wander
{
    float cur = 0, target = 0, step = 0;
    int   remaining = 0;

    inline float next (juce::Random& r, double sr, float minSec, float maxSec) noexcept
    {
        if (remaining <= 0)
        {
            target    = r.nextFloat() * 2.0f - 1.0f;
            remaining = juce::jmax (1, (int) (sr * (minSec + r.nextFloat() * (maxSec - minSec))));
            step      = (target - cur) / (float) remaining;
        }
        --remaining;
        cur += step;
        return cur;
    }
};

struct Voice
{
    // Per-voice random "identity" in [0,1)
    float uScatter = 0;
    float uDelay = 0, uPitch = 0, uPan = 0, uDist = 0, uF1 = 0, uF2 = 0, uG1 = 0, uG2 = 0;

    int   slot = 0;
    bool  female = false;

    float baseDelay = -1.0f;   // samples, smoothed
    float prevDelay = -1.0f;   // total delay at previous sample (for pitch compensation)
    float phase = 0.0f;        // pitch shifter phase [0,1)
    float active = 0.0f;       // fade in/out 0..1

    Wander pitchWander, ampWander, timeWander;
    juce::Random rng;

    Biquad f1, f2;
    float lp = 0.0f;

    // files mode
    std::shared_ptr<Take> take;
    double libPos = 0.0;
    float  segEnv = 0.0f;
    int    segRemaining = 0;
    int    overtime = 0;            // samples played past the segment end while finishing a phrase
    bool   forceSwitch = false;     // take left the active group: fade out and move on now

    // Visual feedback (read by GUI thread)
    std::atomic<float> vizPan { 0 }, vizAz { 0 }, vizDist { 0 }, vizLevel { 0 };
    std::atomic<bool>  vizFemale { false }, vizOn { false };
};

class CrowdEngine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (const juce::AudioBuffer<float>& in, int numInputChannels,
                  juce::AudioBuffer<float>& out, int numSamples, const Params& p);

    const Voice& voice (int i) const { return voices[(size_t) i]; }
    float inputLevel (int ch) const { return inLevels[(size_t) ch].load(); }
    float outputLevel (int speaker) const { return outLevels[(size_t) speaker].load(); }
    bool  slotActive (int ch) const { return slotOn[(size_t) ch].load(); }

    // Called from the message thread; the audio thread picks it up on the next block.
    void setLibrary (std::shared_ptr<const Library> l) { std::atomic_store (&library, std::move (l)); }
    bool filesMode() const { auto l = std::atomic_load (&library); return l && ! l->takes.empty(); }

private:
    void reroll (int seed);
    void newSegment (Voice&, const Library&, const int* cand, int nCand, float mix, bool firstOne);
    void resyncFiles (int seed, juce::int64 timeKey);

    std::shared_ptr<const Library> library;
    bool wasPlaying = false;
    juce::int64 expectedTime = -1;

    double sr = 48000.0;
    int ringSize = 1 << 17, ringMask = (1 << 17) - 1;
    std::array<std::vector<float>, kMaxTakes> rings;
    int writePos = 0;

    std::array<Voice, kMaxVoices> voices;
    int currentSeed = -1;

    juce::Reverb reverb, reverbRear;
    juce::AudioBuffer<float> sendBuf, busBuf;
    Biquad lfeLp1, lfeLp2;
    std::array<std::atomic<float>, kMaxTakes> inLevels {};
    std::array<std::atomic<float>, kNumSpeakers> outLevels {};
    std::array<float, kNumSpeakers> chanGainSmoothed { 1, 1, 1, 1, 1, 1 };
    std::array<std::atomic<bool>, kMaxTakes> slotOn {};
    std::array<int, kMaxTakes> slotHold {};
    float wetGainSmoothed = 1.0f;
};
} // namespace horde
