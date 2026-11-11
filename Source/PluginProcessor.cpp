#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

static juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

APVTS::ParameterLayout HordeProcessor::createLayout()
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

    return { ps.begin(), ps.end() };
}

HordeProcessor::HordeProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Takes", juce::AudioChannelSet::discreteChannels (horde::kMaxTakes), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "HORDE", createLayout())
{
    auto g = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    formats.registerBasicFormats();
    raw = { g ("voices"), g ("takes"), g ("maleTakes"), g ("balance"), g ("delayMin"), g ("delaySpread"),
            g ("pitchSpread"), g ("drift"), g ("timbre"), g ("humanize"), g ("distance"), g ("depthSpread"),
            g ("width"), g ("reverb"), g ("room"), g ("dry"), g ("wet"), g ("seed"), g ("roomType"), g ("scatter"), g ("loose"), g ("mix") };
}

bool HordeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    const int nIn = layouts.getMainInputChannels();
    return nIn >= 1 && nIn <= 16;
}

void HordeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    engine.prepare (sampleRate, 512);
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

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            p.hasTransport = true;
            p.playing = pos->getIsPlaying();
            p.timeInSamples = pos->getTimeInSamples().orFallback (0);
        }

    const int nIn = getTotalNumInputChannels();
    numInputsSeen.store (nIn);

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
void HordeProcessor::addFiles (const juce::StringArray& paths)
{
    for (const auto& path : paths)
    {
        if (takes.size() >= 32) break;
        juce::File f (path);
        bool dup = false;
        for (auto& t : takes) dup |= (t->path == f.getFullPathName());
        if (dup) continue;

        std::unique_ptr<juce::AudioFormatReader> rd (formats.createReaderFor (f));
        if (rd == nullptr || rd->lengthInSamples < 1000) continue;
        const int len = (int) juce::jmin (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 600.0));
        juce::AudioBuffer<float> tmp ((int) rd->numChannels, len);
        rd->read (&tmp, 0, len, 0, true, true);

        auto t = std::make_shared<horde::Take>();
        t->sampleRate = rd->sampleRate;
        t->audio.setSize (1, len);
        t->audio.clear();
        for (int c = 0; c < tmp.getNumChannels(); ++c)
            t->audio.addFrom (0, 0, tmp, c, 0, len, 1.0f / (float) tmp.getNumChannels());

        // normalise peak to -3 dB so quiet and loud takes sit together
        const float pk = t->audio.getMagnitude (0, 0, len);
        if (pk > 1.0e-4f) t->audio.applyGain (0.708f / pk);

        t->hop = juce::jmax (1, (int) (rd->sampleRate * 0.01));
        float mx = 0.0f;
        for (int s = 0; s + t->hop <= len; s += t->hop)
        {
            const float r = t->audio.getRMSLevel (0, s, t->hop);
            t->env.push_back (r);
            mx = juce::jmax (mx, r);
        }
        if (mx > 0.0f) for (auto& e : t->env) e /= mx;

        t->name = f.getFileNameWithoutExtension();
        t->path = f.getFullPathName();
        const auto lower = t->name.toLowerCase();
        t->female = lower.contains ("fem") || lower.contains ("woman") || lower.contains (juce::String::fromUTF8 ("жен"))
                    || lower.contains ("girl");
        takes.push_back (t);
    }
    publishTakes();
}

ReaperBridge::Result HordeProcessor::linkChildTracks()
{
    auto r = reaperBridge.linkChildTracks();
    if (r.ok)
    {
        if (auto* prm = apvts.getParameter ("maleTakes"))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) juce::jmin (r.males, horde::kMaxTakes)));
            prm->endChangeGesture();
        }
        if (auto* prm = apvts.getParameter ("takes"))
            prm->setValueNotifyingHost (prm->convertTo0to1 (0.0f)); // Auto
    }
    return r;
}

void HordeProcessor::removeTake (int index)
{
    if (juce::isPositiveAndBelow (index, (int) takes.size()))
        takes.erase (takes.begin() + index);
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
    engine.setLibrary (std::move (lib));
    if (onTakesChanged) onTakesChanged();
}

void HordeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("Files"), nullptr);
    juce::ValueTree files ("Files");
    for (auto& t : takes)
        files.appendChild (juce::ValueTree ("File", { { "path", t->path }, { "female", t->female.load() } }), nullptr);
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
            for (auto f : files)
            {
                paths.add (f["path"].toString());
                fem.push_back ((bool) f["female"]);
            }
            tree.removeChild (files, nullptr);
            apvts.replaceState (tree);

            takes.clear();
            addFiles (paths);
            for (size_t i = 0; i < takes.size(); ++i)
                for (int k = 0; k < paths.size(); ++k)
                    if (paths[k] == takes[i]->path && (size_t) k < fem.size())
                        takes[i]->female = fem[(size_t) k];
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HordeProcessor(); }
