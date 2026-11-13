#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

static juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

const char* HordeProcessor::speakerId (int s)
{
    static const char* ids[] = { "L", "R", "C", "LFE", "Ls", "Rs" };
    return ids[juce::jlimit (0, horde::kNumSpeakers - 1, s)];
}

APVTS::ParameterLayout HordeProcessor::createLayout (HordeProcessor* owner)
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> ps;

    auto addInt = [&] (const char* id, const char* name, int lo, int hi, int def, const char* suffix = "")
    {
        ps.push_back (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def,
            AudioParameterIntAttributes().withLabel (suffix)));
    };
    auto addF = [&] (const char* id, const char* name, float lo, float hi, float def, float skewMid,
                     std::function<String (float, int)> toText)
    {
        NormalisableRange<float> r (lo, hi);
        if (skewMid > lo && skewMid < hi) r.setSkewForCentre (skewMid);
        ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
            AudioParameterFloatAttributes().withStringFromValueFunction (std::move (toText))));
    };

    addInt ("voices",    "Voices",     1, horde::kMaxVoices, 40);
    ps.push_back (std::make_unique<AudioParameterInt> (ParameterID { "takes", 1 }, "Takes", 0, horde::kMaxTakes, 0,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return v == 0 ? String ("Auto") : String (v); })));
    addInt ("maleTakes", "Male takes", 0, horde::kMaxTakes, 3);
    addF ("balance", "M/F balance", -1.0f, 1.0f, 0.0f, 0.0f, [] (float v, int)
    {
        if (std::abs (v) < 0.005f) return String ("even");
        return (v < 0 ? String ("M +") : String ("F +")) + String (juce::roundToInt (std::abs (v) * 100.0f));
    });
    addF ("delayMin",    "Delay min",    5.0f, 100.0f, 12.0f, 25.0f, [] (float v, int) { return String (v, 1) + " ms"; });
    addF ("delaySpread", "Delay spread", 0.0f, 500.0f, 90.0f, 100.0f, [] (float v, int) { return String (juce::roundToInt (v)) + " ms"; });
    addF ("scatter",     "Scatter",      0.0f, 3000.0f, 700.0f, 600.0f, [] (float v, int) { return String (juce::roundToInt (v)) + " ms"; });
    addF ("mix",         "Mix",          0.0f, 1.0f, 0.5f, 0.0f, pct);
    addF ("loose",       "Loose",        0.0f, 1.0f, 0.5f, 0.0f, pct);
    addF ("pitchSpread", "Pitch spread", 0.0f, 50.0f, 18.0f, 0.0f, [] (float v, int) { return String (v, 1) + " ct"; });
    addF ("drift",       "Drift",        0.0f, 30.0f, 6.0f, 0.0f, [] (float v, int) { return String (v, 1) + " ct"; });
    addF ("timbre",      "Timbre",       0.0f, 1.0f, 0.5f, 0.0f, pct);
    addF ("humanize",    "Humanize",     0.0f, 1.0f, 0.5f, 0.0f, pct);
    addF ("distance",    "Distance",     0.0f, 1.0f, 0.35f, 0.0f, pct);
    addF ("depthSpread", "Depth spread", 0.0f, 1.0f, 0.7f, 0.0f, pct);
    addF ("width",       "Width",        0.0f, 1.0f, 1.0f, 0.0f, pct);
    addF ("reverb",      "Reverb",       0.0f, 1.0f, 0.35f, 0.0f, pct);
    addF ("room",        "Room",         0.0f, 1.0f, 0.7f, 0.0f, pct);
    addF ("dry",         "Dry",          0.0f, 1.0f, 0.0f, 0.0f, pct);
    addF ("wet",         "Wet",          -24.0f, 12.0f, 0.0f, 0.0f, [] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; });
    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "roomType", 1 }, "Room type",
                                                          StringArray { "Field", "Courtyard", "Hall", "Cathedral" }, 2));
    addInt ("seed",      "Seed",         1, 9999, 1);

    ps.push_back (std::make_unique<AudioParameterInt> (ParameterID { "group", 1 }, "Crowd group", 0, horde::kMaxGroups, 0,
        AudioParameterIntAttributes().withStringFromValueFunction ([owner] (int v, int) { return owner->groupNameForParam (v); })));

    // per output channel: level and dry/crowd mix (mono uses L, stereo L/R, 5.1 all six)
    auto dbText = [] (float v, int) { return v <= -59.9f ? String ("-inf") : (v > 0 ? "+" : "") + String (v, 1) + " dB"; };
    for (int s = 0; s < horde::kNumSpeakers; ++s)
    {
        const String sp (speakerId (s));
        addF (("lvl" + sp).toRawUTF8(), ("Level " + sp).toRawUTF8(), -60.0f, 12.0f, 0.0f, -12.0f, dbText);
        addF (("mix" + sp).toRawUTF8(), ("Mix " + sp).toRawUTF8(), 0.0f, 1.0f, 0.5f, 0.0f, [] (float v, int)
        {
            if (std::abs (v - 0.5f) < 0.005f) return String ("both");
            return v < 0.5f ? "dry " + String (juce::roundToInt ((0.5f - v) * 200.0f)) + "%"
                            : "crowd " + String (juce::roundToInt ((v - 0.5f) * 200.0f)) + "%";
        });
    }

    return { ps.begin(), ps.end() };
}

