#include "PluginEditor.h"

namespace
{
const juce::Colour bg (0xff1b1e26), panel (0xff262a35), accent (0xff4fc3f7), hot (0xffffb74d), textCol (0xffe8eaf0);
constexpr int kW = 900, kH = 600, kCols = 8, kCellH = 64, kGridY = 262, kMaxSlots = 32;
const float lengths[] = { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };
}

ChordCanvasEditor::ChordCanvasEditor (ChordCanvasProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setSize (kW, kH);

    auto styleToggle = [] (juce::TextButton& b) { b.setClickingTogglesState (true); b.setRadioGroupId (0); };
    for (int i = 0; i < 12; ++i)
    {
        auto* b = rootBtns.add (new juce::TextButton (chord::noteName (i)));
        styleToggle (*b);
        b->setRadioGroupId (101);
        b->setColour (juce::TextButton::buttonOnColourId, accent);
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b->onClick = [this, i] {
            if (! rootBtns[i]->getToggleState()) return;
            curRoot = i;
            if (selected >= 0) { slots[(size_t) selected].root = i; commit(); }
            audition();
        };
        addAndMakeVisible (b);
    }
    for (size_t i = 0; i < chord::qualities().size(); ++i)
    {
        auto* b = qualBtns.add (new juce::TextButton (chord::qualities()[i].label));
        styleToggle (*b);
        b->setRadioGroupId (102);
        b->setColour (juce::TextButton::buttonOnColourId, hot);
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b->onClick = [this, i] {
            if (! qualBtns[(int) i]->getToggleState()) return;
            curQual = (int) i;
            if (selected >= 0) { slots[(size_t) selected].quality = (int) i; commit(); }
            audition();
        };
        addAndMakeVisible (b);
    }

    lenBox.addItemList ({ "1/2 beat", "1 beat", "2 beats", "4 beats (1 bar)", "8 beats (2 bars)" }, 1);
    lenBox.setSelectedId (4, juce::dontSendNotification);
    lenBox.onChange = [this] {
        if (selected >= 0) { slots[(size_t) selected].beats = lengthFromBox(); commit(); }
    };

    for (int i = 0; i < 12; ++i) keyBox.addItem (chord::noteName (i), i + 1);
    keyBox.setSelectedId (1, juce::dontSendNotification);
    for (size_t i = 0; i < chord::progressions().size(); ++i)
        progBox.addItem (chord::progressions()[i].name, (int) i + 1);
    progBox.setSelectedId (1, juce::dontSendNotification);

    modeBox.addItemList ({ "Piano voicing", "Guitar voicing (strum)" }, 1);
    modeBox.setSelectedId (proc.guitarMode.load() ? 2 : 1, juce::dontSendNotification);
    modeBox.onChange = [this] { proc.guitarMode = (modeBox.getSelectedId() == 2); audition(); repaint(); };

    for (auto* s : { &strumSlider, &gainSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
        addAndMakeVisible (s);
    }
    strumSlider.setRange (0.0, 120.0, 1.0);
    strumSlider.setValue (proc.strumMs.load(), juce::dontSendNotification);
    strumSlider.onValueChange = [this] { proc.strumMs = (float) strumSlider.getValue(); };
    gainSlider.setRange (0.0, 1.0, 0.01);
    gainSlider.setValue (proc.gain.load(), juce::dontSendNotification);
    gainSlider.onValueChange = [this] { proc.gain = (float) gainSlider.getValue(); };

    for (auto* l : { &strumLabel, &gainLabel, &lenLabel, &keyLabel })
    {
        l->setColour (juce::Label::textColourId, textCol);
        addAndMakeVisible (l);
    }

    addBtn.onClick = [this] {
        if ((int) slots.size() >= kMaxSlots) return;
        slots.push_back ({ curRoot, curQual, lengthFromBox() });
        selected = (int) slots.size() - 1;
        commit();
        audition();
    };
    delBtn.onClick = [this] {
        if (selected < 0) return;
        slots.erase (slots.begin() + selected);
        selected = juce::jmin (selected, (int) slots.size() - 1);
        commit(); refreshPalette();
    };
    dupBtn.onClick = [this] {
        if (selected < 0 || (int) slots.size() >= kMaxSlots) return;
        slots.insert (slots.begin() + selected + 1, slots[(size_t) selected]);
        ++selected; commit();
    };
    leftBtn.onClick = [this] {
        if (selected > 0) { std::swap (slots[(size_t) selected], slots[(size_t) selected - 1]); --selected; commit(); }
    };
    rightBtn.onClick = [this] {
        if (selected >= 0 && selected + 1 < (int) slots.size())
        { std::swap (slots[(size_t) selected], slots[(size_t) selected + 1]); ++selected; commit(); }
    };
    clearBtn.onClick = [this] { slots.clear(); selected = -1; commit(); };
    applyBtn.onClick = [this] {
        const auto& pr = chord::progressions()[(size_t) progBox.getSelectedId() - 1];
        const int key = keyBox.getSelectedId() - 1;
        slots.clear();
        for (auto& st : pr.steps)
            slots.push_back ({ (key + st.offset) % 12, st.quality, lengthFromBox() });
        selected = 0; commit(); refreshPalette(); audition();
    };

    dragBtn.setButtonText ("Drag MIDI to track");
    dragBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2e7d32));
    dragBtn.makeFile = [this] { return writeMidiFile(); };
    saveBtn.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Save MIDI", juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                                                                        .getChildFile ("ChordCanvas.mid"), "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc) {
                                  auto f = fc.getResult();
                                  if (f == juce::File()) return;
                                  f.deleteFile();
                                  juce::FileOutputStream os (f);
                                  if (os.openedOk()) proc.buildMidiFile().writeTo (os);
                              });
    };

    for (auto* c : std::initializer_list<juce::Component*> { &addBtn, &delBtn, &dupBtn, &leftBtn, &rightBtn, &clearBtn,
                                                              &applyBtn, &saveBtn, &dragBtn, &modeBox, &lenBox, &keyBox, &progBox })
        addAndMakeVisible (c);

    slots = proc.getSlots();
    revision = proc.getRevision();
    if (! slots.empty()) selected = 0;
    refreshPalette();
    startTimerHz (30);
}

