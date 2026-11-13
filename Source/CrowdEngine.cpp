#include "CrowdEngine.h"
#include <limits>

namespace horde
{
void CrowdEngine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    const int needed = (int) (sr * 4.2) + maxBlock + 16; // delay + scatter + loose + window, with headroom
    ringSize = juce::nextPowerOfTwo (needed);
    ringMask = ringSize - 1;
    for (auto& r : rings)
        r.assign ((size_t) ringSize, 0.0f);

    sendBuf.setSize (4, maxBlock);   // front L/R, rear L/R reverb sends
    busBuf.setSize (5, maxBlock);    // dry voices: L R C Ls Rs
    reverb.setSampleRate (sr);
    reverbRear.setSampleRate (sr);
    lfeLp1.setLowpass (sr, 120.0, 0.54);
    lfeLp2.setLowpass (sr, 120.0, 1.31);
    currentSeed = -1;
    reset();
}

void CrowdEngine::reset()
{
    for (auto& r : rings)
        std::fill (r.begin(), r.end(), 0.0f);
    writePos = 0;
    reverb.reset();
    reverbRear.reset();
    lfeLp1.reset(); lfeLp2.reset();
    slotHold.fill (0);
    for (auto& s : slotOn) s.store (false);
    for (auto& v : voices)
    {
        v.f1.reset(); v.f2.reset();
        v.lp = 0; v.baseDelay = -1.0f; v.prevDelay = -1.0f; v.active = 0.0f;
        v.segEnv = 0.0f; v.segRemaining = 0; v.overtime = 0; v.forceSwitch = false;
    }
}

void CrowdEngine::reroll (int seed)
{
    currentSeed = seed;
    juce::Random master ((juce::int64) seed * 7919 + 17);
    for (auto& v : voices)
    {
        v.uDelay = master.nextFloat();
        v.uScatter = master.nextFloat();
        v.uPitch = master.nextFloat();
        v.uPan   = master.nextFloat();
        v.uDist  = master.nextFloat();
        v.uF1    = master.nextFloat();
        v.uF2    = master.nextFloat();
        v.uG1    = master.nextFloat();
        v.uG2    = master.nextFloat();
        v.phase  = master.nextFloat();
        v.rng.setSeed (master.nextInt64());
    }
}

void CrowdEngine::resyncFiles (int seed, juce::int64 timeKey)
{
    // Same seed + same timeline position => the same crowd every time playback starts there.
    for (int vi = 0; vi < kMaxVoices; ++vi)
    {
        auto& v = voices[(size_t) vi];
        v.rng.setSeed ((juce::int64) seed * 1000003 + (juce::int64) vi * 7919 + timeKey * 31337);
        v.pitchWander = {}; v.ampWander = {}; v.timeWander = {};
        v.take.reset();
        v.segEnv = 0.0f;
        v.segRemaining = 0;
        v.overtime = 0;
        v.forceSwitch = false;
    }
}

