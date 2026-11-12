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
    proc.setBusesLayout (layout);

    juce::AudioBuffer<float> src;
    double sr = 48000.0;
    int nIn = 0, len = (int) (sr * 20);
    if (files)
    {
        juce::StringArray paths;
        for (int i = 4; i < argc; ++i) paths.add (juce::File::getCurrentWorkingDirectory().getChildFile (argv[i]).getFullPathName());
        proc.addFiles (paths);
        std::printf ("loaded %d takes\n", (int) proc.getTakes().size());
        setP (proc, "voices", (float) std::atoi (argv[3]));
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

    juce::AudioBuffer<float> out (2, len), blk (8, 256); juce::MidiBuffer midi;
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
        src.clear(); len = (int) (sr * 20); out.setSize (2, len); src.setSize (nIn, len);  src.clear();
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
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
    }
    std::printf ("peak %.3f  rms L %.4f\n", out.getMagnitude (0, len), out.getRMSLevel (0, 0, len));
    juce::File of = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]); of.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (of), sr, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer (out, 0, len);
    return 0;
}
