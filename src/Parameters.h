#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// All parameter IDs and ranges in one place. IDs are stable strings: never rename
// them, or automation in existing Live sets will break.
namespace prism
{
enum ModuleId { Character = 0, Movement, Diffusion, Texture, NumModules };

inline const char* modulePrefix (int m)
{
    static const char* p[] = { "chr", "mov", "dif", "tex" };
    return p[m];
}

inline const char* moduleName (int m)
{
    static const char* n[] = { "CHARACTER", "MOVEMENT", "DIFFUSION", "TEXTURE" };
    return n[m];
}

inline juce::StringArray effectNames (int m)
{
    switch (m)
    {
        case Character: return { "Drive", "Sweeten", "Fuzz", "Howl", "Swell" };
        case Movement:  return { "Doubler", "Vibrato", "Phaser", "Tremolo", "Pitch" };
        case Diffusion: return { "Cascade", "Reels", "Space", "Collage", "Reverse" };
        default:        return { "Filter", "Squash", "Cassette", "Broken", "Interference" };
    }
}

// Label of the module's second big knob and its small secondary knob
inline const char* knobBName (int m)
{
    static const char* n[] = { "TILT", "RATE", "TIME", "COLOR" };
    return n[m];
}

inline const char* secName (int m)
{
    static const char* n[] = { "SENSITIVITY", "DRIFT", "DRIFT", "" };
    return n[m];
}

inline juce::String pid (int m, const char* suffix) { return juce::String (modulePrefix (m)) + "_" + suffix; }

namespace ids
{
    inline const juce::String mix       = "mix";
    inline const juce::String output    = "output";
    inline const juce::String headroom  = "headroom";
    inline const juce::String order     = "order";
    inline const juce::String bypass    = "bypass";
    inline const juce::String trails    = "trails";
    inline const juce::String driftMacro= "driftMacro";
    inline const juce::String syncMode  = "syncMode";
    inline const juce::String manualBpm = "manualBpm";
    inline const juce::String filterMode= "tex_filterMode";
    inline const juce::String revSpeed  = "rev_speed";
}

// Headroom choices: gain applied into the modules (undone after Texture).
// Low headroom = more gain (quiet sources such as a ukulele pickup);
// Very High = less gain (hot synth VSTs).
inline float headroomGainDb (int choice)
{
    static const float g[] = { 12.0f, 6.0f, 0.0f, -6.0f };
    return g[juce::jlimit (0, 3, choice)];
}

// The 24 permutations of 4 modules, lexicographic. Index 0 = C, M, D, T.
inline std::array<int, 4> orderFromIndex (int index)
{
    std::array<int, 4> perm { 0, 1, 2, 3 };
    for (int i = 0; i < juce::jlimit (0, 23, index); ++i)
        std::next_permutation (perm.begin(), perm.end());
    return perm;
}

inline int indexFromOrder (std::array<int, 4> order)
{
    std::array<int, 4> perm { 0, 1, 2, 3 };
    for (int i = 0; i < 24; ++i)
    {
        if (perm == order) return i;
        std::next_permutation (perm.begin(), perm.end());
    }
    return 0;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; });
    auto db = AudioParameterFloatAttributes().withLabel ("dB");

    // Defaults: Sweeten -> Doubler -> Space -> Filter (neutral tilt), gentle amounts.
    const int   defType[]   = { 1, 0, 2, 0 };
    const float defAmount[] = { 0.3f, 0.3f, 0.35f, 0.5f };
    const float defB[]      = { 0.5f, 0.4f, 0.4f, 0.5f };
    const float defSec[]    = { 0.5f, 0.0f, 0.1f, 0.0f };

    for (int m = 0; m < NumModules; ++m)
    {
        auto grp = std::make_unique<AudioProcessorParameterGroup> (modulePrefix (m), moduleName (m), "|");
        const String mn = String (moduleName (m)).toLowerCase().replaceSection (0, 1, String (moduleName (m)).substring (0, 1));
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { pid (m, "type"), 1 }, mn + " Effect", effectNames (m), defType[m]));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { pid (m, "amount"), 1 }, mn + " Amount", NormalisableRange<float> (0, 1), defAmount[m], pct));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { pid (m, "knobB"), 1 }, mn + " " + String (knobBName (m)).toLowerCase(), NormalisableRange<float> (0, 1), defB[m], pct));
        if (m != Texture)
            grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { pid (m, "sec"), 1 }, mn + " " + String (secName (m)).toLowerCase(), NormalisableRange<float> (0, 1), defSec[m], pct));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { pid (m, "vol"), 1 }, mn + " Effect Vol", NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f, db));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { pid (m, "bypass"), 1 }, mn + " Bypass", false));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { pid (m, "alwaysOn"), 1 }, mn + " Always On", false));
        layout.add (std::move (grp));
    }

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::mix, 1 }, "Mix", NormalisableRange<float> (0, 1), 1.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::output, 1 }, "Output Level", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, db));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::headroom, 1 }, "Headroom", StringArray { "Low", "Medium", "High", "Very High" }, 2));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::order, 1 }, "Module Order", 0, 23, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::trails, 1 }, "Trails", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::driftMacro, 1 }, "Drift Macro", NormalisableRange<float> (0, 1), 0.0f, pct));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::syncMode, 1 }, "Tempo Source", StringArray { "Free", "Host", "Tap" }, 1));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::manualBpm, 1 }, "Tap BPM", NormalisableRange<float> (40.0f, 240.0f, 0.1f), 120.0f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::filterMode, 1 }, "Filter Mode", StringArray { "Tilt", "Low-pass", "High-pass" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::revSpeed, 1 }, "Reverse Speed",
        NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " oct"; })));
    return layout;
}
} // namespace prism
