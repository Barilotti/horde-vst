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

    sendBuf.setSize (2, maxBlock);
    reverb.setSampleRate (sr);
    currentSeed = -1;
    reset();
}

void CrowdEngine::reset()
{
    for (auto& r : rings)
        std::fill (r.begin(), r.end(), 0.0f);
    writePos = 0;
    reverb.reset();
    slotHold.fill (0);
    for (auto& s : slotOn) s.store (false);
    for (auto& v : voices)
    {
        v.f1.reset(); v.f2.reset();
        v.lp = 0; v.baseDelay = -1.0f; v.prevDelay = -1.0f; v.active = 0.0f;
        v.segEnv = 0.0f; v.segRemaining = 0;
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
    }
}

void CrowdEngine::newSegment (Voice& v, int, const Library& lib, float mix, bool firstOne)
{
    const int n = (int) lib.takes.size();
    v.take = lib.takes[(size_t) v.rng.nextInt (n)];
    const auto& t = *v.take;
    const int len = t.audio.getNumSamples();

    // pick a start point that actually has voice in it
    double pos = 0.0;
    for (int tries = 0; tries < 12; ++tries)
    {
        pos = (double) v.rng.nextFloat() * (double) juce::jmax (1, len - (int) (t.sampleRate * 0.5));
        const size_t e = (size_t) juce::jlimit (0, (int) t.env.size() - 1, (int) (pos / t.hop));
        if (t.env.empty() || t.env[e] > 0.12f)
            break;
    }
    v.libPos = pos;

    const float segSec = (12.0f - 10.5f * mix) * (0.6f + 0.8f * v.rng.nextFloat());
    v.segRemaining = (int) (segSec * sr * (firstOne ? v.rng.nextFloat() : 1.0f));
    v.segEnv = 0.0f;
    v.f1.reset(); v.f2.reset(); v.lp = 0.0f;
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
    float* outL = out.getWritePointer (0);
    float* outR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        out.clear (ch, 0, numSamples);

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
    bool filesOn = true;
    if (files && p.hasTransport)
    {
        filesOn = p.playing;
        if (p.playing && (! wasPlaying || std::abs (p.timeInSamples - expectedTime) > (juce::int64) numSamples * 4))
            resyncFiles (p.seed, p.timeInSamples / juce::jmax ((juce::int64) 1, (juce::int64) (sr * 0.25)));
        wasPlaying = p.playing;
        expectedTime = p.timeInSamples + numSamples;
    }
    const float segFade = 1.0f / (float) (0.04 * sr);

    sendBuf.clear();
    float* sL = sendBuf.getWritePointer (0);
    float* sR = sendBuf.getWritePointer (1);

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
        // Distribute voices across takes: interleave so every take gets used evenly.
        // If the take assignment changes, fade the voice out first, then move it.
        if (files)
        {
            bool known = false;
            if (v.take != nullptr)
                for (auto& t : lib->takes)
                    if (t == v.take) { known = true; break; }
            if (! known)
                newSegment (v, vi, *lib, p.mix, true);
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

        const float pan = p.width * (v.uPan * 2.0f - 1.0f);
        const float ang = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float gL  = std::cos (ang), gR = std::sin (ang);

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
                const int len = v.take->audio.getNumSamples();
                if (v.segRemaining <= 0 || v.libPos >= (double) (len - 4))
                {
                    v.segEnv -= segFade;
                    if (v.segEnv <= 0.0f)
                        newSegment (v, vi, *lib, p.mix, false);
                }
                else
                {
                    v.segEnv = juce::jmin (1.0f, v.segEnv + segFade);
                    --v.segRemaining;
                }
                if ((n & 7) == 0)
                {
                    fileRate = std::exp2 ((baseCents + p.drift * pw) * (1.0f / 1200.0f)) * v.take->sampleRate / sr;
                    amp = std::exp (p.humanize * 0.7f * aw);
                }
                float x = readTake (v.take->audio.getReadPointer (0), v.take->audio.getNumSamples(), v.libPos) * v.segEnv;
                v.libPos += fileRate;

                x = v.f1.process (x);
                x = v.f2.process (x);
                v.lp += lpA * (x - v.lp);
                x = v.lp * gainBase * amp * v.active;
                energy += x * x;
                outL[n] += x * gL;
                if (outR) outR[n] += x * gR;
                sL[n] += x * gL * send;
                sR[n] += x * gR * send;
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

            outL[n] += x * gL;
            if (outR) outR[n] += x * gR;
            sL[n] += x * gL * send;
            sR[n] += x * gR * send;
        }

        v.vizPan.store (pan);
        v.vizDist.store (dist);
        v.vizLevel.store (std::sqrt (energy / (float) juce::jmax (1, numSamples)) / juce::jmax (1.0e-4f, norm));
        v.vizFemale.store (v.female);
        v.vizOn.store (wantOn);
    }

    // ---- 4. Shared reverb
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
    reverb.processStereo (sL, sR, numSamples);

    // ---- 5. Output = dry (sum of used takes, centered) + (voices + reverb) * wet gain
    const float wetTarget = juce::Decibels::decibelsToGain (p.wetDb);
    const float dryG = p.dry / std::sqrt ((float) nSlots);
    for (int n = 0; n < numSamples; ++n)
    {
        wetGainSmoothed += (wetTarget - wetGainSmoothed) * 0.002f;
        float s = 0.0f;
        for (int k = 0; k < nSlots; ++k)
            s += rings[(size_t) slotList[(size_t) k]][(size_t) ((writePos + n) & ringMask)];
        const float d = s * dryG * 0.7071f;
        outL[n] = d + (outL[n] + sL[n]) * wetGainSmoothed;
        if (outR) outR[n] = d + (outR[n] + sR[n]) * wetGainSmoothed;
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        const float pk = (ch == 0 || outR == nullptr) ? out.getMagnitude (0, 0, numSamples)
                                                      : out.getMagnitude (1, 0, numSamples);
        outLevels[(size_t) ch].store (juce::jmax (pk, outLevels[(size_t) ch].load() * 0.85f));
    }
    for (int ch = 2; ch < out.getNumChannels(); ++ch)
        out.clear (ch, 0, numSamples);

    writePos = (writePos + numSamples) & ringMask;
}
} // namespace horde
