#pragma once
#include "Effect.h"

// TEXTURE: filtering, dynamics and degradation over the whole chain.
// Runs at 2x oversampling. amount = main knob, knobB = Color (expanded view).
namespace prism
{
using namespace dsp;

//==============================================================================
class FilterEffect : public Effect
{
    TiltEq tilt[2]; Svf svf[2]; float sr = 48000;
public:
    void prepare (float s, int) override { sr = s; for (auto& t : tilt) t.prepare (sr, 700); reset(); }
    void reset() override { for (int c = 0; c < 2; ++c) { tilt[c].reset(); svf[c].reset(); } }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext& ctx) override
    {
        float* ch[2] = { L, R };
        if (ctx.filterMode == 0)
        {
            for (int c = 0; c < 2; ++c)
            {
                tilt[c].setTilt ((p.amount - 0.5f) * 2.0f, 12.0f);
                for (int i = 0; i < n; ++i) ch[c][i] = tilt[c].process (ch[c][i]);
            }
            return;
        }
        const float fc = expMap (p.amount, 20.0f, 20000.0f);
        const float q = 0.6f + p.knobB * 8.0f;
        const float comp = 1.0f / (1.0f + (q - 0.6f) * 0.2f);   // keep resonant peaks in check
        for (int c = 0; c < 2; ++c)
        {
            svf[c].set (fc, q, sr);
            for (int i = 0; i < n; ++i)
            {
                svf[c].process (ch[c][i]);
                ch[c][i] = (ctx.filterMode == 1 ? svf[c].lpOut : svf[c].hpOut) * comp;
            }
        }
    }
};

//==============================================================================
class SquashEffect : public Effect
{
    float env = 0, sr = 48000;
public:
    void prepare (float s, int) override { sr = s; reset(); }
    void reset() override { env = 0; }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext&) override
    {
        const float threshDb = -p.amount * 40.0f;
        const float ratio = 3.0f + p.amount * 9.0f;
        const float att = std::exp (-1.0f / (lerp (0.0005f, 0.02f, p.knobB) * sr));
        const float relFast = std::exp (-1.0f / (lerp (0.04f, 0.25f, p.knobB) * sr));
        const float relSlow = std::exp (-1.0f / (lerp (0.25f, 0.8f, p.knobB) * sr));
        const float makeup = dbToGain (-threshDb * (1.0f - 1.0f / ratio) * 0.55f);
        const float drive = clamp01 ((p.amount - 0.75f) * 4.0f);
        const float k = 1.0f + drive * 5.0f, norm = 0.5f / fastTanh (0.5f * k);
        for (int i = 0; i < n; ++i)
        {
            const float lvl = std::max (std::fabs (L[i]), std::fabs (R[i]));
            // program-dependent release: short transients recover fast, sustained levels slowly
            const float rel = lvl > env * 0.5f ? relSlow : relFast;
            env = lvl + (lvl > env ? att : rel) * (env - lvl);
            const float over = gainToDb (env) - threshDb;
            float gr = 0;
            if (over > 4.0f) gr = over * (1.0f - 1.0f / ratio);
            else if (over > -4.0f) gr = (1.0f - 1.0f / ratio) * (over + 4.0f) * (over + 4.0f) / 16.0f;
            const float g = dbToGain (-gr) * makeup;
            float l = L[i] * g, r = R[i] * g;
            if (drive > 0) { l = lerp (l, fastTanh (l * k) * norm, drive); r = lerp (r, fastTanh (r * k) * norm, drive); }
            L[i] = l; R[i] = r;
        }
    }
};