void CrowdEngine::newSegment (Voice& v, const Library& lib, const int* cand, int nCand, float mix, bool firstOne)
{
    v.take = lib.takes[(size_t) cand[v.rng.nextInt (nCand)]];
    const auto& t = *v.take;
    const int len = t.audio.getNumSamples();

    double pos = 0.0;
    if (! t.phrases.empty() && t.voicedHops > 0)
    {
        // Pick a phrase (longer ones more often). Normally start at its beginning, so the voice
        // shouts a whole word/phrase; when the crowd first appears start anywhere inside one,
        // so voices don't all attack together.
        int r = v.rng.nextInt (t.voicedHops);
        const auto* ph = &t.phrases.back();
        for (auto& p : t.phrases)
        {
            const int l = p.second - p.first;
            if (r < l) { ph = &p; break; }
            r -= l;
        }
        const int l = ph->second - ph->first;
        int startHop = ph->first;
        if (firstOne)
            startHop += juce::jlimit (0, juce::jmax (0, l - 1), r);
        else if (l > 150 && v.rng.nextFloat() < 0.5f)    // long phrase (> 1.5 s): sometimes join mid-way
            startHop += v.rng.nextInt (l - 100);
        pos = (double) juce::jmax (0, startHop * t.hop - t.hop * 2);   // short lead-in, don't clip the onset
    }
    else
    {
        // no analysis available: pick a start point that has some level
        for (int tries = 0; tries < 12; ++tries)
        {
            pos = (double) v.rng.nextFloat() * (double) juce::jmax (1, len - (int) (t.sampleRate * 0.5));
            const size_t e = (size_t) juce::jlimit (0, (int) t.env.size() - 1, (int) (pos / t.hop));
            if (t.env.empty() || t.env[e] > 0.12f)
                break;
        }
    }
    v.libPos = juce::jlimit (0.0, (double) juce::jmax (0, len - 8), pos);

    const float segSec = (12.0f - 10.5f * mix) * (0.6f + 0.8f * v.rng.nextFloat());
    v.segRemaining = (int) (segSec * sr * (firstOne ? v.rng.nextFloat() : 1.0f));
    v.segEnv = 0.0f;
    v.overtime = 0;
    v.forceSwitch = false;
    v.f1.reset(); v.f2.reset(); v.lp = 0.0f;
}

//==============================================================================
void analyseTake (Take& t)
{
    t.voiced.clear(); t.silentAhead.clear(); t.phrases.clear(); t.voicedHops = 0;
    const int hop = juce::jmax (1, t.hop);
    const int nh = t.audio.getNumSamples() / hop;
    if (nh < 4) return;

    // energy in the voice band (150 Hz - 4 kHz): ignores rumble, hum and hiss
    Biquad hp, lp;
    hp.setHighpass (t.sampleRate, 150.0, 0.707);
    lp.setLowpass (t.sampleRate, 4000.0, 0.707);
    const float* d = t.audio.getReadPointer (0);
    std::vector<float> db ((size_t) nh);
    for (int h = 0; h < nh; ++h)
    {
        double acc = 0.0;
        for (int i = 0; i < hop; ++i)
        {
            const float x = lp.process (hp.process (d[h * hop + i]));
            acc += (double) x * x;
        }
        db[(size_t) h] = (float) (10.0 * std::log10 (acc / hop + 1.0e-12));
    }

    // adaptive threshold: above the noise floor, but never more than 35 dB / less than 18 dB under the loud parts
    std::vector<float> sorted (db);
    std::sort (sorted.begin(), sorted.end());
    const float floorDb = sorted[(size_t) (nh * 10 / 100)];
    const float peakDb  = sorted[(size_t) juce::jmin (nh - 1, nh * 98 / 100)];
    const float thrOn   = juce::jmax (-70.0f, juce::jlimit (peakDb - 35.0f, peakDb - 18.0f, floorDb + 9.0f));
    const float thrOff  = thrOn - 5.0f;

    std::vector<uint8_t> vo ((size_t) nh, 0);
    bool on = false;
    for (int h = 0; h < nh; ++h)
    {
        on = on ? db[(size_t) h] > thrOff : db[(size_t) h] > thrOn;
        vo[(size_t) h] = on ? 1 : 0;
    }
    // fill short gaps inside words (< 120 ms), then drop blips shorter than 120 ms
    auto runs = [&] (uint8_t value, int maxLen, uint8_t replaceWith, bool interiorOnly)
    {
        int h = 0;
        while (h < nh)
        {
            if (vo[(size_t) h] != value) { ++h; continue; }
            int e = h;
            while (e < nh && vo[(size_t) e] == value) ++e;
            const bool interior = h > 0 && e < nh;
            if (e - h < maxLen && (! interiorOnly || interior))
                std::fill (vo.begin() + h, vo.begin() + e, replaceWith);
            h = e;
        }
    };
    runs (0, 12, 1, true);
    runs (1, 12, 0, false);

    t.voiced = std::move (vo);
    t.silentAhead.assign ((size_t) nh, 0);
    int run = 0;
    for (int h = nh - 1; h >= 0; --h)
    {
        run = t.voiced[(size_t) h] ? 0 : juce::jmin (65535, run + 1);
        t.silentAhead[(size_t) h] = (uint16_t) run;
    }
    for (int h = 0; h < nh;)
    {
        if (! t.voiced[(size_t) h]) { ++h; continue; }
        int e = h;
        while (e < nh && t.voiced[(size_t) e]) ++e;
        t.phrases.push_back ({ h, e });
        t.voicedHops += e - h;
        h = e;
    }
}

