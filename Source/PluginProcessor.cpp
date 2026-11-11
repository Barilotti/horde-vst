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
    addInt ("takes",     "Takes",      1, horde::kMaxTakes, 6);
    addInt ("maleTakes", "Male takes", 0, horde::kMaxTakes, 3);
    addF ("balance", "M/F balance", -1.0f, 1.0f, 0.0f, 0.0f, [] (float v, int)
    {
        if (std::abs (v) < 0.005f) return String ("even");
        return (v < 0 ? String ("M +") : String ("F +")) + String (juce::roundToInt (std::abs (v) * 100.0f));
    });
    addF ("delayMin",    "Delay min",    5.0f, 100.0f, 12.0f, 25.0f, [] (float v, int) { return String (v, 1) + " ms"; });
    addF ("delaySpread", "Delay spread", 0.0f, 500.0f, 90.0f, 100.0f, [] (float v, int) { return String (juce::roundToInt (v)) + " ms"; });
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
    raw = { g ("voices"), g ("takes"), g ("maleTakes"), g ("balance"), g ("delayMin"), g ("delaySpread"),
            g ("pitchSpread"), g ("drift"), g ("timbre"), g ("humanize"), g ("distance"), g ("depthSpread"),
            g ("width"), g ("reverb"), g ("room"), g ("dry"), g ("wet"), g ("seed"), g ("roomType") };
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
        done += chunk;
    }
}

juce::AudioProcessorEditor* HordeProcessor::createEditor() { return new HordeEditor (*this); }

void HordeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void HordeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HordeProcessor(); }