ChordCanvasEditor::~ChordCanvasEditor() { stopTimer(); }

float ChordCanvasEditor::lengthFromBox() const
{
    return lengths[juce::jlimit (0, 4, lenBox.getSelectedId() - 1)];
}

void ChordCanvasEditor::commit()
{
    proc.setSlots (slots);
    revision = proc.getRevision();
    refreshPalette();
    repaint();
}

void ChordCanvasEditor::audition()
{
    if (selected >= 0 && selected < (int) slots.size())
        proc.auditionSlot (slots[(size_t) selected]);
}

void ChordCanvasEditor::refreshPalette()
{
    if (selected >= 0 && selected < (int) slots.size())
    {
        const auto& s = slots[(size_t) selected];
        curRoot = s.root; curQual = s.quality;
        for (int i = 0; i < 5; ++i)
            if (juce::approximatelyEqual (lengths[i], s.beats)) lenBox.setSelectedId (i + 1, juce::dontSendNotification);
    }
    rootBtns[curRoot]->setToggleState (true, juce::dontSendNotification);
    qualBtns[curQual]->setToggleState (true, juce::dontSendNotification);
}

void ChordCanvasEditor::select (int i)
{
    selected = i;
    refreshPalette();
    audition();
    repaint();
}

juce::File ChordCanvasEditor::writeMidiFile()
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ChordCanvas.mid");
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (os.openedOk()) proc.buildMidiFile().writeTo (os);
    return f;
}

void ChordCanvasEditor::timerCallback()
{
    if (proc.getRevision() != revision)
    {
        slots = proc.getSlots();
        revision = proc.getRevision();
        if (selected >= (int) slots.size()) selected = (int) slots.size() - 1;
        refreshPalette();
        repaint();
    }
    const int ps = proc.playingSlot.load();
    if (ps != shownPlaying) { shownPlaying = ps; repaint(); }
}

juce::Rectangle<int> ChordCanvasEditor::cellBounds (int i) const
{
    const int pad = 20, w = (kW - pad * 2) / kCols;
    return { pad + (i % kCols) * w + 2, kGridY + (i / kCols) * (kCellH + 6), w - 4, kCellH };
}

void ChordCanvasEditor::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < (int) slots.size(); ++i)
        if (cellBounds (i).contains (e.getPosition())) { select (i); return; }
}