// Pairwise constant-power panning over the 5 main speakers of a 5.1 layout (ITU angles).
// az in degrees, 0 = front centre, positive = right. Gains written as L R C Ls Rs.
static void panSurround (float az, float* g) noexcept
{
    static constexpr float ang[6] = { -110.0f, -30.0f, 0.0f, 30.0f, 110.0f, 250.0f };
    static constexpr int   spk[6] = { 3, 0, 2, 1, 4, 3 };
    while (az < -110.0f) az += 360.0f;
    while (az >= 250.0f) az -= 360.0f;
    for (int k = 0; k < 5; ++k)
    {
        if (az <= ang[k + 1])
        {
            const float f = (az - ang[k]) / (ang[k + 1] - ang[k]);
            g[spk[k]]     += std::cos (f * juce::MathConstants<float>::halfPi);
            g[spk[k + 1]] += std::sin (f * juce::MathConstants<float>::halfPi);
            return;
        }
    }
}

static inline float readTake (const float* d, int len, double pos) noexcept
{
    const int i0 = (int) pos;
    if (i0 < 1 || i0 >= len - 2) return 0.0f;
    const float fr = (float) (pos - (double) i0);
    const float xm1 = d[i0 - 1], x0 = d[i0], x1 = d[i0 + 1], x2 = d[i0 + 2];
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * fr + c2) * fr + c1) * fr + x0;
}

static inline float readInterp (const std::vector<float>& ring, int mask, float pos) noexcept
{
    // pos: absolute (possibly fractional) read position, already wrapped positive
    const int   i0 = (int) pos;
    const float fr = pos - (float) i0;
    const float xm1 = ring[(size_t) ((i0 - 1) & mask)];
    const float x0  = ring[(size_t) (i0 & mask)];
    const float x1  = ring[(size_t) ((i0 + 1) & mask)];
    const float x2  = ring[(size_t) ((i0 + 2) & mask)];
    // 4-point Hermite
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * fr + c2) * fr + c1) * fr + x0;
}

