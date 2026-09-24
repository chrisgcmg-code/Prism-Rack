#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace prism;

PrismRackProcessor::PrismRackProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PrismRack", createLayout())
{
    for (int m = 0; m < NumModules; ++m)
    {
        mp[(size_t) m].type     = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (pid (m, "type")));
        mp[(size_t) m].amount   = apvts.getRawParameterValue (pid (m, "amount"));
        mp[(size_t) m].knobB    = apvts.getRawParameterValue (pid (m, "knobB"));
        mp[(size_t) m].sec      = m != Texture ? apvts.getRawParameterValue (pid (m, "sec")) : nullptr;
        mp[(size_t) m].vol      = apvts.getRawParameterValue (pid (m, "vol"));
        mp[(size_t) m].bypass   = apvts.getRawParameterValue (pid (m, "bypass"));
        mp[(size_t) m].alwaysOn = apvts.getRawParameterValue (pid (m, "alwaysOn"));
        modules[(size_t) m] = std::make_unique<Module> (m);
    }
    pMix        = apvts.getRawParameterValue (ids::mix);
    pOutput     = apvts.getRawParameterValue (ids::output);
    pHeadroom   = apvts.getRawParameterValue (ids::headroom);
    pOrder      = apvts.getRawParameterValue (ids::order);
    pBypass     = apvts.getRawParameterValue (ids::bypass);
    pTrails     = apvts.getRawParameterValue (ids::trails);
    pDriftMacro = apvts.getRawParameterValue (ids::driftMacro);
    pSync       = apvts.getRawParameterValue (ids::syncMode);
    pBpm        = apvts.getRawParameterValue (ids::manualBpm);
    pFilterMode = apvts.getRawParameterValue (ids::filterMode);
    pRevSpeed   = apvts.getRawParameterValue (ids::revSpeed);
    apvts.state.setProperty ("darkMode", false, nullptr);
}

bool PrismRackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo()) return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void PrismRackProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int maxBlock = std::max (1, samplesPerBlock);
    totalLatency = 0;
    for (auto& m : modules) { m->prepare (sampleRate, maxBlock); totalLatency += m->getLatency(); }
    work.setSize (2, maxBlock);
    hgBuf.assign ((size_t) maxBlock, 1.0f);
    for (auto& d : dryDelay) { d.prepare (totalLatency + 8); d.reset(); }
    setLatencySamples (totalLatency);

    headroomS.reset (sampleRate, 0.05);  headroomS.setCurrentAndTargetValue (dsp::dbToGain (headroomGainDb ((int) pHeadroom->load())));
    mixS.reset (sampleRate, 0.03);       mixS.setCurrentAndTargetValue (pMix->load());
    outS.reset (sampleRate, 0.02);       outS.setCurrentAndTargetValue (dsp::dbToGain (pOutput->load()));
    bypassS.reset (sampleRate, 0.01);    bypassS.setCurrentAndTargetValue (0.0f);
}

void PrismRackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    if (n > work.getNumSamples())   // host sent a bigger block than promised: process in pieces
    {
        const int step = work.getNumSamples();
        juce::MidiBuffer dummy;
        for (int start = 0; start < n; start += step)
        {
            const int len = std::min (step, n - start);
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, len);
            processBlock (sub, dummy);
        }
        return;
    }

    const int numIn = getTotalNumInputChannels();
    const float* inL = buffer.getReadPointer (0);
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : inL;

    // ---- tempo ----
    ProcessContext ctx;
    bool playing = false; float hostBpm = 120.0f;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto b = pos->getBpm()) hostBpm = (float) *b;
        }
    const int sync = (int) pSync->load();
    ctx.synced = sync != 0;
    ctx.bpm = sync == 2 ? pBpm->load() : (sync == 1 ? hostBpm : 120.0f);
    ctx.bpm = juce::jlimit (20.0f, 400.0f, ctx.bpm);
    ctx.transportStarted = playing && ! wasPlaying;
    wasPlaying = playing;
    ctx.driftMacro = pDriftMacro->load();
    ctx.filterMode = (int) pFilterMode->load();
    ctx.reverseSpeedOct = pRevSpeed->load();
    displayBpm.store (ctx.bpm);

    // ---- auto headroom: measure input peak for 5 s ----
    if (autoRequested.exchange (false)) { autoSamplesLeft = (int) (5.0 * getSampleRate()); autoPeak = 0; autoDone = false; }
    if (autoSamplesLeft > 0)
    {
        for (int i = 0; i < n; ++i) autoPeak = std::max (autoPeak, std::max (std::fabs (inL[i]), std::fabs (inR[i])));
        autoSamplesLeft -= n;
        autoProgress.store (1.0f - (float) std::max (0, autoSamplesLeft) / (float) (5.0 * getSampleRate()));
        if (autoSamplesLeft <= 0)
        {
            // choose the setting that puts peaks closest to -12 dBFS going into Character
            const float peakDb = dsp::gainToDb (std::max (autoPeak, 1.0e-6f));
            int best = 2; float bestErr = 1e9f;
            for (int c = 0; c < 4; ++c)
            {
                const float err = std::fabs (peakDb + headroomGainDb (c) + 12.0f);
                if (err < bestErr) { bestErr = err; best = c; }
            }
            autoResult.store (best); autoDone.store (true);
        }
    }

    // ---- dry path (latency-compensated) and chain input ----
    float* wL = work.getWritePointer (0); float* wR = work.getWritePointer (1);
    headroomS.setTargetValue (dsp::dbToGain (headroomGainDb ((int) pHeadroom->load())));
    for (int i = 0; i < n; ++i)
    {
        const float hg = headroomS.getNextValue();
        hgBuf[(size_t) i] = hg;                      // kept so the undo stage uses the identical ramp
        wL[i] = inL[i] * hg; wR[i] = inR[i] * hg;
    }

    // ---- module chain ----
    const bool globalBypass = pBypass->load() > 0.5f;
    bool anyAlwaysOn = false;
    for (auto& p : mp) anyAlwaysOn |= p.alwaysOn->load() > 0.5f;
    const bool totalBypass = globalBypass && ! anyAlwaysOn;
    const auto order = orderFromIndex ((int) pOrder->load());
    for (int slot = 0; slot < 4; ++slot)
    {
        const int m = order[(size_t) slot];
        const auto& p = mp[(size_t) m];
        ModuleInputs in;
        in.type = p.type->getIndex();
        in.amount = p.amount->load(); in.knobB = p.knobB->load(); in.sec = p.sec ? p.sec->load() : 0.0f;
        in.volDb = p.vol->load();
        in.active = p.bypass->load() < 0.5f && (! globalBypass || p.alwaysOn->load() > 0.5f);
        in.trails = pTrails->load() > 0.5f;
        modules[(size_t) m]->process (work, n, in, ctx);
    }

    // ---- undo headroom, mix, bypass, output ----
    mixS.setTargetValue (pMix->load());
    outS.setTargetValue (dsp::dbToGain (pOutput->load()));
    bypassS.setTargetValue (totalBypass ? 1.0f : 0.0f);
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    bool bad = false;
    for (int i = 0; i < n; ++i)
    {
        dryDelay[0].push (inL[i]); dryDelay[1].push (inR[i]);
        const float dL = dryDelay[0].at (totalLatency), dR = dryDelay[1].at (totalLatency);
        const float inv = 1.0f / hgBuf[(size_t) i];
        const float cL = wL[i] * inv, cR = wR[i] * inv;
        const float mix = mixS.getNextValue();
        const float gd = std::cos (mix * dsp::kPi * 0.5f), gw = std::sin (mix * dsp::kPi * 0.5f);
        const float bp = bypassS.getNextValue(), og = outS.getNextValue();
        float yl = dL * gd + cL * gw, yr = dR * gd + cR * gw;
        yl = (yl + (cL - yl) * bp) * og;
        yr = (yr + (cR - yr) * bp) * og;
        if (! std::isfinite (yl) || ! std::isfinite (yr)) { bad = true; yl = yr = 0; }
        outL[i] = juce::jlimit (-4.0f, 4.0f, yl);
        if (outR) outR[i] = juce::jlimit (-4.0f, 4.0f, yr);
    }
    if (bad)   // something blew up: clear all state so we recover instead of emitting NaNs
    {
        ++sanitizeCount;
        for (auto& m : modules) m->resetState();
        for (auto& d : dryDelay) d.reset();
        buffer.clear();
    }
}

juce::AudioProcessorEditor* PrismRackProcessor::createEditor() { return new PrismRackEditor (*this); }

void PrismRackProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", kStateVersion, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void PrismRackProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PrismRackProcessor(); }