void ChordCanvasEditor::resized()
{
    const int pad = 20;
    modeBox.setBounds (pad, 46, 180, 26);
    strumLabel.setBounds (216, 46, 62, 26);
    strumSlider.setBounds (278, 46, 190, 26);
    gainLabel.setBounds (484, 46, 52, 26);
    gainSlider.setBounds (536, 46, 150, 26);
    dragBtn.setBounds (kW - pad - 250, 46, 160, 26);
    saveBtn.setBounds (kW - pad - 84, 46, 84, 26);

    keyLabel.setBounds (pad, 86, 34, 26);
    keyBox.setBounds (pad + 36, 86, 64, 26);
    progBox.setBounds (pad + 110, 86, 280, 26);
    applyBtn.setBounds (pad + 400, 86, 80, 26);

    const int bw = (kW - pad * 2) / 12;
    for (int i = 0; i < 12; ++i)
    {
        rootBtns[i]->setBounds (pad + i * bw + 1, 128, bw - 2, 34);
        qualBtns[i]->setBounds (pad + i * bw + 1, 170, bw - 2, 34);
    }

    lenLabel.setBounds (pad, 218, 52, 26);
    lenBox.setBounds (pad + 54, 218, 150, 26);
    int x = pad + 220;
    for (auto* b : { &addBtn, &delBtn, &dupBtn })
    { b->setBounds (x, 218, 84, 26); x += 90; }
    leftBtn.setBounds (x, 218, 40, 26);  x += 46;
    rightBtn.setBounds (x, 218, 40, 26); x += 52;
    clearBtn.setBounds (x, 218, 70, 26);
}

void ChordCanvasEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (textCol);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("ChordCanvas", 20, 8, 300, 30, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (textCol.withAlpha (0.55f));
    g.drawText ("Pick root + type, press Add. Press play in Cubase to hear it synced to the project tempo.",
                200, 8, kW - 220, 30, juce::Justification::centredRight);

    // コード列
    const int rows = juce::jmax (1, ((int) slots.size() + kCols - 1) / kCols);
    g.setColour (panel);
    g.fillRoundedRectangle (14.0f, (float) kGridY - 8.0f, (float) kW - 28.0f, (float) rows * (kCellH + 6) + 10.0f, 8.0f);

    if (slots.empty())
    {
        g.setColour (textCol.withAlpha (0.5f));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("No chords yet. Choose a root and a type above, then press Add (or Apply a progression).",
                    14, kGridY, kW - 28, kCellH, juce::Justification::centred);
    }
    for (int i = 0; i < (int) slots.size(); ++i)
    {
        auto r = cellBounds (i).toFloat();
        const bool playing = (i == shownPlaying), sel = (i == selected);
        g.setColour (playing ? hot.withAlpha (0.85f) : juce::Colour (0xff353b4b));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (sel ? accent : juce::Colours::transparentBlack);
        g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, 2.5f);
        g.setColour (playing ? juce::Colours::black : textCol);
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText (chord::chordName (slots[(size_t) i]), r.removeFromTop (r.getHeight() * 0.68f).toNearestInt(), juce::Justification::centred);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (juce::String (slots[(size_t) i].beats) + " beats", r.toNearestInt(), juce::Justification::centred);
    }

    // 鍵盤表示: 再生中ならそのコード、なければ選択中のコード
    const int showIdx = shownPlaying >= 0 ? shownPlaying : selected;
    const int ky = 470, kh = 100, pad = 20;
    const int whiteCount = 28, ww = (kW - pad * 2) / whiteCount;
    static const int whiteOf[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    static const bool black[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
    std::vector<int> lit;
    if (showIdx >= 0 && showIdx < (int) slots.size())
    {
        lit = chord::voice (slots[(size_t) showIdx], proc.guitarMode.load());
        g.setColour (textCol);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (chord::chordName (slots[(size_t) showIdx]) + (proc.guitarMode.load() ? "  (guitar voicing)" : "  (piano voicing)"),
                    pad, ky - 28, 400, 22, juce::Justification::centredLeft);
    }
    auto isLit = [&] (int m) { return std::find (lit.begin(), lit.end(), m) != lit.end(); };
    auto whiteX = [&] (int m) { return pad + (((m / 12) - 3) * 7 + whiteOf[m % 12]) * ww; };

    for (int m = 36; m < 84; ++m)
    {
        if (black[m % 12]) continue;
        auto r = juce::Rectangle<int> (whiteX (m), ky, ww - 1, kh);
        g.setColour (isLit (m) ? accent : juce::Colours::white);
        g.fillRect (r);
    }
    for (int m = 36; m < 84; ++m)
    {
        if (! black[m % 12]) continue;
        auto r = juce::Rectangle<int> (whiteX (m - 1) + ww * 7 / 10, ky, ww * 6 / 10, kh * 6 / 10);
        g.setColour (isLit (m) ? accent.darker (0.3f) : juce::Colour (0xff111111));
        g.fillRect (r);
    }
}
