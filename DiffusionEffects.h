#pragma once
#include "Effect.h"

// DIFFUSION: delays and reverbs. Each effect REPLACES the buffer with its wet signal;
// the Diffusion module adds the dry signal back (so tails can ring out on bypass).
// amount = feedback / decay, knobB = Time, sec = Drift.
namespace prism
{
using namespace dsp;

//==============================================================================
class CascadeEffect : public Effect
{
    DelayLine dl[2]; OnePole lp[2], hp[2]; Smooth time; Lfo lfo; Rng rng; float sr = 48000;
public:
    void prepare (float s, int) override
    {
        sr = s; for (auto& d : dl) d.prepare ((int) (1.3f * sr));
        time.setTime (0.25f, sr); lfo.setFreq (0.45f, sr); rng.seed (4242);
        reset();
    }
    void reset() override { for (int c = 0; c < 2; ++c) { dl[c].reset(); lp[c].reset(); hp[c].reset(); } time.reset (0.3f * sr); lfo.reset(); }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext& ctx) override
    {
        const float T = ctx.timeSec (p.knobB, 0.02f, 1.2f, 1.2f);
        const float fc = std::min (8000.0f, std::max (1300.0f, 6000.0f * std::pow (0.02f / T, 0.35f)));
        for (int c = 0; c < 2; ++c) { lp[c].setLowpass (fc, sr); hp[c].setLowpass (60.0f, sr); }
        const float fb = p.amount * 1.1f;
        const float outScale = 1.0f / (1.0f + std::max (0.0f, fb - 0.8f) * 1.5f);
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            lfo.tick();
            const float d = time.process (T * sr);
            const float jitter = p.sec * (0.004f * d * lfo.sine() + 0.3f * rng.bi());
            for (int c = 0; c < 2; ++c)
            {
                const float rd = dl[c].readHermite (d * (c ? 1.015f : 1.0f) + jitter);
                const float y = lp[c].lp (hp[c].hp (rd));
                // soft-clipped feedback: above ~95 % it drones instead of running away
                dl[c].push (ch[c][i] + fb * fastTanh (y * 1.1f) / 1.1f);
                ch[c][i] = y * outScale + p.sec * 0.0015f * rng.bi();
            }
        }
    }
};

//==============================================================================
class ReelsEffect : public Effect
{
    DelayLine dl[2]; Biquad bump[2]; OnePole lp[2], hp[2]; Smooth time; Lfo wow, flutter; Rng rng;
    float sr = 48000, drop = 1, dropTarget = 1; Smooth dropS;
public:
    void prepare (float s, int) override
    {
        sr = s; for (auto& d : dl) d.prepare ((int) (2.1f * sr));
        time.setTime (0.4f, sr); wow.setFreq (0.8f, sr); flutter.setFreq (7.0f, sr); rng.seed (777);
        dropS.setTime (0.03f, sr);
        for (int c = 0; c < 2; ++c) { bump[c].peak (100.0f, 0.9f, 3.0f, sr); lp[c].setLowpass (7500.0f, sr); hp[c].setLowpass (40.0f, sr); }
        reset();
    }
    void reset() override
    {
        for (int c = 0; c < 2; ++c) { dl[c].reset(); bump[c].reset(); lp[c].reset(); hp[c].reset(); }
        time.reset (0.35f * sr); wow.reset(); flutter.reset(); dropS.reset (1); drop = dropTarget = 1;
    }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext& ctx) override
    {
        const float T = ctx.timeSec (p.knobB, 0.05f, 2.0f, 2.0f);
        const float fb = p.amount * 1.05f;
        const float outScale = 1.0f / (1.0f + std::max (0.0f, fb - 0.8f) * 1.5f);
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            wow.tick(); flutter.tick();
            const float d = time.process (T * sr);
            const float mod = p.sec * (0.0025f * sr * (0.7f * wow.sine() + 0.3f * wow.random()) + 0.00025f * sr * flutter.sine());
            if (rng.chance (p.sec * 0.6f / sr)) dropTarget = 0.25f + 0.4f * rng.uni();
            else if (rng.chance (8.0f / sr)) dropTarget = 1.0f;
            drop = dropS.process (dropTarget);
            for (int c = 0; c < 2; ++c)
            {
                const float rd = dl[c].readHermite (d + mod + (c ? 0.003f * sr : 0.0f));
                float y = lp[c].lp (bump[c].process (hp[c].hp (rd)));
                y = fastTanh (y * 1.4f) / 1.4f * drop;
                dl[c].push (ch[c][i] + fb * y);
                ch[c][i] = y * outScale;
            }
        }
    }
};

