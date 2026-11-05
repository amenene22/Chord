#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// 押したままドラッグすると .mid を DAW(Cubase)のトラックへ渡せるボタン
class MidiDragButton : public juce::TextButton
{
public:
    std::function<juce::File()> makeFile;
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() > 6 && makeFile != nullptr)
            if (auto f = makeFile(); f.existsAsFile())
                juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
    }
};

class ChordCanvasEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ChordCanvasEditor (ChordCanvasProcessor&);
    ~ChordCanvasEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<int> cellBounds (int i) const;
    void commit();
    void select (int i);
    void refreshPalette();
    void audition();
    juce::File writeMidiFile();
    float lengthFromBox() const;

    ChordCanvasProcessor& proc;
    std::vector<chord::Slot> slots;
    int selected = -1, shownPlaying = -1, revision = -1;
    int curRoot = 0, curQual = 0;

    juce::OwnedArray<juce::TextButton> rootBtns, qualBtns;
    juce::TextButton addBtn { "Add" }, delBtn { "Delete" }, dupBtn { "Duplicate" },
                     leftBtn { "<" }, rightBtn { ">" }, clearBtn { "Clear" },
                     applyBtn { "Apply" }, saveBtn { "Save .mid" };
    MidiDragButton dragBtn;
    juce::ComboBox modeBox, lenBox, keyBox, progBox;
    juce::Slider strumSlider, gainSlider;
    juce::Label strumLabel { {}, "Strum ms" }, gainLabel { {}, "Volume" },
                lenLabel { {}, "Length" }, keyLabel { {}, "Key" };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordCanvasEditor)
};
