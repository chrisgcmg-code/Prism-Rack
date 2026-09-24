#pragma once
#include "Effect.h"

// MOVEMENT: modulation and pitch. amount = depth/intensity, knobB = Rate, sec = Drift.
namespace prism
{
using namespace dsp;

//==============================================================================
class DoublerEffect : public Effect
{
public:
    void syncPhase() override { lfo.reset(); }
private:
    DelayLine dl[2]; Lfo lfo; Smooth base; Rng rng; float sr = 48000, dip = 0, dipVel = 0;
public:
    void prepare (float s, int) override
    {
        sr = s;
        for (auto& d : dl) d.prepare ((int) (0.2f * sr));
        base.setTime (0.05f, sr);
        rng.seed (1234);
        reset();
    }
    void reset() override { for (auto& d : dl) d.reset(); lfo.reset(); base.reset (0.01f * sr); dip = dipVel = 0; }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext& ctx) override
    {
        lfo.setFreq (ctx.rateHz (p.knobB, 0.08f, 4.0f), sr);
        const float baseTarget = (0.008f + p.amount * p.amount * 0.112f) * sr;
        const float modDepth = 0.0004f * sr;
        const float dipChance = p.sec * 1.5f / sr; // ~1.5 dips per second at full Drift
        for (int i = 0; i < n; ++i)
        {
            lfo.tick();
            const float b = base.process (baseTarget);
            if (dip <= 0 && rng.chance (dipChance)) dipVel = (0.5f + rng.uni()) * 0.02f * p.sec; // ~2 % pitch dip
            dip += dipVel; if (dip > 0.003f * sr * p.sec) dipVel = -std::fabs (dipVel);
            if (dip < 0) { dip = 0; dipVel = 0; }
            const float wL = dl[0].readHermite (b + modDepth * lfo.sine() + dip);
            const float wR = dl[1].readHermite (b * 1.27f + modDepth * lfo.sine (0.25f) + dip * 0.7f);
            dl[0].push (L[i]); dl[1].push (R[i]);
            L[i] = (L[i] + 0.85f * wL) * 0.72f;
            R[i] = (R[i] + 0.85f * wR) * 0.72f;
        }
    }
};

//==============================================================================
class VibratoEffect : public Effect
{
public:
    void syncPhase() override { lfo.reset(); }
private:
    DelayLine dl[2]; Lfo lfo; Smooth depthS; float sr = 48000;
public:
    void prepare (float s, int) override { sr = s; for (auto& d : dl) d.prepare ((int) (0.03f * sr)); depthS.setTime (0.03f, sr); lfo.rng.seed (77); reset(); }
    void reset() override { for (auto& d : dl) d.reset(); lfo.reset(); depthS.reset (0); }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext& ctx) override
    {
        lfo.setFreq (ctx.rateHz (p.knobB, 0.2f, 12.0f), sr);
        const float centre = 0.005f * sr;
        for (int i = 0; i < n; ++i)
        {
            lfo.tick();
            const float depth = depthS.process (p.amount * 0.004f * sr);
            const float v = lerp (lfo.sine(), lfo.random() * 1.2f, p.sec);
            dl[0].push (L[i]); dl[1].push (R[i]);
            L[i] = dl[0].readHermite (centre + depth * v);
            R[i] = dl[1].readHermite (centre + depth * v);
        }
    }
};