HordeProcessor::HordeProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Takes", juce::AudioChannelSet::discreteChannels (horde::kMaxTakes), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "HORDE", createLayout (this))
{
    auto g = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    formats.registerBasicFormats();
    startTimerHz (10);
    raw = { g ("voices"), g ("takes"), g ("maleTakes"), g ("balance"), g ("delayMin"), g ("delaySpread"),
            g ("pitchSpread"), g ("drift"), g ("timbre"), g ("humanize"), g ("distance"), g ("depthSpread"),
            g ("width"), g ("reverb"), g ("room"), g ("dry"), g ("wet"), g ("seed"), g ("roomType"), g ("scatter"), g ("loose"), g ("mix"), g ("group"), {}, {} };
    for (int s = 0; s < horde::kNumSpeakers; ++s)
    {
        raw.lvl[(size_t) s]   = g (("lvl" + juce::String (speakerId (s))).toRawUTF8());
        raw.chMix[(size_t) s] = g (("mix" + juce::String (speakerId (s))).toRawUTF8());
    }
    updateOutputMap();
}

bool HordeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()
        && out != juce::AudioChannelSet::create5point1() && out != juce::AudioChannelSet::discreteChannels (6))
        return false;
    const int nIn = layouts.getMainInputChannels();
    return nIn >= 1 && nIn <= 16;
}

void HordeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    updateOutputMap();
    engine.prepare (sampleRate, 512);
}

void HordeProcessor::updateOutputMap()
{
    const auto set = getChannelLayoutOfBus (false, 0);
    const int n = set.size();
    horde::OutFormat f = n >= 6 ? horde::OutFormat::surround51 : n == 1 ? horde::OutFormat::mono : horde::OutFormat::stereo;
    std::array<int, horde::kNumSpeakers> idx { 0, 1, 2, 3, 4, 5 };   // discrete 6 = L R C LFE Ls Rs
    if (f == horde::OutFormat::mono)        idx = { 0, -1, -1, -1, -1, -1 };
    else if (f == horde::OutFormat::stereo) idx = { 0, 1, -1, -1, -1, -1 };
    else if (! set.isDiscreteLayout())
    {
        using CS = juce::AudioChannelSet;
        const CS::ChannelType types[] = { CS::left, CS::right, CS::centre, CS::LFE, CS::leftSurround, CS::rightSurround };
        for (int s = 0; s < horde::kNumSpeakers; ++s)
            idx[(size_t) s] = set.getChannelIndexForType (types[s]);
    }
    for (int s = 0; s < horde::kNumSpeakers; ++s)
        outIndex[(size_t) s].store (idx[(size_t) s]);
    outFormat.store ((int) f);
}

void HordeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    horde::Params p;
    p.voices        = (int) raw.voices->load();
    p.takes         = (int) raw.takes->load();
    p.maleTakes     = (int) raw.maleTakes->load();
    p.balance       = raw.balance->load();
    p.delayMinMs    = raw.delayMin->load();
    p.delaySpreadMs = raw.delaySpread->load();
    p.pitchSpread   = raw.pitchSpread->load();
    p.drift         = raw.drift->load();
    p.timbre        = raw.timbre->load();
    p.humanize      = raw.humanize->load();
    p.distance      = raw.distance->load();
    p.depthSpread   = raw.depthSpread->load();
    p.width         = raw.width->load();
    p.reverb        = raw.reverb->load();
    p.room          = raw.room->load();
    p.dry           = raw.dry->load();
    p.wetDb         = raw.wet->load();
    p.seed          = (int) raw.seed->load();
    p.roomType      = (int) raw.roomType->load();
    p.scatterMs     = raw.scatter->load();
    p.loose         = raw.loose->load();
    p.mix           = raw.mix->load();
    p.group         = (int) raw.group->load();
    p.format        = getOutFormat();
    for (int s = 0; s < horde::kNumSpeakers; ++s)
    {
        p.chanIndex[(size_t) s]  = outIndex[(size_t) s].load();
        p.chanGainDb[(size_t) s] = raw.lvl[(size_t) s]->load();
        p.chanMix[(size_t) s]    = raw.chMix[(size_t) s]->load();
    }

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            p.hasTransport = true;
            p.playing = pos->getIsPlaying();
            p.timeInSamples = pos->getTimeInSamples().orFallback (0);
        }

    const int nIn = getTotalNumInputChannels();
    numInputsSeen.store (nIn);

    // ---- CAPTURE: while armed, record the input and pass it through untouched
    if (capArmed.load())
    {
        const bool rolling = ! p.hasTransport || p.playing;
        const int ns = buffer.getNumSamples();
        if (rolling && ! capStopRequest.load())
        {
            const int chans = juce::jmin (nIn, horde::kMaxTakes);
            capChannels.store (juce::jmax (capChannels.load(), chans));
            const int start = capLen.load();
            for (int c = 0; c < chans; ++c)
            {
                const float* src = buffer.getReadPointer (c);
                for (int i = 0; i < ns; ++i)
                {
                    const int idx = start + i;
                    const int ch = idx / kCapChunk;
                    if (ch >= kCapMaxChunks) break;
                    if (float* dst = cap[(size_t) c].chunks[(size_t) ch].load())
                        dst[idx % kCapChunk] = src[i];
                }
            }
            capLen.store (juce::jmin (start + ns, kCapChunk * kCapMaxChunks));
            capRecording.store (true);
        }
        else if (capRecording.load() || capStopRequest.load())
        {
            // playback stopped (or user pressed the button again): done
            capArmed.store (false);
            capStopRequest.store (false);
            capDone.store (capRecording.load());
            capRecording.store (false);
        }
        for (int c = 2; c < buffer.getNumChannels(); ++c)
            buffer.clear (c, 0, ns);
        return;
    }

    const int n = buffer.getNumSamples();
    int done = 0;
    // Engine's internal scratch is sized for the prepared block size; chunk if host sends more.
    while (done < n)
    {
        const int chunk = juce::jmin (n - done, 512);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), done, chunk);
        engine.process (view, nIn, view, chunk, p);
        p.timeInSamples += chunk;
        done += chunk;
    }
}

juce::AudioProcessorEditor* HordeProcessor::createEditor() { return new HordeEditor (*this); }