void CrowdEngine::process (const juce::AudioBuffer<float>& in, int numInputChannels,
                           juce::AudioBuffer<float>& out, int numSamples, const Params& p)
{
    if (p.seed != currentSeed)
        reroll (p.seed);

    const int fixedTakes = juce::jlimit (0, kMaxTakes, p.takes);
    const int nIn   = juce::jmin (numInputChannels, kMaxTakes);

    // ---- 1. Write inputs into ring buffers, measure levels
    for (int ch = 0; ch < kMaxTakes; ++ch)
    {
        auto& ring = rings[(size_t) ch];
        float peak = 0.0f;
        if (ch < nIn)
        {
            const float* src = in.getReadPointer (ch);
            for (int n = 0; n < numSamples; ++n)
            {
                ring[(size_t) ((writePos + n) & ringMask)] = src[n];
                peak = juce::jmax (peak, std::abs (src[n]));
            }
        }
        else
        {
            for (int n = 0; n < numSamples; ++n)
                ring[(size_t) ((writePos + n) & ringMask)] = 0.0f;
        }
        // auto take detection: a channel counts as a take once it carries signal, held for 30 s
        if (peak > 0.003f)
            slotHold[(size_t) ch] = (int) (sr * 30.0);
        else
            slotHold[(size_t) ch] = juce::jmax (0, slotHold[(size_t) ch] - numSamples);
        slotOn[(size_t) ch].store (slotHold[(size_t) ch] > 0);

        const float old = inLevels[(size_t) ch].load();
        inLevels[(size_t) ch].store (juce::jmax (peak, old * 0.85f));
    }

    // ---- 2. Clear outputs (input and output buffers may alias; inputs are already in the rings)
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        out.clear (ch, 0, numSamples);

    const bool surround = p.format == OutFormat::surround51;
    const bool mono     = p.format == OutFormat::mono;

    // Which input channels feed voices
    std::array<int, kMaxTakes> slotList {};
    int nSlots = 0;
    for (int ch = 0; ch < kMaxTakes; ++ch)
        if (fixedTakes > 0 ? ch < fixedTakes : slotHold[(size_t) ch] > 0)
            slotList[(size_t) nSlots++] = ch;
    if (nSlots == 0)
        slotList[(size_t) nSlots++] = 0;

    // Files mode: voices play the loaded take files instead of the track inputs
    const auto lib = std::atomic_load (&library);
    const bool files = lib != nullptr && ! lib->takes.empty();
    std::array<int, 256> cand {};
    int nCand = 0;
    if (files)
        for (size_t i = 0; i < lib->takes.size() && nCand < (int) cand.size(); ++i)
            if (p.group <= 0 || (i < lib->groupOf.size() && lib->groupOf[i] == p.group))
                cand[(size_t) nCand++] = (int) i;
    bool filesOn = nCand > 0;
    if (files && p.hasTransport)
    {
        filesOn = filesOn && p.playing;
        if (p.playing && (! wasPlaying || std::abs (p.timeInSamples - expectedTime) > (juce::int64) numSamples * 4))
            resyncFiles (p.seed, p.timeInSamples / juce::jmax ((juce::int64) 1, (juce::int64) (sr * 0.25)));
        wasPlaying = p.playing;
        expectedTime = p.timeInSamples + numSamples;
    }
    const float segFade = 1.0f / (float) (0.04 * sr);
    const int   maxOvertime = (int) (1.5 * sr);

    sendBuf.clear();
    busBuf.clear();
    float* sFL = sendBuf.getWritePointer (0);
    float* sFR = sendBuf.getWritePointer (1);
    float* sRL = sendBuf.getWritePointer (2);
    float* sRR = sendBuf.getWritePointer (3);
    float* bL  = busBuf.getWritePointer (0);
    float* bR  = busBuf.getWritePointer (1);
    float* bC  = busBuf.getWritePointer (2);
    float* bLs = busBuf.getWritePointer (3);
    float* bRs = busBuf.getWritePointer (4);

    const int   nVoices = juce::jlimit (1, kMaxVoices, p.voices);
    const float maleGain   = juce::jmin (1.0f, 1.0f - p.balance);
    const float femaleGain = juce::jmin (1.0f, 1.0f + p.balance);
    const float W          = (float) (0.050 * sr);                 // pitch-shift window (samples)
    const float delaySmooth = 1.0f - std::exp (-1.0f / (float) (0.25 * sr));
    const float fadeStep    = 1.0f / (float) (0.05 * sr);
    const float norm        = 2.0f / std::sqrt ((float) nVoices);
    const float ringF       = (float) ringSize;

    // ---- 3. Voices
    for (int vi = 0; vi < kMaxVoices; ++vi)
    {
        auto& v = voices[(size_t) vi];
        if (files)
        {
            // a voice whose take is gone or outside the selected group moves to another take
            bool known = false;
            if (v.take != nullptr)
                for (int k = 0; k < nCand && ! known; ++k)
                    known = lib->takes[(size_t) cand[(size_t) k]] == v.take;
            if (! known)
            {
                if (v.take == nullptr || v.active <= 0.0f)
                {
                    if (nCand > 0) newSegment (v, *lib, cand.data(), nCand, p.mix, true);
                    else           v.take.reset();
                }
                else
                    v.forceSwitch = true;
            }
            if (v.take == nullptr)
            {
                v.active = 0.0f;
                v.vizOn.store (false);
                continue;
            }
        }
        else
        {
            // live mode: a voice whose input went away jumps to another take
            bool known = false;
            for (int k = 0; k < nSlots; ++k)
                known |= slotList[(size_t) k] == v.slot;
            if (! known)
                v.segRemaining = 0;
        }
        if (! files && p.mix > 0.001f && v.segRemaining == std::numeric_limits<int>::max())
            v.segRemaining = (int) (v.rng.nextFloat() * sr * 3.0);
        const bool wantOn = vi < nVoices && (! files || filesOn);
        if (! wantOn && v.active <= 0.0f)
        {
            v.vizOn.store (false);
            continue;
        }
        v.female = files ? v.take->female.load() : v.slot >= p.maleTakes;
        const float genderGain = v.female ? femaleGain : maleGain;

        float targetDelay = (float) ((p.delayMinMs + v.uDelay * p.delaySpreadMs + v.uScatter * p.scatterMs) * 0.001 * sr);
        const float looseAmp    = (float) (p.loose * 0.2 * sr);   // up to 200 ms of tempo wander
        if (v.baseDelay < 0.0f)
            v.baseDelay = targetDelay;

        const float dist   = juce::jlimit (0.0f, 1.0f, p.distance + (v.uDist * 2.0f - 1.0f) * p.depthSpread * 0.5f);
        const float distGain = juce::Decibels::decibelsToGain (-18.0f * dist);
        const float cutoff = 18000.0f * std::pow (2200.0f / 18000.0f, dist);
        const float lpA    = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (cutoff, (float) sr * 0.45f) / (float) sr);
        const float send   = p.reverb * (0.25f + 0.9f * dist);

        // ---- placement. Stereo: constant-power pan. 5.1: all around the listener (WIDTH 100% = full circle).
        const float pan = p.width * (v.uPan * 2.0f - 1.0f);
        float g[5] = { 0, 0, 0, 0, 0 };
        float az = pan * 90.0f;
        if (surround)
        {
            az = pan * 180.0f;
            panSurround (az, g);
        }
        else if (mono)
        {
            g[0] = g[1] = 0.7071f;
        }
        else
        {
            const float ang = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            g[0] = std::cos (ang);
            g[1] = std::sin (ang);
        }
        const float fL = (g[0] + 0.7071f * g[2]) * send, fR = (g[1] + 0.7071f * g[2]) * send;
        const float rL = g[3] * send, rR = g[4] * send;
        auto emit = [&] (int n, float x) noexcept
        {
            bL[n] += x * g[0];
            bR[n] += x * g[1];
            sFL[n] += x * fL;
            sFR[n] += x * fR;
            if (surround)
            {
                bC[n]  += x * g[2];
                bLs[n] += x * g[3];
                bRs[n] += x * g[4];
                sRL[n] += x * rL;
                sRR[n] += x * rR;
            }
        };

        // Timbre: two random peaking filters in formant regions (female voices skew higher)
        const float fShift = v.female ? 1.18f : 1.0f;
        v.f1.setPeak (sr, fShift * (300.0 * std::pow (1000.0 / 300.0, (double) v.uF1)), 1.4, p.timbre * 9.0 * (v.uG1 * 2.0 - 1.0));
        v.f2.setPeak (sr, fShift * (1100.0 * std::pow (3600.0 / 1100.0, (double) v.uF2)), 1.6, p.timbre * 9.0 * (v.uG2 * 2.0 - 1.0));

        const float baseCents = p.pitchSpread * (v.uPitch * 2.0f - 1.0f);
        const std::vector<float>* ring = &rings[(size_t) v.slot];
        const float gainBase = distGain * genderGain * norm;

        float energy = 0.0f, phaseInc = 0.0f, amp = 1.0f;
        double fileRate = 1.0;

        for (int n = 0; n < numSamples; ++n)
        {
            v.active = juce::jlimit (0.0f, 1.0f, v.active + (wantOn ? fadeStep : -fadeStep));

            if (! files)
            {
                // ---- live mode take mixing: every few seconds fade out, jump to another take
                // and another moment in its recent past, fade back in
                if (v.segRemaining <= 0)
                {
                    v.segEnv -= segFade;
                    if (v.segEnv <= 0.0f)
                    {
                        const bool firstOne = v.active <= fadeStep * 2.0f;
                        v.segEnv = 0.0f;
                        v.slot = slotList[(size_t) v.rng.nextInt (nSlots)];
                        ring = &rings[(size_t) v.slot];
                        if (p.mix > 0.001f)
                        {
                            v.uScatter = v.rng.nextFloat();
                            v.uDelay   = v.rng.nextFloat();
                            const float segSec = (12.0f - 10.5f * p.mix) * (0.6f + 0.8f * v.rng.nextFloat());
                            v.segRemaining = juce::jmax (1, (int) (segSec * sr * (firstOne ? v.rng.nextFloat() : 1.0f)));
                        }
                        else
                        {
                            v.segRemaining = std::numeric_limits<int>::max();
                        }
                        targetDelay = (float) ((p.delayMinMs + v.uDelay * p.delaySpreadMs + v.uScatter * p.scatterMs) * 0.001 * sr);
                        v.baseDelay = targetDelay;
                        v.prevDelay = -1.0f;
                        v.f1.reset(); v.f2.reset(); v.lp = 0.0f;
                    }
                }
                else
                {
                    v.segEnv = juce::jmin (1.0f, v.segEnv + segFade);
                    if (v.segRemaining != std::numeric_limits<int>::max())
                        --v.segRemaining;
                }
            }

            v.baseDelay += (targetDelay - v.baseDelay) * delaySmooth;
            const float tw = v.timeWander.next (v.rng, sr, 2.0f, 6.0f);
            const float totalDelay = v.baseDelay + looseAmp * 0.5f * (tw + 1.0f);
            if (v.prevDelay < 0.0f) v.prevDelay = totalDelay;
            const float slope = totalDelay - v.prevDelay;   // >0: falling behind (slower), <0: catching up
            v.prevDelay = totalDelay;

            // modulation is slow: update the expensive parts every 8 samples
            const float pw = v.pitchWander.next (v.rng, sr, 0.4f, 1.8f);
            const float aw = v.ampWander.next (v.rng, sr, 0.15f, 0.9f);
            if (files)
            {
                // ---- files mode: read the take directly, jump to another take/phrase now and then
                const auto& t = *v.take;
                const int len = t.audio.getNumSamples();
                const int h = (int) (v.libPos / (double) t.hop);
                const bool haveMap = ! t.voiced.empty();
                const bool voicedHere = haveMap && h >= 0 && h < (int) t.voiced.size() && t.voiced[(size_t) h] != 0;
                if ((n & 7) == 0 && haveMap && ! voicedHere && h >= 0 && h < (int) t.silentAhead.size()
                    && t.silentAhead[(size_t) h] >= 20)
                    v.forceSwitch = true;                      // 200 ms+ of nothing ahead: go find voice elsewhere

                bool endNow = v.forceSwitch || v.libPos >= (double) (len - 4);
                if (! endNow && v.segRemaining <= 0)
                {
                    // segment time is up: finish the current phrase first (at most 1.5 s more)
                    if (! voicedHere || v.overtime > maxOvertime) endNow = true;
                    else ++v.overtime;
                }
                if (endNow)
                {
                    v.segEnv -= segFade;
                    if (v.segEnv <= 0.0f)
                    {
                        if (nCand > 0)
                            newSegment (v, *lib, cand.data(), nCand, p.mix, false);
                        else
                            v.segEnv = 0.0f;
                    }
                }
                else
                {
                    v.segEnv = juce::jmin (1.0f, v.segEnv + segFade);
                    if (v.segRemaining > 0) --v.segRemaining;
                }
                const auto& tk = *v.take;   // may have changed in newSegment
                if ((n & 7) == 0)
                {
                    fileRate = std::exp2 ((baseCents + p.drift * pw) * (1.0f / 1200.0f)) * tk.sampleRate / sr;
                    amp = std::exp (p.humanize * 0.7f * aw);
                }
                float x = readTake (tk.audio.getReadPointer (0), tk.audio.getNumSamples(), v.libPos) * v.segEnv;
                v.libPos += fileRate;

                x = v.f1.process (x);
                x = v.f2.process (x);
                v.lp += lpA * (x - v.lp);
                x = v.lp * gainBase * amp * v.active;
                energy += x * x;
                emit (n, x);
                continue;
            }

            if ((n & 7) == 0)
            {
                const float cents = baseCents + p.drift * pw;
                // read speed = shifterRatio - slope; choose shifterRatio so pitch stays at target
                phaseInc = (1.0f - (std::exp2 (cents * (1.0f / 1200.0f)) + slope)) / W;
                amp = std::exp (p.humanize * 0.7f * aw);
            }
            v.phase += phaseInc;
            v.phase -= std::floor (v.phase);

            float phB = v.phase + 0.5f;
            if (phB >= 1.0f) phB -= 1.0f;

            const float wA = std::sin (juce::MathConstants<float>::pi * v.phase);
            const float wB = std::sin (juce::MathConstants<float>::pi * phB);

            const float now = (float) (writePos + n);
            float posA = now - (totalDelay + v.phase * W);
            float posB = now - (totalDelay + phB * W);
            if (posA < 0.0f) posA += ringF;
            if (posB < 0.0f) posB += ringF;

            float x = (wA * wA * readInterp (*ring, ringMask, posA)
                     + wB * wB * readInterp (*ring, ringMask, posB)) * v.segEnv;

            x = v.f1.process (x);
            x = v.f2.process (x);
            v.lp += lpA * (x - v.lp);
            x = v.lp;

            x *= gainBase * amp * v.active;

            energy += x * x;
            emit (n, x);
        }

        v.vizPan.store (pan);
        v.vizAz.store (az);
        v.vizDist.store (dist);
        v.vizLevel.store (std::sqrt (energy / (float) juce::jmax (1, numSamples)) / juce::jmax (1.0e-4f, norm));
        v.vizFemale.store (v.female);
        v.vizOn.store (wantOn);
    }

    // ---- 4. Shared reverb (front pair; in 5.1 also a rear pair, cross-fed so the room surrounds you)
    juce::Reverb::Parameters rp;
    // Room types: {min size, max size, damping, width}
    static constexpr float rooms[4][4] = { { 0.20f, 0.50f, 0.75f, 1.0f },    // Field (open air)
                                           { 0.45f, 0.75f, 0.55f, 0.9f },    // Courtyard
                                           { 0.60f, 0.90f, 0.40f, 1.0f },    // Hall
                                           { 0.80f, 0.99f, 0.20f, 1.0f } };  // Cathedral
    const auto& rm = rooms[juce::jlimit (0, 3, p.roomType)];
    rp.roomSize   = rm[0] + (rm[1] - rm[0]) * p.room;
    rp.damping    = rm[2];
    rp.wetLevel   = 1.0f;
    rp.dryLevel   = 0.0f;
    rp.width      = rm[3];
    rp.freezeMode = 0.0f;
    reverb.setParameters (rp);
    if (surround)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float a = sFL[n], b = sFR[n], c = sRL[n], d = sRR[n];
            sFL[n] = a + 0.5f * c; sFR[n] = b + 0.5f * d;
            sRL[n] = c + 0.5f * a; sRR[n] = d + 0.5f * b;
        }
        rp.roomSize *= 0.94f;   // slightly different tank: decorrelated from the front
        reverbRear.setParameters (rp);
        reverbRear.processStereo (sRL, sRR, numSamples);
    }
    reverb.processStereo (sFL, sFR, numSamples);

    // ---- 5. Output per speaker = level * (dry part + crowd part), MIX per speaker balances the two
    const float wetTarget = juce::Decibels::decibelsToGain (p.wetDb);
    const float dryG = p.dry / std::sqrt ((float) nSlots);
    std::array<float*, kNumSpeakers> dst {};
    std::array<float, kNumSpeakers> dryW {}, wetW {}, gainT {};
    for (int s = 0; s < kNumSpeakers; ++s)
    {
        const int idx = p.chanIndex[(size_t) s];
        dst[(size_t) s] = (idx >= 0 && idx < out.getNumChannels()) ? out.getWritePointer (idx) : nullptr;
        const float m = juce::jlimit (0.0f, 1.0f, p.chanMix[(size_t) s]);
        dryW[(size_t) s] = juce::jmin (1.0f, 2.0f * (1.0f - m));
        wetW[(size_t) s] = juce::jmin (1.0f, 2.0f * m);
        gainT[(size_t) s] = p.chanGainDb[(size_t) s] <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (p.chanGainDb[(size_t) s]);
    }
    for (int n = 0; n < numSamples; ++n)
    {
        wetGainSmoothed += (wetTarget - wetGainSmoothed) * 0.002f;
        for (int s = 0; s < kNumSpeakers; ++s)
            chanGainSmoothed[(size_t) s] += (gainT[(size_t) s] - chanGainSmoothed[(size_t) s]) * 0.002f;

        float in = 0.0f;
        for (int k = 0; k < nSlots; ++k)
            in += rings[(size_t) slotList[(size_t) k]][(size_t) ((writePos + n) & ringMask)];
        const float d = in * dryG;

        const float wg = wetGainSmoothed;
        float wet[kNumSpeakers], dry[kNumSpeakers] = { 0, 0, 0, 0, 0, 0 };
        if (mono)
        {
            wet[spL] = (bL[n] + bR[n] + (sFL[n] + sFR[n]) * 0.7071f) * wg;
            dry[spL] = d;
        }
        else
        {
            wet[spL] = (bL[n] + sFL[n]) * wg;
            wet[spR] = (bR[n] + sFR[n]) * wg;
            if (surround)
            {
                wet[spC]  = bC[n] * wg;
                wet[spLs] = (bLs[n] + sRL[n]) * wg;
                wet[spRs] = (bRs[n] + sRR[n]) * wg;
                const float sum = bL[n] + bR[n] + bC[n] + bLs[n] + bRs[n];
                wet[spLFE] = lfeLp2.process (lfeLp1.process (sum)) * 0.3f * wg;
                dry[spC] = d;
            }
            else
            {
                dry[spL] = dry[spR] = d * 0.7071f;
            }
        }
        for (int s = 0; s < kNumSpeakers; ++s)
            if (float* o = dst[(size_t) s])
                o[n] = chanGainSmoothed[(size_t) s] * (dryW[(size_t) s] * dry[s] + wetW[(size_t) s] * wet[s]);
    }
    for (int s = 0; s < kNumSpeakers; ++s)
    {
        const float pk = dst[(size_t) s] != nullptr ? juce::FloatVectorOperations::findMaximum (dst[(size_t) s], numSamples) : 0.0f;
        const float mn = dst[(size_t) s] != nullptr ? -juce::FloatVectorOperations::findMinimum (dst[(size_t) s], numSamples) : 0.0f;
        outLevels[(size_t) s].store (juce::jmax (juce::jmax (pk, mn), outLevels[(size_t) s].load() * 0.85f));
    }

    writePos = (writePos + numSamples) & ringMask;
}
} // namespace horde