//==============================================================================
class SpaceEffect : public Effect
{
    static constexpr int N = 8;
    DelayLine line[N]; OnePole damp[N]; Smooth len[N]; Allpass ap[2][4]; DelayLine pre[2]; Lfo lfo;
    float sr = 48000, st[N] {};
    static constexpr float baseMs[N] = { 31.7f, 37.3f, 41.9f, 47.3f, 53.7f, 59.1f, 67.3f, 73.9f };
public:
    void prepare (float s, int) override
    {
        sr = s;
        for (int i = 0; i < N; ++i) { line[i].prepare ((int) (baseMs[i] * 0.001f * 2.3f * sr) + 64); len[i].setTime (0.3f, sr); }
        const float apMs[4] = { 4.6f, 6.9f, 9.6f, 12.2f };
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < 4; ++k) ap[c][k].prepare ((int) (apMs[k] * (c ? 1.07f : 1.0f) * 0.001f * sr));
            pre[c].prepare ((int) (0.1f * sr));
        }
        lfo.setFreq (0.37f, sr);
        reset();
    }
    void reset() override
    {
        for (int i = 0; i < N; ++i) { line[i].reset(); damp[i].reset(); len[i].reset (baseMs[i] * 0.001f * sr); st[i] = 0; }
        for (int c = 0; c < 2; ++c) { pre[c].reset(); for (auto& a : ap[c]) a.reset(); }
        lfo.reset();
    }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext&) override
    {
        const float t = p.knobB;                              // room -> chamber -> plate -> hall -> cloud
        const float size = 0.35f + t * 1.9f;
        const float rt60 = 0.2f * std::pow (150.0f, p.amount); // 0.2 s .. 30 s
        const float predelay = t * 0.08f * sr;
        const float dampHz = 11000.0f - t * 6000.0f;
        const float apG = 0.5f + 0.2f * t;
        const float modDepth = p.sec * 12.0f;
        const float outScale = 0.4f / std::sqrt (std::max (1.0f, rt60));   // long decays build up energy
        float g[N];
        for (int i = 0; i < N; ++i)
        {
            const float lenS = baseMs[i] * 0.001f * size * sr;
            g[i] = std::pow (10.0f, -3.0f * lenS / (rt60 * sr));
            damp[i].setLowpass (dampHz, sr);
        }
        for (int c = 0; c < 2; ++c) for (auto& a : ap[c]) a.g = apG;
        for (int s = 0; s < n; ++s)
        {
            lfo.tick();
            float in[2] = { L[s], R[s] };
            for (int c = 0; c < 2; ++c)
            {
                pre[c].push (in[c]);
                float x = pre[c].readLinear (predelay);
                for (auto& a : ap[c]) x = a.process (x);
                in[c] = x;
            }
            float o[N];
            for (int i = 0; i < N; ++i)
            {
                const float l = len[i].process (baseMs[i] * 0.001f * size * sr);
                const float m = modDepth * lfo.sine ((float) i / N);
                o[i] = damp[i].lp (line[i].readLinear (l + m)) * g[i];
            }
            // fast Walsh-Hadamard transform (normalised)
            float h[N]; std::copy (o, o + N, h);
            for (int w = 1; w < N; w <<= 1)
                for (int i = 0; i < N; i += 2 * w)
                    for (int j = i; j < i + w; ++j) { const float a = h[j], b = h[j + w]; h[j] = a + b; h[j + w] = a - b; }
            const float norm = 0.35355339f;
            for (int i = 0; i < N; ++i) line[i].push (h[i] * norm + in[i & 1] * 0.5f);
            L[s] = (o[0] + o[2] + o[4] + o[6]) * outScale;
            R[s] = (o[1] + o[3] + o[5] + o[7]) * outScale;
        }
    }
};

