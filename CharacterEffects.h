#pragma once
#include "Effect.h"

// CHARACTER: drives, preamp, fuzz, resonant fuzz, swells.
// Runs at 4x oversampling. amount = intensity, knobB = Tilt (0.5 = flat), sec = Sensitivity.
namespace prism
{
using namespace dsp;

inline float makeupFor (float gain) { return 0.25f / fastTanh (std::min (3.0f, 0.25f * gain)); }

//==============================================================================
class DriveEffect : public Effect
{
    TiltEq tilt[2]; OnePole post[2]; DcBlocker dc[2]; float sr = 48000;
public:
    void prepare (float s, int) override
    {
        sr = s;
        for (int c = 0; c < 2; ++c) { tilt[c].prepare (sr, 800); post[c].setLowpass (9000, sr); dc[c].prepare (sr); }
        reset();
    }
    void reset() override { for (int c = 0; c < 2; ++c) { tilt[c].reset(); post[c].reset(); dc[c].reset(); } }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext&) override
    {
        const float g = dbToGain (p.amount * 36.0f) * dbToGain ((p.sec - 0.5f) * 18.0f);
        const float bias = 0.18f, off = fastTanh (bias), mk = makeupFor (g);
        float* ch[2] = { L, R };
        for (int c = 0; c < 2; ++c)
        {
            tilt[c].setTilt ((p.knobB - 0.5f) * 2.0f, 6.0f);
            for (int i = 0; i < n; ++i)
            {
                float x = tilt[c].process (ch[c][i]) * g;
                // asymmetric triode-like curve: positive side clips softer than negative
                float y = x >= 0 ? fastTanh (x + bias) - off : (fastTanh (1.25f * x + bias) - off) * 0.9f;
                ch[c][i] = post[c].lp (dc[c].process (y)) * mk;
            }
        }
    }
};

//==============================================================================
class SweetenEffect : public Effect
{
    Biquad lowShelf[2], presence[2], air[2]; TiltEq tilt[2]; float env = 0, envA = 0, envR = 0, sr = 48000;
    float lastA = -1, lastB = -1;
public:
    void prepare (float s, int) override
    {
        sr = s; envA = std::exp (-1.0f / (0.010f * sr)); envR = std::exp (-1.0f / (0.150f * sr));
        for (auto& t : tilt) t.prepare (sr, 800);
        reset();
    }
    void reset() override
    {
        for (int c = 0; c < 2; ++c) { lowShelf[c].reset(); presence[c].reset(); air[c].reset(); tilt[c].reset(); }
        env = 0; lastA = lastB = -1;
    }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext&) override
    {
        if (std::fabs (p.amount - lastA) > 1e-4f || std::fabs (p.knobB - lastB) > 1e-4f)
        {
            for (int c = 0; c < 2; ++c)
            {
                lowShelf[c].lowShelf (120.0f, 3.0f * p.amount, sr);
                presence[c].peak (3000.0f, 0.8f, 4.0f * p.amount, sr);
                air[c].highShelf (10000.0f, 2.0f * p.amount, sr);
                tilt[c].setTilt ((p.knobB - 0.5f) * 2.0f, 5.0f);
            }
            lastA = p.amount; lastB = p.knobB;
        }
        const float threshDb = -30.0f + p.sec * 20.0f;       // Sensitivity: -30..-10 dB
        const float ratio = 1.0f + p.amount;                 // up to 2:1
        const float satDrive = 1.0f + p.amount * 1.5f;
        for (int i = 0; i < n; ++i)
        {
            float l = tilt[0].process (air[0].process (presence[0].process (lowShelf[0].process (L[i]))));
            float r = tilt[1].process (air[1].process (presence[1].process (lowShelf[1].process (R[i]))));
            const float lvl = std::max (std::fabs (l), std::fabs (r));
            env = lvl + (lvl > env ? envA : envR) * (env - lvl);
            const float over = gainToDb (env) - threshDb;
            // soft knee of 6 dB
            float grDb = 0;
            if (over > 3.0f) grDb = over * (1.0f - 1.0f / ratio);
            else if (over > -3.0f) grDb = (1.0f - 1.0f / ratio) * (over + 3.0f) * (over + 3.0f) / 12.0f;
            const float g = dbToGain (-grDb + grDb * 0.5f); // half the reduction back as makeup
            l *= g; r *= g;
            L[i] = fastTanh (l * satDrive) / satDrive * (1.0f + 0.2f * p.amount);
            R[i] = fastTanh (r * satDrive) / satDrive * (1.0f + 0.2f * p.amount);
        }
    }
};

