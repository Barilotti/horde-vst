// HordeSnapshot takes.wav out.png : runs audio in a thread, renders editor to PNG.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (argv[1])));
    const int nIn = (int) rd->numChannels, len = (int) rd->lengthInSamples;
    juce::AudioBuffer<float> src (nIn, len);
    rd->read (&src, 0, len, 0, true, true);

    HordeProcessor proc;
    auto layout = proc.getBusesLayout();
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::discreteChannels (8);
    if (std::getenv ("HORDE_51")) layout.outputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    proc.setBusesLayout (layout);
    proc.apvts.getParameter ("voices")->setValueNotifyingHost (proc.apvts.getParameter ("voices")->convertTo0to1 (48.0f));
    proc.apvts.getParameter ("seed")->setValueNotifyingHost (proc.apvts.getParameter ("seed")->convertTo0to1 (4827.0f));
    {
        juce::StringArray paths;
        for (int i = 4; i < argc; ++i) paths.add (juce::File::getCurrentWorkingDirectory().getChildFile (argv[i]).getFullPathName());
        if (! paths.isEmpty()) proc.addFiles (paths);
        if (std::getenv ("HORDE_GROUPS"))
        {
            proc.createGroup ("Zombies");
            proc.createGroup (juce::String::fromUTF8 ("Кричащая толпа"));
            proc.setTakeGroup (0, "Zombies");
            proc.setTakeGroup (1, "Zombies");
            proc.setTakeGroup (2, juce::String::fromUTF8 ("Кричащая толпа"));
            proc.setActiveGroup (1);
        }
    }
    proc.prepareToPlay (rd->sampleRate, 512);

    std::atomic<bool> run { true };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> blk (8, 512); juce::MidiBuffer midi;
        int pos = 24000;
        while (run)
        {
            blk.clear();
            for (int c = 0; c < nIn; ++c) blk.copyFrom (c, 0, src, c, pos, 512);
            proc.processBlock (blk, midi);
            pos += 512; if (pos + 512 >= len) pos = 24000;
            juce::Thread::sleep (10);
        }
    });

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setVisible (true);
    if (auto* panel = std::getenv ("HORDE_PANEL"))
        if (auto* c = dynamic_cast<horde::ui::Content*> (ed->getChildComponent (0)))
            c->showPanel (std::atoi (panel));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, argc > 3 ? 2.0f : 1.0f);
    run = false; audio.join();
    juce::File out (argv[2]); out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    ed.reset();
    return 0;
}