//==============================================================================
class CollageEffect : public Effect
{
    DelayLine rec[2]; Rng rng; float sr = 48000;
    int sliceCounter = 0;
    struct Voice { bool active = false; float pos = 0, speed = 1, speedEnd = 1, dir = 1; int len = 0, t = 0; } v;
    float baseOut[2] {};
public:
    void prepare (float s, int) override { sr = s; for (auto& r : rec) r.prepare ((int) (8.0f * sr)); rng.seed (2468); reset(); }
    void reset() override { for (auto& r : rec) r.reset(); sliceCounter = 0; v = {}; baseOut[0] = baseOut[1] = 0; }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext& ctx) override
    {
        const float sliceS = ctx.timeSec (p.knobB, 0.03f, 1.0f, 2.0f);
        const int slice = std::max (64, (int) (sliceS * sr));
        const float fb = p.amount * 0.5f;
        const float density = 0.2f + p.amount * 0.75f;
        const int fade = (int) (0.005f * sr);
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            if (++sliceCounter >= slice)
            {
                sliceCounter = 0;
                if (! v.active && rng.chance (density))
                {
                    v.active = true; v.t = 0;
                    v.len = slice * (rng.chance (0.3f) ? 2 : 1);
                    v.pos = (float) (slice * (1 + (int) (rng.uni() * 3.99f)));
                    v.dir = rng.chance (0.3f) ? -1.0f : 1.0f;
                    v.speed = rng.chance (p.sec) ? 2.0f : 1.0f;              // Drift = chance of double-speed loops
                    v.speedEnd = rng.chance (0.2f) ? v.speed * 0.5f : v.speed; // occasional pitch bend
                }
            }
            float win = 0, speed = 1;
            if (v.active)
            {
                const float prog = (float) v.t / (float) v.len;
                speed = lerp (v.speed, v.speedEnd, prog);
                win = std::min (1.0f, std::min ((float) v.t, (float) (v.len - v.t)) / (float) fade);
            }
            for (int c = 0; c < 2; ++c)
            {
                const float base = rec[c].readHermite ((float) slice - 1);
                float y = base;
                if (v.active)
                {
                    const float g = rec[c].readLinear (v.pos);
                    y = lerp (base * 0.6f, g, win);
                }
                rec[c].push (ch[c][i] + fb * y);
                ch[c][i] = y;
            }
            if (v.active)
            {
                v.pos += 1.0f - speed * v.dir;
                v.pos = std::min (std::max (v.pos, 1.0f), (float) rec[0].capacity() - 4);
                if (++v.t >= v.len) v.active = false;
            }
        }
    }
};

//==============================================================================
class ReverseEffect : public Effect
{
    DelayLine buf[2]; Lfo lfo; float sr = 48000;
    struct Head { float phase = 0; float len = 1; } head[2];
    float last[2] {};
public:
    void prepare (float s, int) override { sr = s; for (auto& b : buf) b.prepare ((int) (6.5f * sr)); lfo.setFreq (0.5f, sr); reset(); }
    void reset() override
    {
        for (auto& b : buf) b.reset();
        head[0] = { 0.0f, 0.3f * sr }; head[1] = { 0.5f, 0.3f * sr };
        last[0] = last[1] = 0; lfo.reset();
    }
    void process (float* L, float* R, int n, const EffectParams& p, const ProcessContext& ctx) override
    {
        const float chunk = ctx.timeSec (p.knobB, 0.1f, 2.0f, 2.0f) * sr;
        const float speed = std::pow (2.0f, ctx.reverseSpeedOct);
        const float fb = p.amount * 0.85f;
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            lfo.tick();
            const float drift = p.sec * 0.002f * sr * (0.5f + 0.5f * lfo.sine());
            float out[2] = { 0, 0 };
            for (auto& h : head)
            {
                const float w = std::sin (kPi * h.phase);
                const float dist = h.phase * h.len * (1.0f + speed) + drift * h.phase;
                for (int c = 0; c < 2; ++c) out[c] += buf[c].readLinear (dist) * w * w;
                h.phase += 1.0f / h.len;
                if (h.phase >= 1.0f) { h.phase -= 1.0f; h.len = chunk; } // latch a new length at each window start
            }
            for (int c = 0; c < 2; ++c)
            {
                buf[c].push (ch[c][i] + fb * last[c]);
                last[c] = out[c];
                ch[c][i] = out[c];
            }
        }
    }
};
} // namespace prism