//==============================================================================
class CassetteEffect : public Effect
{
    DelayLine dl[2]; Lfo wow, flutter; OnePole lp[2], hissLp; Biquad bump[2]; Rng rng; Smooth dropS;
    float sr = 48000, dropTarget = 1, lastA = -1, lastB = -1;
public:
    void prepare (float s, int) override
    {
        sr = s; for (auto& d : dl) d.prepare ((int) (0.03f * sr));
        wow.setFreq (0.5f, sr); flutter.setFreq (9.0f, sr); rng.seed (1357); dropS.setTime (0.04f, sr);
        hissLp.setLowpass (9000.0f, sr);
        reset();
    }
    void reset() override
    {
        for (int c = 0; c < 2; ++c) { dl[c].reset(); lp[c].reset(); bump[c].reset(); }
        hissLp.reset(); wow.reset(); flutter.reset(); dropS.reset (1); dropTarget = 1; lastA = lastB = -1;
    }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext&) override
    {
        const float a = p.amount, age = p.knobB;
        if (std::fabs (a - lastA) > 1e-4f || std::fabs (age - lastB) > 1e-4f)
        {
            for (int c = 0; c < 2; ++c)
            {
                lp[c].setLowpass (16000.0f - (a * 0.5f + age * 0.5f) * 12000.0f, sr);
                bump[c].peak (90.0f, 1.0f, 2.0f * a, sr);
            }
            lastA = a; lastB = age;
        }
        const float wowDepth = a * (0.3f + age) * 0.0015f * sr;
        const float flutDepth = a * (1.0f + age) * 0.00015f * sr;
        const float hiss = a * (0.3f + 0.7f * age) * 0.004f;
        const float drive = 1.0f + a * 2.0f;
        const float dropChance = a * age * 1.5f / sr;
        const float base = 0.008f * sr;
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            wow.tick(); flutter.tick();
            if (rng.chance (dropChance)) dropTarget = 0.15f + 0.4f * rng.uni();
            else if (rng.chance (6.0f / sr)) dropTarget = 1.0f;
            const float drop = dropS.process (dropTarget);
            const float m = wowDepth * (0.6f * wow.sine() + 0.4f * wow.random()) + flutDepth * flutter.sine();
            const float h = hissLp.lp (rng.bi()) * hiss;
            for (int c = 0; c < 2; ++c)
            {
                dl[c].push (ch[c][i]);
                float y = dl[c].readHermite (base + m * (c ? 0.97f : 1.0f));
                y = bump[c].process (y);
                y = fastTanh (y * drive) / drive * (1.0f + 0.3f * a);
                ch[c][i] = lp[c].lp (y * drop + h);
            }
        }
    }
};

//==============================================================================
class BrokenEffect : public Effect
{
    DelayLine dl[2]; Rng rng; Smooth speedS; Lfo am;
    float sr = 48000, d = 0, speedTarget = 1;
    int slowLeft = 0;                                  // samples left in a slowdown event
    int stutLen = 0, stutT = 0, stutReps = 0;          // stutter event
public:
    void prepare (float s, int) override { sr = s; for (auto& x : dl) x.prepare ((int) (1.0f * sr)); speedS.setTime (0.06f, sr); am.setFreq (13.0f, sr); rng.seed (8642); reset(); }
    void reset() override { for (auto& x : dl) x.reset(); d = 0.01f * sr; speedS.reset (1); speedTarget = 1; slowLeft = stutReps = 0; am.reset(); }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext&) override
    {
        const float a = p.amount;
        const float eventsPerSec = a * (0.4f + 2.6f * (1.0f - p.knobB));  // Color: rate <-> depth
        const float depth = 0.3f + 0.7f * p.knobB;
        const float baseD = 0.01f * sr;
        const int fade = (int) (0.002f * sr);
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            am.tick();
            if (slowLeft == 0 && stutReps == 0 && rng.chance (eventsPerSec / sr))
            {
                if (rng.chance (0.65f))
                {
                    slowLeft = (int) ((0.1f + 0.4f * rng.uni()) * sr);
                    speedTarget = 1.0f - depth * 0.5f * (0.5f + rng.uni());
                }
                else
                {
                    stutLen = (int) ((0.03f + 0.05f * rng.uni()) * sr);
                    stutReps = 2 + (int) (rng.uni() * 3); stutT = 0;
                }
            }
            if (slowLeft > 0) { if (--slowLeft == 0) speedTarget = 1; }
            else speedTarget = std::min (1.5f, 1.0f + (d - baseD) / (0.3f * sr)); // speed up to catch back up
            const float speed = speedS.process (speedTarget);
            d = std::min (std::max (d + (1.0f - speed), baseD * 0.5f), 0.9f * sr);

            float readD = d + a * 0.0004f * sr * am.sine();
            float win = 1;
            if (stutReps > 0)
            {
                const int within = stutT % stutLen;
                readD += (float) (stutLen + stutT - within);
                win = std::min (1.0f, (float) std::min (within, stutLen - within) / (float) fade);
                if (++stutT >= stutLen * stutReps) stutReps = 0;
            }
            const float amp = (1.0f - a * 0.15f * (0.5f + 0.5f * am.sine (0.3f))) * win;
            for (int c = 0; c < 2; ++c)
            {
                dl[c].push (ch[c][i]);
                ch[c][i] = dl[c].readHermite (readD) * amp;
            }
        }
    }
};

