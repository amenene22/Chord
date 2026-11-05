#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>

// コード理論まわり(UIにもオーディオにも依存しない)
namespace chord
{
inline const char* noteName (int pc)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n[((pc % 12) + 12) % 12];
}

struct Quality { const char* label; const char* suffix; std::vector<int> iv; };

// 並び順を変えない(保存データがインデックスを使う)
inline const std::vector<Quality>& qualities()
{
    static const std::vector<Quality> q = {
        { "maj",  "",     { 0, 4, 7 } },
        { "m",    "m",    { 0, 3, 7 } },
        { "7",    "7",    { 0, 4, 7, 10 } },
        { "maj7", "maj7", { 0, 4, 7, 11 } },
        { "m7",   "m7",   { 0, 3, 7, 10 } },
        { "sus4", "sus4", { 0, 5, 7 } },
        { "sus2", "sus2", { 0, 2, 7 } },
        { "dim",  "dim",  { 0, 3, 6 } },
        { "aug",  "aug",  { 0, 4, 8 } },
        { "add9", "add9", { 0, 4, 7, 14 } },
        { "6",    "6",    { 0, 4, 7, 9 } },
        { "m7b5", "m7b5", { 0, 3, 6, 10 } },
    };
    return q;
}

struct Slot
{
    int root = 0;        // 0..11 (C=0)
    int quality = 0;     // qualities() のインデックス
    float beats = 4.0f;  // 長さ(拍)
};

inline juce::String chordName (const Slot& s)
{
    return juce::String (noteName (s.root)) + qualities()[(size_t) s.quality].suffix;
}

// ピアノ: 左手ベース + 右手クローズドポジション / ギター: 低音弦から積むストラム用ボイシング
inline std::vector<int> voice (const Slot& s, bool guitar)
{
    const auto& iv = qualities()[(size_t) s.quality].iv;
    std::vector<int> n;

    if (! guitar)
    {
        n.push_back (36 + s.root);
        const int base = 60 + s.root - (s.root >= 6 ? 12 : 0);
        for (int i : iv) n.push_back (base + i);
    }
    else
    {
        const int base = 40 + ((s.root - 4 + 12) % 12); // E2..D#3
        int fifth = 7;
        for (int i : iv)
        {
            const int m = i % 12;
            if (m == 6 || m == 7 || m == 8) fifth = m;
        }
        n.push_back (base);
        n.push_back (base + fifth);
        n.push_back (base + 12);
        for (int i : iv)
        {
            const int m = i % 12;
            if (m == 0 || m == fifth) continue;
            n.push_back (base + 12 + m);
        }
    }

    std::sort (n.begin(), n.end());
    n.erase (std::unique (n.begin(), n.end()), n.end());
    return n;
}

struct ProgStep { int offset; int quality; };
struct Progression { const char* name; std::vector<ProgStep> steps; };

inline const std::vector<Progression>& progressions()
{
    static const std::vector<Progression> p = {
        { "I-V-vi-IV (Pop)",             { { 0, 0 }, { 7, 0 }, { 9, 1 }, { 5, 0 } } },
        { "IVM7-V7-iii7-vi7 (Oudou)",    { { 5, 3 }, { 7, 2 }, { 4, 4 }, { 9, 4 } } },
        { "vi-IV-I-V",                   { { 9, 1 }, { 5, 0 }, { 0, 0 }, { 7, 0 } } },
        { "I-vi-IV-V (50s)",             { { 0, 0 }, { 9, 1 }, { 5, 0 }, { 7, 0 } } },
        { "ii7-V7-IM7 (Jazz)",           { { 2, 4 }, { 7, 2 }, { 0, 3 } } },
        { "Canon",                       { { 0, 0 }, { 7, 0 }, { 9, 1 }, { 4, 1 }, { 5, 0 }, { 0, 0 }, { 5, 0 }, { 7, 0 } } },
        { "IVM7-III7-vi7-I7 (City pop)", { { 5, 3 }, { 4, 2 }, { 9, 4 }, { 0, 2 } } },
        { "I-IV-V7-I (Blues-ish)",       { { 0, 0 }, { 5, 0 }, { 7, 2 }, { 0, 0 } } },
    };
    return p;
}
} // namespace chord