//==============================================================================
class PhaserEffect : public Effect
{
public:
    void syncPhase() override { lfo.reset(); }
private:
    static constexpr int kMaxStages = 12;
    float z[2][kMaxStages] {}; float fb[2] {}; Lfo lfo, wobble; float sr = 48000, coef[2] {};
public:
    void prepare (float s, int) override { sr = s; lfo.rng.seed (99); wobble.rng.seed (5); reset(); }
    void reset() override { std::memset (z, 0, sizeof (z)); fb[0] = fb[1] = 0; lfo.reset(); wobble.reset(); }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext& ctx) override
    {
        const int stages = 2 * (1 + (int) std::lround (p.amount * 5.0f));
        const float feedback = 0.25f + p.amount * 0.45f;
        const float rate = ctx.rateHz (p.knobB, 0.05f, 8.0f);
        wobble.setFreq (0.3f, sr);
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            wobble.tick();
            lfo.setFreq (rate * (1.0f + 0.4f * p.sec * wobble.random()), sr);
            lfo.tick();
            if ((i & 3) == 0)
                for (int c = 0; c < 2; ++c)
                {
                    const float v = lerp (lfo.sine (c * 0.25f), lfo.random(), p.sec * 0.6f);
                    const float f = 160.0f * std::pow (2.0f, (v * 0.5f + 0.5f) * 4.3f);
                    const float t = std::tan (kPi * f / sr);
                    coef[c] = (t - 1.0f) / (t + 1.0f);
                }
            for (int c = 0; c < 2; ++c)
            {
                const float x = ch[c][i];
                float y = x + fb[c] * feedback;
                for (int k = 0; k < stages; ++k)
                {
                    const float out = coef[c] * y + z[c][k];
                    z[c][k] = y - coef[c] * out;
                    y = out;
                }
                fb[c] = fastTanh (y);
                ch[c][i] = 0.5f * (x + y);
            }
        }
    }
};

//==============================================================================
class TremoloEffect : public Effect
{
public:
    void syncPhase() override { lfo.reset(); }
private:
    Lfo lfo, wobble; float sr = 48000; Smooth gainS;
public:
    void prepare (float s, int) override { sr = s; wobble.rng.seed (31); gainS.setTime (0.0015f, sr); reset(); }
    void reset() override { lfo.reset(); wobble.reset(); gainS.reset (1); }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext& ctx) override
    {
        const float depth = std::min (1.0f, p.amount * 1.6f);
        const float hard = clamp01 ((p.amount - 0.4f) / 0.6f);
        const float rate = ctx.rateHz (p.knobB, 0.5f, 16.0f);
        wobble.setFreq (0.7f, sr);
        for (int i = 0; i < n; ++i)
        {
            wobble.tick();
            const float w = wobble.random();
            lfo.setFreq (rate * (1.0f + 0.35f * p.sec * w), sr);
            lfo.tick();
            const float v = lfo.square (hard);
            const float d = depth * (1.0f - 0.4f * p.sec * (0.5f + 0.5f * w));
            const float g = gainS.process (1.0f - d * (0.5f - 0.5f * v));
            L[i] *= g; R[i] *= g;
        }
    }
};

//==============================================================================
class PitchEffect : public Effect
{
    DelayLine dl[2]; float phase = 0, sr = 48000, grain = 2880; Smooth ratioS;
    float held[2] {}; int holdCount = 0;
public:
    void prepare (float s, int) override { sr = s; grain = 0.06f * sr; for (auto& d : dl) d.prepare ((int) (0.15f * sr)); reset(); }
    void reset() override { for (auto& d : dl) d.reset(); phase = 0; ratioS.reset (1); held[0] = held[1] = 0; holdCount = 0; }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext&) override
    {
        float semi = (p.amount * 2.0f - 1.0f) * 12.0f;
        if (std::fabs (semi - std::round (semi)) < 0.2f) semi = std::round (semi);
        const float target = std::pow (2.0f, semi / 12.0f);
        ratioS.setTime (0.005f + p.knobB * p.knobB * 2.0f, sr); // Rate knob = glide time
        const int hold = 1 + (int) (p.sec * 6.0f);
        const float levels = std::pow (2.0f, 15.0f - p.sec * 11.0f);
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            const float ratio = ratioS.process (target);
            phase += (1.0f - ratio) / grain;
            phase -= std::floor (phase);
            const float p2 = Lfo::frac (phase + 0.5f);
            const float w1 = std::sin (kPi * phase), w2 = std::sin (kPi * p2);
            const bool sample = (++holdCount >= hold);
            if (sample) holdCount = 0;
            for (int c = 0; c < 2; ++c)
            {
                dl[c].push (ch[c][i]);
                float y = dl[c].readLinear (2 + phase * grain) * w1 * w1 + dl[c].readLinear (2 + p2 * grain) * w2 * w2;
                if (p.sec > 0.01f)
                {
                    if (sample) held[c] = std::round (y * levels) / levels;
                    y = held[c];
                }
                ch[c][i] = y;
            }
        }
    }
};
} // namespace prism
