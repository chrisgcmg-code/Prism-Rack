#pragma once
#include <cmath>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cstring>

// Small, allocation-free (after prepare) DSP building blocks shared by all effects.
namespace prism::dsp
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float clamp01 (float x)   { return std::min (1.0f, std::max (0.0f, x)); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }
inline float expMap (float t, float lo, float hi) { return lo * std::pow (hi / lo, clamp01 (t)); }
inline float fastTanh (float x)
{
    // Pade approximant, accurate to ~1e-3 in [-3, 3], clamped beyond.
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
inline float softClip (float x) { return fastTanh (x); }

//==============================================================================
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    void seed (uint32_t v) { s = v ? v : 0x9E3779B9u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni()     { return (next() >> 8) * (1.0f / 16777216.0f); }   // [0,1)
    float bi()      { return uni() * 2.0f - 1.0f; }                    // [-1,1)
    bool chance (float p) { return uni() < p; }
};

//==============================================================================
// One-pole parameter / control smoother
struct Smooth
{
    float y = 0, coef = 0.999f;
    void setTime (float seconds, float sr) { coef = seconds <= 0 ? 0.0f : std::exp (-1.0f / (seconds * sr)); }
    void reset (float v) { y = v; }
    float process (float target) { y = target + coef * (y - target); return y; }
};

//==============================================================================
struct OnePole
{
    float z = 0, a = 0;
    void setLowpass (float fc, float sr) { a = std::exp (-kTwoPi * std::min (fc, sr * 0.49f) / sr); }
    void reset() { z = 0; }
    float lp (float x) { z = x + a * (z - x); return z; }
    float hp (float x) { return x - lp (x); }
};

struct DcBlocker
{
    float x1 = 0, y1 = 0, r = 0.995f;
    void prepare (float sr) { r = 1.0f - (kTwoPi * 20.0f / sr); }
    void reset() { x1 = y1 = 0; }
    float process (float x) { float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
};

//==============================================================================
// Tilt EQ: one-pole split around a pivot; tilt in [-1, 1] maps to +/- maxDb
struct TiltEq
{
    OnePole split;
    float gl = 1, gh = 1;
    void prepare (float sr, float pivot = 800.0f) { split.setLowpass (pivot, sr); split.reset(); }
    void reset() { split.reset(); }
    // Level-compensated: the boosted side rises by half as much as the cut side falls,
    // so sweeping the tilt changes tone rather than loudness.
    void setTilt (float tilt, float maxDb = 6.0f)
    {
        const float comp = -std::fabs (tilt) * maxDb * 0.5f;
        gl = dbToGain (-tilt * maxDb + comp);
        gh = dbToGain ( tilt * maxDb + comp);
    }
    float process (float x) { const float lo = split.lp (x); return lo * gl + (x - lo) * gh; }
};

//==============================================================================
// RBJ biquad (TDF-II)
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void set (float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
    void peak (float fc, float q, float db, float sr)
    {
        const float A = std::pow (10.0f, db / 40.0f), w = kTwoPi * fc / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf (float fc, float db, float sr)
    {
        const float A = std::pow (10.0f, db / 40.0f), w = kTwoPi * fc / sr, c = std::cos (w);
        const float al = std::sin (w) / 2 * std::sqrt (2.0f), sA = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + sA), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sA),
             (A + 1) + (A - 1) * c + sA, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sA);
    }
    void highShelf (float fc, float db, float sr)
    {
        const float A = std::pow (10.0f, db / 40.0f), w = kTwoPi * fc / sr, c = std::cos (w);
        const float al = std::sin (w) / 2 * std::sqrt (2.0f), sA = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
             (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
    }
    void bandpass (float fc, float q, float sr)
    {
        const float w = kTwoPi * fc / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
        set (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
};

//==============================================================================
// Cytomic / Simper TPT state-variable filter
struct Svf
{
    float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    float lpOut = 0, bpOut = 0, hpOut = 0;
    void reset() { ic1 = ic2 = 0; }
    void set (float fc, float q, float sr)
    {
        g = std::tan (kPi * std::min (fc, sr * 0.45f) / sr);
        k = 1.0f / std::max (q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void process (float x)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        lpOut = v2; bpOut = v1; hpOut = x - k * v1 - v2;
    }
};

//==============================================================================
struct Envelope
{
    float y = 0, att = 0, rel = 0;
    void set (float attackS, float releaseS, float sr)
    {
        att = std::exp (-1.0f / (std::max (attackS, 1e-5f) * sr));
        rel = std::exp (-1.0f / (std::max (releaseS, 1e-5f) * sr));
    }
    void reset() { y = 0; }
    float process (float x)
    {
        x = std::fabs (x);
        const float c = x > y ? att : rel;
        y = x + c * (y - x);
        return y;
    }
};

//==============================================================================
// Mono delay line, power-of-two buffer, fractional reads.
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void prepare (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 4) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1; w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    int capacity() const { return mask - 3; }
    void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // delay in samples measured from the most recently pushed sample (0 = that sample)
    float at (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }
    float readLinear (float d) const
    {
        d = std::min (std::max (d, 0.0f), (float) capacity());
        const int i = (int) d; const float f = d - (float) i;
        return at (i) + f * (at (i + 1) - at (i));
    }
    float readHermite (float d) const
    {
        d = std::min (std::max (d, 1.0f), (float) capacity() - 2);
        const int i = (int) d; const float f = d - (float) i;
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
};

//==============================================================================
// LFO with sine / triangle / variable-square / smooth random outputs.
struct Lfo
{
    float phase = 0, inc = 0, rndA = 0, rndB = 0;
    Rng rng;
    void setFreq (float hz, float sr) { inc = hz / sr; }
    void reset (float p = 0) { phase = p; }
    // advance one sample; returns true on wrap
    bool tick()
    {
        phase += inc;
        if (phase >= 1.0f) { phase -= 1.0f; rndA = rndB; rndB = rng.bi(); return true; }
        return false;
    }
    float sine (float offset = 0) const { return std::sin (kTwoPi * frac (phase + offset)); }
    float tri (float offset = 0) const { const float p = frac (phase + offset); return 4.0f * std::fabs (p - 0.5f) - 1.0f; }
    // smoothness 1 = sine, 0 = hard square
    float square (float hardness, float offset = 0) const
    {
        const float k = 1.0f + hardness * 30.0f;
        return fastTanh (k * sine (offset)) / fastTanh (k);
    }
    float random() const
    {
        const float t = 0.5f - 0.5f * std::cos (kPi * phase);
        return rndA + (rndB - rndA) * t;
    }
    static float frac (float x) { return x - std::floor (x); }
};

//==============================================================================
// Schroeder allpass used for diffusion
struct Allpass
{
    DelayLine d; int len = 1; float g = 0.6f;
    void prepare (int n) { d.prepare (n + 64); len = n; }
    void reset() { d.reset(); }
    float process (float x, float modSamples = 0)
    {
        const float delayed = std::fabs (modSamples) < 1.0e-6f ? d.at (len - 1) : d.readLinear ((float) len - 1 + modSamples);
        const float v = x + g * delayed;
        d.push (v);
        return delayed - g * v;
    }
};
} // namespace prism::dsp
