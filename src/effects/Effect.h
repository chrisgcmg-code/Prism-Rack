#pragma once
#include "../dsp/Core.h"

namespace prism
{
// Per-block information shared with every effect.
struct ProcessContext
{
    float sampleRate = 48000.0f;   // rate the effect runs at (oversampled for Character/Texture)
    float bpm = 120.0f;
    bool synced = false;           // Host or Tap tempo in use
    bool transportStarted = false; // true on the block where playback began
    float driftMacro = 0.0f;
    int filterMode = 0;            // Texture Filter: 0 tilt, 1 LP, 2 HP
    float reverseSpeedOct = 0.0f;  // Diffusion Reverse playback speed

    // Rate knob -> Hz: free exponential sweep, or a tempo division when synced.
    float rateHz (float knob, float lo, float hi) const
    {
        if (! synced) return dsp::expMap (knob, lo, hi);
        static const float beatsPerCycle[] = { 16, 8, 4, 2, 1, 0.5f, 1.0f / 3, 0.25f, 1.0f / 6, 0.125f };
        const int idx = std::min (9, std::max (0, (int) std::lround (knob * 9)));
        return bpm / 60.0f / beatsPerCycle[idx];
    }
    // Time knob -> seconds: free exponential sweep, or a tempo division when synced (clamped to maxS).
    float timeSec (float knob, float lo, float hi, float maxS) const
    {
        if (! synced) return dsp::expMap (knob, lo, hi);
        static const float beats[] = { 0.125f, 1.0f / 6, 0.25f, 1.0f / 3, 0.5f, 2.0f / 3, 0.75f, 1, 1.5f, 2, 3, 4 };
        const int idx = std::min (11, std::max (0, (int) std::lround (knob * 11)));
        float t = beats[idx] * 60.0f / bpm;
        while (t > maxS) t *= 0.5f;
        return t;
    }
};

struct EffectParams
{
    float amount = 0, knobB = 0, sec = 0;
};

// Base class. Character/Movement/Texture effects process in place (full signal).
// Diffusion effects replace the buffer with WET signal only; the module adds dry.
class Effect
{
public:
    virtual ~Effect() = default;
    virtual void prepare (float sampleRate, int maxBlock) = 0;
    virtual void reset() = 0;
    virtual void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext& ctx) = 0;
    // Called when Live's transport starts, so tempo-synced LFOs line up with the bar.
    virtual void syncPhase() {}
};
} // namespace prism