//==============================================================================
std::shared_ptr<horde::Take> HordeProcessor::makeTake (const juce::AudioBuffer<float>& src, double sampleRate,
                                                       const juce::String& name, const juce::String& path, bool female)
{
    const int len = src.getNumSamples();
    juce::AudioBuffer<float> mono (1, len);
    mono.clear();
    for (int c = 0; c < src.getNumChannels(); ++c)
        mono.addFrom (0, 0, src, c, 0, len, 1.0f / (float) src.getNumChannels());

    // Squeeze out long silences, noise and breaths between phrases, so voices land on actual shouting.
    // The decision comes from the speech analysis (adaptive to each recording's noise floor).
    const int hop = juce::jmax (1, (int) (sampleRate * 0.01));
    const int nh = len / hop;
    horde::Take probe;
    probe.audio.setDataToReferTo (mono.getArrayOfWritePointers(), 1, len);   // no copy
    probe.sampleRate = sampleRate;
    probe.hop = hop;
    horde::analyseTake (probe);
    std::vector<bool> keep ((size_t) nh, false);
    for (int h = 0; h < nh; ++h)
    {
        const bool voiced = probe.voiced.empty() ? mono.getRMSLevel (0, h * hop, hop) > 0.0018f
                                                 : probe.voiced[(size_t) h] != 0;
        if (voiced)
            for (int k = juce::jmax (0, h - 15); k <= juce::jmin (nh - 1, h + 15); ++k)
                keep[(size_t) k] = true;
    }
    int outLen = 0;
    for (int h = 0; h < nh; ++h) if (keep[(size_t) h]) outLen += hop;
    if (outLen < (int) (sampleRate * 0.3))
        return nullptr;

    auto t = std::make_shared<horde::Take>();
    t->sampleRate = sampleRate;
    t->audio.setSize (1, outLen);
    int w = 0;
    for (int h = 0; h < nh; ++h)
        if (keep[(size_t) h]) { t->audio.copyFrom (0, w, mono, 0, h * hop, hop); w += hop; }

    // normalise peak to -3 dB so quiet and loud takes sit together
    const float pk = t->audio.getMagnitude (0, 0, outLen);
    if (pk > 1.0e-4f) t->audio.applyGain (0.708f / pk);

    t->hop = hop;
    float mx = 0.0f;
    for (int s = 0; s + hop <= outLen; s += hop)
    {
        const float r = t->audio.getRMSLevel (0, s, hop);
        t->env.push_back (r);
        mx = juce::jmax (mx, r);
    }
    if (mx > 0.0f) for (auto& e : t->env) e /= mx;
    horde::analyseTake (*t);

    t->name = name;
    t->path = path;
    t->female = female;
    return t;
}

void HordeProcessor::addFiles (const juce::StringArray& paths)
{
    for (const auto& path : paths)
    {
        if (takes.size() >= 64) break;
        juce::File f (path);
        bool dup = false;
        for (auto& t : takes) dup |= (t->path == f.getFullPathName());
        if (dup) continue;

        std::unique_ptr<juce::AudioFormatReader> rd (formats.createReaderFor (f));
        if (rd == nullptr || rd->lengthInSamples < 1000) continue;
        const int len = (int) juce::jmin (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 600.0));
        juce::AudioBuffer<float> tmp ((int) rd->numChannels, len);
        rd->read (&tmp, 0, len, 0, true, true);

        const auto name = f.getFileNameWithoutExtension();
        const auto lower = name.toLowerCase();
        const bool female = lower.contains ("fem") || lower.contains ("woman") || lower.contains (juce::String::fromUTF8 ("жен"))
                            || lower.contains ("girl");
        if (auto t = makeTake (tmp, rd->sampleRate, name, f.getFullPathName(), female))
            takes.push_back (t);
    }
    publishTakes();
}

ReaperBridge::Result HordeProcessor::linkChildTracks()
{
    auto r = reaperBridge.linkChildTracks();
    if (r.ok)
    {
        // replace previously loaded track takes (keeping their groups), keep dropped files
        for (auto& t : takes)
            if (t->path.startsWith ("track:"))
                pendingTrackGroups.set (t->path, t->group);
        takes.erase (std::remove_if (takes.begin(), takes.end(),
                                     [] (auto& t) { return t->path.startsWith ("track:"); }), takes.end());
        for (auto& ta : r.audio)
            if (auto t = makeTake (ta.mono, ta.sampleRate, ta.name, "track:" + ta.name, ta.female))
            {
                t->group = pendingTrackGroups.getValue (t->path, {});
                takes.push_back (t);
            }
        linkedToTracks = true;
        publishTakes();
    }
    return r;
}

