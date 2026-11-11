// Offline render: HordeRender in.wav out.wav [voices]
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::puts ("usage: HordeRender in.wav out.wav [voices]"); return 1; }
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (argv[1])));
    if (! rd) return 2;
    const int nIn = (int) rd->numChannels, len = (int) rd->lengthInSamples + (int) (rd->sampleRate * 3);
    juce::AudioBuffer<float> src (nIn, len); src.clear();
    rd->read (&src, 0, (int) rd->lengthInSamples, 0, true, true);

    HordeProcessor proc;
    auto layout = proc.getBusesLayout();
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::discreteChannels (8);
    proc.setBusesLayout (layout);
    if (argc > 3) proc.apvts.getParameter ("voices")->setValueNotifyingHost (proc.apvts.getParameter ("voices")->convertTo0to1 ((float) std::atoi (argv[3])));
    proc.apvts.getParameter ("takes")->setValueNotifyingHost (proc.apvts.getParameter ("takes")->convertTo0to1 ((float) nIn));
    proc.prepareToPlay (rd->sampleRate, 256);

    juce::AudioBuffer<float> out (2, len), blk (8, 256); juce::MidiBuffer midi;
    for (int pos = 0; pos < len; pos += 256)
    {
        const int n = juce::jmin (256, len - pos);
        blk.setSize (8, n, false, false, true); blk.clear();
        for (int c = 0; c < nIn; ++c) blk.copyFrom (c, 0, src, c, pos, n);
        proc.processBlock (blk, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
    }
    std::printf ("peak %.3f  rms L %.4f\n", out.getMagnitude (0, len), out.getRMSLevel (0, 0, len));
    juce::File of (argv[2]); of.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (of), rd->sampleRate, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer (out, 0, len);
    return 0;
}
