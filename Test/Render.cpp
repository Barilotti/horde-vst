// Offline render.
//   HordeRender in.wav out.wav [voices]              live-input mode (multichannel wav = takes)
//   HordeRender --files out.wav voices a.wav b.wav   files mode
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"

static void setP (HordeProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::puts ("usage: see source"); return 1; }
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    const bool files = juce::String (argv[1]) == "--files";
    const bool capture = juce::String (argv[1]) == "--capture";   // --capture in.wav out.wav voices
    if (capture) { argv[1] = argv[2]; argv[2] = argv[3]; }

    HordeProcessor proc;
    auto layout = proc.getBusesLayout();
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::discreteChannels (8);
    const bool surround = std::getenv ("HORDE_51") != nullptr;
    if (surround) layout.outputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    proc.setBusesLayout (layout);
    const int nOut = surround ? 6 : 2;

    juce::AudioBuffer<float> src;
    double sr = 48000.0;
    int nIn = 0, len = (int) (sr * 20);
    if (files)
    {
        juce::StringArray paths;
        for (int i = 4; i < argc; ++i) paths.add (juce::File::getCurrentWorkingDirectory().getChildFile (argv[i]).getFullPathName());
        proc.addFiles (paths);
        std::printf ("loaded %d takes\n", (int) proc.getTakes().size());
        for (auto& t : proc.getTakes())
            std::printf ("  %-16s %6.1f s, %3d phrases, voiced %3d%%\n", t->name.toRawUTF8(), t->audio.getNumSamples() / t->sampleRate,
                         (int) t->phrases.size(), (int) (100 * t->voicedHops / juce::jmax<size_t> (1, t->voiced.size())));
        setP (proc, "voices", (float) std::atoi (argv[3]));
        if (auto* g = std::getenv ("HORDE_GROUP"))   // put take 1 into group "A" and play only that group
        {
            proc.createGroup ("A");
            proc.setTakeGroup (0, "A");
            proc.setActiveGroup (std::atoi (g));
            std::printf ("group label: %s, takes in it: %d\n", proc.getActiveGroupLabel().toRawUTF8(), proc.countTakesInGroup (1));
            juce::MemoryBlock st; proc.getStateInformation (st);
            HordeProcessor p2; p2.setStateInformation (st.getData(), (int) st.getSize());
            std::printf ("after reload: groups %d, label %s, in group %d\n", p2.getGroups().size(), p2.getActiveGroupLabel().toRawUTF8(), p2.countTakesInGroup (1));
        }
    }
    else
    {
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (argv[1])));
        if (! rd) return 2;
        sr = rd->sampleRate; nIn = (int) rd->numChannels; len = (int) rd->lengthInSamples + (int) (sr * 3);
        src.setSize (nIn, len); src.clear();
        rd->read (&src, 0, (int) rd->lengthInSamples, 0, true, true);
        if (argc > 3 && ! capture) setP (proc, "voices", (float) std::atoi (argv[3]));
    }
    proc.prepareToPlay (sr, 256);

    juce::AudioBuffer<float> out (nOut, len), blk (8, 256); juce::MidiBuffer midi;
    if (capture)
    {
        proc.toggleCapture();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        for (int pos = 0; pos < len; pos += 256)
        {
            const int n = juce::jmin (256, len - pos);
            blk.setSize (8, n, false, false, true); blk.clear();
            for (int c = 0; c < nIn; ++c) blk.copyFrom (c, 0, src, c, pos, n);
            proc.processBlock (blk, midi);
            if (pos % 48000 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        }
        proc.toggleCapture();
        proc.processBlock (blk, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
        std::printf ("%s\ntakes now: %d\n", proc.lastCaptureMessage.toRawUTF8(), (int) proc.getTakes().size());
        src.clear(); len = (int) (sr * 20); out.setSize (nOut, len); src.setSize (nIn, len);  src.clear();
        // state round-trip
        juce::MemoryBlock st; proc.getStateInformation (st);
        std::printf ("state size %d KB\n", (int) st.getSize() / 1024);
        proc.setStateInformation (st.getData(), (int) st.getSize());
        std::printf ("takes after reload: %d\n", (int) proc.getTakes().size());
    }
    for (int pos = 0; pos < len; pos += 256)
    {
        const int n = juce::jmin (256, len - pos);
        blk.setSize (8, n, false, false, true); blk.clear();
        for (int c = 0; c < nIn; ++c) blk.copyFrom (c, 0, src, c, pos, n);
        proc.processBlock (blk, midi);
        for (int c = 0; c < nOut; ++c) out.copyFrom (c, pos, blk, c, 0, n);
    }
    std::printf ("peak %.3f  rms:", out.getMagnitude (0, len));
    for (int c = 0; c < nOut; ++c) std::printf (" %.4f", out.getRMSLevel (c, 0, len));
    std::printf ("\n");
    juce::File of = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]); of.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (of), sr, (unsigned) nOut, 24, {}, 0));
    w->writeFromAudioSampleBuffer (out, 0, len);
    return 0;
}
