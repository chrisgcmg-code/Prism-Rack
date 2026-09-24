#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "effects/CharacterEffects.h"
#include "effects/MovementEffects.h"
#include "effects/DiffusionEffects.h"
#include "effects/TextureEffects.h"

namespace prism
{
struct ModuleInputs
{
    int type = 0;
    float amount = 0, knobB = 0, sec = 0, volDb = 0;
    bool active = true, trails = true;
};

// One slot of the chain: five effects, selection crossfade, bypass crossfade with a
// latency-matched dry path, optional oversampling, and Effect Vol.
class Module
{
public:
    explicit Module (int moduleId) : id (moduleId)
    {
        switch (id)
        {
            case Character:
                fx[0] = std::make_unique<DriveEffect>();   fx[1] = std::make_unique<SweetenEffect>();
                fx[2] = std::make_unique<FuzzEffect>();    fx[3] = std::make_unique<HowlEffect>();
                fx[4] = std::make_unique<SwellEffect>();   osStages = 2; break;          // 4x
            case Movement:
                fx[0] = std::make_unique<DoublerEffect>(); fx[1] = std::make_unique<VibratoEffect>();
                fx[2] = std::make_unique<PhaserEffect>();  fx[3] = std::make_unique<TremoloEffect>();
                fx[4] = std::make_unique<PitchEffect>();   osStages = 0; break;
            case Diffusion:
                fx[0] = std::make_unique<CascadeEffect>(); fx[1] = std::make_unique<ReelsEffect>();
                fx[2] = std::make_unique<SpaceEffect>();   fx[3] = std::make_unique<CollageEffect>();
                fx[4] = std::make_unique<ReverseEffect>(); osStages = 0; break;
            default:
                fx[0] = std::make_unique<FilterEffect>();  fx[1] = std::make_unique<SquashEffect>();
                fx[2] = std::make_unique<CassetteEffect>();fx[3] = std::make_unique<BrokenEffect>();
                fx[4] = std::make_unique<InterferenceEffect>(); osStages = 1; break;        // 2x
        }
        if (osStages > 0)
            os = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) osStages,
                    juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    }

    void prepare (double sampleRate, int maxBlock)
    {
        hostRate = (float) sampleRate;
        factor = 1 << osStages;
        fxRate = hostRate * (float) factor;
        if (os) { os->initProcessing ((size_t) maxBlock); os->reset(); latency = (int) std::lround (os->getLatencyInSamples()); }
        else latency = 0;
        for (auto& e : fx) e->prepare (fxRate, maxBlock * factor);
        proc.setSize (2, maxBlock);
        prevBuf.setSize (2, maxBlock * factor);
        for (auto& d : dryDelay) { d.prepare (latency + 8); d.reset(); }
        for (auto* s : { &amountS, &knobBS, &secS })
            s->reset (sampleRate, 0.05);
        volS.reset (sampleRate, 0.03);
        activeS.reset (sampleRate, 0.012);
        inputS.reset (sampleRate, 0.012);
        firstBlock = true;
    }

    // Clear all audio state without reallocating (safe on the audio thread)
    void resetState()
    {
        for (auto& e : fx) e->reset();
        if (os) os->reset();
        for (auto& d : dryDelay) d.reset();
        prev = -1; firstBlock = true;
    }

    int getLatency() const { return latency; }
    bool isDiffusion() const { return id == Diffusion; }