void HordeProcessor::handleAsyncUpdate()
{
    // re-read the take tracks after the project is opened
    if (linkedToTracks && reaperBridge.available())
        linkChildTracks();
}

void HordeProcessor::toggleCapture()
{
    if (capArmed.load())
    {
        capStopRequest.store (true);
        return;
    }
    freeCaptureChunks();
    capLen.store (0);
    capChannels.store (0);
    capDone.store (false);
    capStopRequest.store (false);
    capSampleRate = getSampleRate() > 0 ? getSampleRate() : 48000.0;
    for (auto& c : cap)                                  // first few seconds ready before recording starts
        for (int i = 0; i < 8; ++i)
            c.chunks[(size_t) i].store (new float[kCapChunk]());
    capArmed.store (true);
}

HordeProcessor::CaptureState HordeProcessor::getCaptureState() const
{
    if (capRecording.load()) return CaptureState::recording;
    if (capArmed.load())     return CaptureState::armed;
    return CaptureState::idle;
}

void HordeProcessor::freeCaptureChunks()
{
    for (auto& c : cap)
        for (auto& ch : c.chunks)
            delete[] ch.exchange (nullptr);
}

void HordeProcessor::timerCallback()
{
    if (capArmed.load())
    {
        // keep ~8 chunks of free space ahead of the write position
        const int needed = juce::jmin (kCapMaxChunks, capLen.load() / kCapChunk + 8);
        for (auto& c : cap)
            for (int i = 0; i < needed; ++i)
                if (c.chunks[(size_t) i].load() == nullptr)
                    c.chunks[(size_t) i].store (new float[kCapChunk]());
    }
    else if (capDone.exchange (false))
    {
        finishCapture();
    }
    else if (! capArmed.load() && capLen.load() == 0 && cap[0].chunks[0].load() != nullptr)
    {
        freeCaptureChunks();   // cancelled before anything was recorded
    }
}

void HordeProcessor::finishCapture()
{
    const int len = capLen.load();
    const int chans = capChannels.load();
    std::vector<juce::AudioBuffer<float>> bufs;
    for (int c = 0; c < chans; ++c)
    {
        juce::AudioBuffer<float> b (1, len);
        b.clear();
        for (int k = 0; k * kCapChunk < len; ++k)
            if (float* src = cap[(size_t) c].chunks[(size_t) k].load())
                b.copyFrom (0, k * kCapChunk, src, juce::jmin (kCapChunk, len - k * kCapChunk));
        bufs.push_back (std::move (b));
    }
    freeCaptureChunks();
    capLen.store (0);

    // a stereo track feeds the same take into L and R: keep it once
    std::vector<bool> skip (bufs.size(), false);
    for (size_t c = 1; c < bufs.size(); ++c)
    {
        const float rms = bufs[c].getRMSLevel (0, 0, len);
        if (rms < 1.0e-5f) { skip[c] = true; continue; }
        for (size_t d = 0; d < c; ++d)
        {
            if (skip[d]) continue;
            juce::AudioBuffer<float> diff (bufs[c]);
            diff.addFrom (0, 0, bufs[d], 0, 0, len, -1.0f);
            if (diff.getRMSLevel (0, 0, len) < rms * 0.15f) { skip[c] = true; break; }
        }
    }

    takes.erase (std::remove_if (takes.begin(), takes.end(),
                                 [] (auto& t) { return t->path.startsWith ("capture:"); }), takes.end());
    int added = 0;
    for (size_t c = 0; c < bufs.size(); ++c)
    {
        if (skip[c]) continue;
        const auto name = "Captured input " + juce::String ((int) c + 1);
        if (auto t = makeTake (bufs[c], capSampleRate, name, "capture:" + juce::String ((int) c + 1), false))
        {
            takes.push_back (t);
            ++added;
        }
    }
    publishTakes();

    lastCaptureMessage = added > 0
        ? "Captured " + juce::String (added) + " take(s), " + juce::String (len / capSampleRate, 1) + " s.\n\n"
          "They are now played by the crowd and saved inside the project. Set M/F for each one in the list."
        : juce::String ("Nothing was captured: the plugin input was silent. Route the take tracks into HORDE and try again.");
    if (onCaptureFinished) onCaptureFinished (lastCaptureMessage);
}