//==============================================================================
class FuzzEffect : public Effect
{
    OnePole inHp[2], post[2]; DcBlocker dc[2]; Envelope env; float gate = 1, gateCoef = 0, sr = 48000;
public:
    void prepare (float s, int) override
    {
        sr = s; env.set (0.002f, 0.050f, sr); gateCoef = std::exp (-1.0f / (0.004f * sr));
        for (int c = 0; c < 2; ++c) { inHp[c].setLowpass (80, sr); post[c].setLowpass (5500, sr); dc[c].prepare (sr); }
        reset();
    }
    void reset() override { for (int c = 0; c < 2; ++c) { inHp[c].reset(); post[c].reset(); dc[c].reset(); } env.reset(); gate = 1; }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext&) override
    {
        const float g = dbToGain (18.0f + p.amount * 38.0f);
        const float bias = (p.knobB - 0.5f) * 0.9f;          // Tilt: sputtery (-) to smooth (+)
        const float gateTh = dbToGain (-70.0f + p.sec * 40.0f);
        const float mk = 0.32f;
        for (int i = 0; i < n; ++i)
        {
            const float e = env.process (0.5f * (L[i] + R[i]));
            const float target = e > gateTh ? 1.0f : 0.0f;
            gate = target + gateCoef * (gate - target);
            float* ch[2] = { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                float x = inHp[c].hp (*ch[c]) * g + bias;
                float y = x > 0 ? 1.0f - std::exp (-x) : -(1.0f - std::exp (1.6f * x)) * 0.75f;
                *ch[c] = post[c].lp (dc[c].process (y)) * mk * gate;
            }
        }
    }
};

//==============================================================================
class HowlEffect : public Effect
{
    Svf svf[2]; DcBlocker dc[2]; Envelope env; float sr = 48000;
public:
    void prepare (float s, int) override { sr = s; env.set (0.003f, 0.25f, sr); for (auto& d : dc) d.prepare (sr); reset(); }
    void reset() override { for (int c = 0; c < 2; ++c) { svf[c].reset(); dc[c].reset(); } env.reset(); }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext&) override
    {
        const float g = dbToGain (12.0f + p.amount * 30.0f);
        const float q = 0.8f + p.amount * p.amount * 18.0f;
        const float base = expMap (p.knobB, 200.0f, 6000.0f);
        const float depth = p.sec * 6.0f;
        for (int i = 0; i < n; i += 8)
        {
            const int m = std::min (8, n - i);
            float e = 0;
            for (int k = 0; k < m; ++k) e = env.process (0.5f * (L[i + k] + R[i + k]));
            const float fc = std::min (base * (1.0f + depth * std::min (1.0f, e * 4.0f)), 14000.0f);
            for (int c = 0; c < 2; ++c)
            {
                svf[c].set (fc, q, sr);
                float* x = (c == 0 ? L : R) + i;
                for (int k = 0; k < m; ++k)
                {
                    const float fz = fastTanh (x[k] * g);
                    svf[c].process (fz);
                    // resonance is tamed by a saturator so high Q sings instead of exploding
                    x[k] = fastTanh (dc[c].process (svf[c].lpOut) * 1.2f) * 0.35f;
                }
            }
        }
    }
};

//==============================================================================
class SwellEffect : public Effect
{
    Envelope fast, slow; TiltEq tilt[2];
    float gain = 1, sr = 48000; int stage = 2; // 0 falling, 1 rising, 2 idle-open
    int refractory = 0;
public:
    void prepare (float s, int) override
    {
        sr = s; fast.set (0.001f, 0.020f, sr); slow.set (0.030f, 0.300f, sr);
        for (auto& t : tilt) t.prepare (sr, 800);
        reset();
    }
    void reset() override { fast.reset(); slow.reset(); gain = 0; stage = 1; refractory = 0; for (auto& t : tilt) t.reset(); }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext&) override
    {
        const float swellTime = expMap (p.amount, 0.02f, 2.0f);
        const float riseInc = 1.0f / (swellTime * sr);
        const float fallInc = 1.0f / (0.004f * sr);
        const float gateTh = dbToGain (-60.0f + (1.0f - p.sec) * 36.0f); // higher Sensitivity = lower threshold
        for (auto& t : tilt) t.setTilt ((p.knobB - 0.5f) * 2.0f, 6.0f);
        for (int i = 0; i < n; ++i)
        {
            const float x = 0.5f * (std::fabs (L[i]) + std::fabs (R[i]));
            const float f = fast.process (x), s = slow.process (x);
            if (refractory > 0) --refractory;
            // new note: fast envelope jumps above the slow one
            if (refractory == 0 && f > gateTh && f > s * 2.0f)
            {
                stage = 0; refractory = (int) (0.08f * sr);
            }
            if (stage == 0) { gain -= fallInc; if (gain <= 0) { gain = 0; stage = 1; } }
            else if (stage == 1) { gain += riseInc * (1.0f - gain * 0.5f); if (gain >= 1) { gain = 1; stage = 2; } }
            const float shaped = gain * gain * (3 - 2 * gain); // smoothstep for a musical fade
            L[i] = tilt[0].process (L[i]) * shaped;
            R[i] = tilt[1].process (R[i]) * shaped;
        }
    }
};
} // namespace prism