    void process (juce::AudioBuffer<float>& buf, int n, const ModuleInputs& in, ProcessContext ctx)
    {
        const int type = juce::jlimit (0, 4, in.type);
        if (firstBlock)
        {
            amountS.setCurrentAndTargetValue (in.amount); knobBS.setCurrentAndTargetValue (in.knobB);
            secS.setCurrentAndTargetValue (in.sec); volS.setCurrentAndTargetValue (dsp::dbToGain (in.volDb));
            activeS.setCurrentAndTargetValue (in.active ? 1.0f : 0.0f);
            inputS.setCurrentAndTargetValue (in.active ? 1.0f : 0.0f);
            cur = type; fx[(size_t) cur]->reset(); idle = ! in.active; firstBlock = false;
        }
        if (type != cur)
        {
            prev = cur; cur = type; fx[(size_t) cur]->reset();
            fadeLen = (int) (0.03f * fxRate); fadePos = 0;
        }
        amountS.setTargetValue (in.amount); knobBS.setTargetValue (in.knobB); secS.setTargetValue (in.sec);
        volS.setTargetValue (dsp::dbToGain (in.volDb));

        const bool diff = isDiffusion();
        const bool tailMode = diff && in.trails;
        // With trails, Diffusion always outputs its wet; bypass only mutes what goes IN.
        activeS.setTargetValue ((in.active || tailMode) ? 1.0f : 0.0f);
        inputS.setTargetValue (in.active ? 1.0f : 0.0f);

        // latency-matched dry path
        float* ch[2] = { buf.getWritePointer (0), buf.getWritePointer (1) };
        for (int c = 0; c < 2; ++c)
        {
            float* p = proc.getWritePointer (c);
            for (int i = 0; i < n; ++i) { dryDelay[c].push (ch[c][i]); p[i] = latency > 0 ? dryDelay[c].at (latency) : ch[c][i]; }
        }
        // p now holds delayed dry; swap roles: effect input = buf (undelayed), dry reference = proc
        const bool wantRun = in.active || activeS.isSmoothing() || activeS.getCurrentValue() > 0.0f;
        const bool running = wantRun && ! (tailMode && ! in.active && idle && ! inputS.isSmoothing());
        if (! running)
        {
            for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::copy (ch[c], proc.getReadPointer (c), n);
            if (! idle) idle = true;
            skipSmoothers (n);
            return;
        }
        if (idle) { fx[(size_t) cur]->reset(); if (os) os->reset(); idle = false; quietSamples = 0; }

        // Diffusion: scale input (trails bypass fades the input, not the output)
        if (diff)
            for (int i = 0; i < n; ++i)
            {
                const float g = inputS.getNextValue();
                ch[0][i] *= g; ch[1][i] *= g;
            }
        else inputS.skip (n);

        if (ctx.transportStarted && ctx.synced) fx[(size_t) cur]->syncPhase();
        runEffects (buf, n, ctx);

        // output stage
        float peak = 0;
        const float* dry[2] = { proc.getReadPointer (0), proc.getReadPointer (1) };
        for (int i = 0; i < n; ++i)
        {
            const float vol = volS.getNextValue(), act = activeS.getNextValue();
            for (int c = 0; c < 2; ++c)
            {
                const float w = ch[c][i];
                if (diff) { ch[c][i] = dry[c][i] + w * vol * act; peak = std::max (peak, std::fabs (w)); }
                else ch[c][i] = dry[c][i] + (w * vol - dry[c][i]) * act;
            }
        }
        // Diffusion in trail mode goes idle once its tail has died away
        if (tailMode && ! in.active)
        {
            quietSamples = peak < 1.0e-5f ? quietSamples + n : 0;
            if (quietSamples > (int) hostRate) idle = true;
        }
    }

private:
    void skipSmoothers (int n) { amountS.skip (n); knobBS.skip (n); secS.skip (n); volS.skip (n); activeS.skip (n); inputS.skip (n); }

    void runEffects (juce::AudioBuffer<float>& buf, int n, const ProcessContext& hostCtx)
    {
        ProcessContext ctx = hostCtx; ctx.sampleRate = fxRate;
        juce::dsp::AudioBlock<float> block (buf.getArrayOfWritePointers(), 2, (size_t) n);
        juce::dsp::AudioBlock<float> up = os ? os->processSamplesUp (block) : block;
        const int N = (int) up.getNumSamples();
        float* L = up.getChannelPointer (0); float* R = up.getChannelPointer (1);
        constexpr int chunk = 32;
        for (int s = 0; s < N; s += chunk)
        {
            const int m = std::min (chunk, N - s);
            const int hostSteps = std::max (1, m / factor);
            EffectParams p;
            p.amount = amountS.skip (hostSteps); p.knobB = knobBS.skip (hostSteps); p.sec = secS.skip (hostSteps);
            if (id == Movement || id == Diffusion) p.sec = p.sec + ctx.driftMacro * (1.0f - p.sec);
            if (prev >= 0)
            {
                float* pl = prevBuf.getWritePointer (0); float* pr = prevBuf.getWritePointer (1);
                std::copy (L + s, L + s + m, pl); std::copy (R + s, R + s + m, pr);
                fx[(size_t) prev]->process (pl, pr, m, p, ctx);
                fx[(size_t) cur]->process (L + s, R + s, m, p, ctx);
                for (int i = 0; i < m; ++i)
                {
                    const float f = std::min (1.0f, (float) fadePos++ / (float) fadeLen);
                    L[s + i] = pl[i] + (L[s + i] - pl[i]) * f;
                    R[s + i] = pr[i] + (R[s + i] - pr[i]) * f;
                }
                if (fadePos >= fadeLen) prev = -1;
            }
            else fx[(size_t) cur]->process (L + s, R + s, m, p, ctx);
        }
        if (os) os->processSamplesDown (block);
    }

    int id, osStages = 0, factor = 1, latency = 0;
    float hostRate = 48000, fxRate = 48000;
    std::array<std::unique_ptr<Effect>, 5> fx;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::AudioBuffer<float> proc, prevBuf;
    dsp::DelayLine dryDelay[2];
    juce::SmoothedValue<float> amountS, knobBS, secS, volS, activeS, inputS;
    int cur = 0, prev = -1, fadePos = 0, fadeLen = 1, quietSamples = 0;
    bool firstBlock = true, idle = false;
};
} // namespace prism