void HordeProcessor::removeTake (int index)
{
    if (juce::isPositiveAndBelow (index, (int) takes.size()))
        takes.erase (takes.begin() + index);
    linkedToTracks = std::any_of (takes.begin(), takes.end(), [] (auto& t) { return t->path.startsWith ("track:"); });
    publishTakes();
}

void HordeProcessor::toggleFemale (int index)
{
    if (juce::isPositiveAndBelow (index, (int) takes.size()))
        takes[(size_t) index]->female = ! takes[(size_t) index]->female.load();
    if (onTakesChanged) onTakesChanged();
}

void HordeProcessor::publishTakes()
{
    auto lib = std::make_shared<horde::Library>();
    lib->takes = takes;
    for (auto& t : takes)
        lib->groupOf.push_back (t->group.isEmpty() ? 0 : groups.indexOf (t->group) + 1);
    engine.setLibrary (std::move (lib));
    if (onTakesChanged) onTakesChanged();
}

//==============================================================================
juce::String HordeProcessor::groupNameForParam (int v) const
{
    if (v <= 0) return "Custom";
    const juce::SpinLock::ScopedLockType sl (groupNamesLock);
    return v <= groupNamesForHost.size() ? groupNamesForHost[v - 1] : "Group " + juce::String (v);
}

void HordeProcessor::groupsChanged()
{
    {
        const juce::SpinLock::ScopedLockType sl (groupNamesLock);
        groupNamesForHost = groups;
    }
    publishTakes();
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
}

juce::String HordeProcessor::createGroup (const juce::String& wanted)
{
    if (groups.size() >= horde::kMaxGroups) return {};
    auto base = wanted.trim().isEmpty() ? juce::String ("Group") : wanted.trim();
    auto name = base;
    for (int k = 2; groups.contains (name); ++k)
        name = base + " " + juce::String (k);
    groups.add (name);
    groupsChanged();
    return name;
}

void HordeProcessor::renameGroup (int gi, const juce::String& wanted)
{
    if (! juce::isPositiveAndBelow (gi, groups.size())) return;
    auto name = wanted.trim();
    if (name.isEmpty() || name == groups[gi]) return;
    auto base = name;
    for (int k = 2; groups.contains (name); ++k)
        name = base + " " + juce::String (k);
    for (auto& t : takes)
        if (t->group == groups[gi]) t->group = name;
    groups.set (gi, name);
    groupsChanged();
}

void HordeProcessor::deleteGroup (int gi)
{
    if (! juce::isPositiveAndBelow (gi, groups.size())) return;
    for (auto& t : takes)
        if (t->group == groups[gi]) t->group = {};
    const int active = getActiveGroup();
    groups.remove (gi);
    if (active == gi + 1)     setActiveGroup (0);
    else if (active > gi + 1) setActiveGroup (active - 1);
    groupsChanged();
}

void HordeProcessor::setTakeGroup (int ti, const juce::String& group)
{
    if (! juce::isPositiveAndBelow (ti, (int) takes.size())) return;
    takes[(size_t) ti]->group = groups.contains (group) ? group : juce::String();
    publishTakes();
}

int HordeProcessor::getActiveGroup() const
{
    return juce::jlimit (0, groups.size(), (int) raw.group->load());
}

void HordeProcessor::setActiveGroup (int g)
{
    g = juce::jlimit (0, groups.size(), g);
    if (auto* param = apvts.getParameter ("group"))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) g));
        param->endChangeGesture();
    }
    if (onTakesChanged) onTakesChanged();
}

void HordeProcessor::stepActiveGroup (int delta)
{
    const int n = groups.size() + 1;
    setActiveGroup (((getActiveGroup() + delta) % n + n) % n);
}