//==============================================================================
class InterferenceEffect : public Effect
{
    enum Type { None, Crush, Gap, Repeat, Static, Whistle };
    DelayLine dl[2]; Rng rng; Biquad noiseBp; Smooth gainS; float sr = 48000;
    Type type = None; int left = 0, holdN = 1, holdC = 0, repLen = 1, t = 0;
    float held[2] {}, whPhase = 0, whFreq = 1000, whSweep = 0, crackle = 0;
public:
    void prepare (float s, int) override { sr = s; for (auto& x : dl) x.prepare ((int) (0.2f * sr)); rng.seed (97531); gainS.setTime (0.002f, sr); reset(); }
    void reset() override { for (auto& x : dl) x.reset(); type = None; left = 0; gainS.reset (1); noiseBp.reset(); whPhase = 0; }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext&) override
    {
        const float a = p.amount, analog = p.knobB;
        const float eventsPerSec = a * 3.0f;
        const float bed = a * 0.003f * analog;
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            if (type == None && rng.chance (eventsPerSec / sr))
            {
                t = 0;
                if (rng.uni() >= analog)
                {
                    const float r = rng.uni();
                    if (r < 0.4f)      { type = Crush;  left = (int) ((0.05f + 0.25f * rng.uni()) * sr); holdN = 4 + (int) (rng.uni() * 26); }
                    else if (r < 0.7f) { type = Gap;    left = (int) ((0.02f + 0.06f * rng.uni()) * sr); }
                    else               { type = Repeat; repLen = (int) (0.02f * sr); left = repLen * (2 + (int) (rng.uni() * 5)); }
                }
                else
                {
                    if (rng.chance (0.6f)) { type = Static; left = (int) ((0.1f + 0.5f * rng.uni()) * sr); noiseBp.bandpass (1000.0f + 2000.0f * rng.uni(), 1.2f, sr); }
                    else { type = Whistle; left = (int) ((0.3f + 1.2f * rng.uni()) * sr); whFreq = 1000.0f + 3000.0f * rng.uni(); whSweep = (rng.bi() * 800.0f) / (float) left; }
                }
            }
            const float g = gainS.process (type == Gap ? 0.0f : 1.0f);
            float add = rng.bi() * bed;
            float readD = 0;
            if (type == Static)
            {
                if (rng.chance (40.0f / sr)) crackle = rng.bi();
                crackle *= 0.995f;
                add += (noiseBp.process (rng.bi()) * 0.6f + crackle * 0.5f) * a * 0.12f;
            }
            else if (type == Whistle)
            {
                whFreq += whSweep; whPhase += whFreq / sr; whPhase -= std::floor (whPhase);
                const float env = std::min (1.0f, std::min ((float) t, (float) left) / (0.05f * sr));
                add += std::sin (kTwoPi * whPhase) * a * 0.03f * env;
            }
            else if (type == Repeat) readD = (float) (repLen + t - t % repLen);
            const bool sample = type != Crush || (++holdC >= holdN);
            if (sample) holdC = 0;
            for (int c = 0; c < 2; ++c)
            {
                dl[c].push (ch[c][i]);
                float y = type == Repeat ? dl[c].readLinear (readD) : ch[c][i];
                if (type == Crush) { if (sample) held[c] = std::round (y * 64.0f) / 64.0f; y = held[c]; }
                ch[c][i] = y * g + add;
            }
            if (type != None) { ++t; if (--left <= 0) type = None; }
        }
    }
};
} // namespace prism