juce::String HordeProcessor::getActiveGroupLabel() const
{
    if (takes.empty()) return "None";
    const int g = getActiveGroup();
    return g == 0 ? juce::String ("Custom") : groups[g - 1];
}

int HordeProcessor::countTakesInGroup (int g) const
{
    if (g <= 0) return (int) takes.size();
    int n = 0;
    for (auto& t : takes) n += t->group == groups[g - 1] ? 1 : 0;
    return n;
}

void HordeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("Files"), nullptr);
    juce::ValueTree files ("Files");
    files.setProperty ("linkTracks", linkedToTracks, nullptr);
    for (auto& t : takes)
        if (! t->path.startsWith ("track:"))
            files.appendChild (juce::ValueTree ("File", { { "path", t->path }, { "female", t->female.load() }, { "group", t->group } }), nullptr);
    for (auto& t : takes)
        if (t->path.startsWith ("track:") && t->group.isNotEmpty())
            files.appendChild (juce::ValueTree ("TrackGroup", { { "path", t->path }, { "group", t->group } }), nullptr);
    for (auto& gname : groups)
        files.appendChild (juce::ValueTree ("Group", { { "name", gname } }), nullptr);
    for (auto& t : takes)
    {
        if (! t->path.startsWith ("capture:")) continue;
        juce::MemoryBlock mb;
        {
            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (new juce::MemoryOutputStream (mb, false),
                                                                              t->sampleRate, 1, 16, {}, 0));
            if (w != nullptr)
                w->writeFromAudioSampleBuffer (t->audio, 0, t->audio.getNumSamples());
        }
        files.appendChild (juce::ValueTree ("Captured", { { "name", t->name }, { "id", t->path }, { "female", t->female.load() },
                                                          { "group", t->group }, { "flac", mb.toBase64Encoding() } }), nullptr);
    }
    state.appendChild (files, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void HordeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            auto files = tree.getChildWithName ("Files");
            juce::StringArray paths;
            std::vector<bool> fem;
            std::vector<std::shared_ptr<horde::Take>> captured;
            juce::StringArray fileGroups;
            groups.clear();
            pendingTrackGroups.clear();
            for (auto f : files)
            {
                if (f.hasType ("Group"))      { groups.addIfNotAlreadyThere (f["name"].toString()); continue; }
                if (f.hasType ("TrackGroup")) { pendingTrackGroups.set (f["path"].toString(), f["group"].toString()); continue; }
                if (f.hasType ("Captured"))
                {
                    juce::MemoryBlock mb;
                    mb.fromBase64Encoding (f["flac"].toString());
                    juce::FlacAudioFormat flac;
                    std::unique_ptr<juce::AudioFormatReader> rd (flac.createReaderFor (new juce::MemoryInputStream (mb, true), true));
                    if (rd != nullptr)
                    {
                        juce::AudioBuffer<float> b (1, (int) rd->lengthInSamples);
                        rd->read (&b, 0, (int) rd->lengthInSamples, 0, true, false);
                        if (auto t = makeTake (b, rd->sampleRate, f["name"].toString(), f["id"].toString(), (bool) f["female"]))
                        {
                            t->group = f["group"].toString();
                            captured.push_back (t);
                        }
                    }
                    continue;
                }
                paths.add (f["path"].toString());
                fem.push_back ((bool) f["female"]);
                fileGroups.add (f["group"].toString());
            }
            tree.removeChild (files, nullptr);
            apvts.replaceState (tree);

            takes.clear();
            linkedToTracks = (bool) files.getProperty ("linkTracks", false);
            for (auto& c : captured) takes.push_back (c);
            addFiles (paths);
            if (linkedToTracks)
                triggerAsyncUpdate();
            for (size_t i = 0; i < takes.size(); ++i)
                for (int k = 0; k < paths.size(); ++k)
                    if (paths[k] == takes[i]->path && (size_t) k < fem.size())
                    {
                        takes[i]->female = fem[(size_t) k];
                        takes[i]->group = fileGroups[k];
                    }
            groupsChanged();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HordeProcessor(); }
