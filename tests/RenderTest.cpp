// Offline render test: runs every effect through a range of settings on two test
// signals (ukulele-like plucks and a hot synth saw) and checks for NaNs, runaway
// levels and silence. Also exercises reordering, bypass, switching and tempo modes.
// Optional: pass a directory to write WAV renders of each effect for listening.
#include "PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>
#include "PluginEditor.h"

using namespace prism;

static void setParam (PrismRackProcessor& p, const juce::String& id, float value)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

static juce::AudioBuffer<float> makeUke (double sr, int len)
{
    juce::AudioBuffer<float> b (2, len); b.clear();
    const float notes[] = { 392.0f, 261.6f, 329.6f, 440.0f };   // G C E A
    juce::Random rnd (1);
    for (int k = 0; k < 8; ++k)
    {
        const int start = (int) (k * 0.45 * sr);
        for (int s = 0; s < 4; ++s)
        {
            const float f = notes[s] * (k % 2 ? 1.122f : 1.0f);
            for (int i = start + s * 300; i < len; ++i)
            {
                const float t = (float) (i - start) / (float) sr;
                const float env = std::exp (-t * 4.0f);
                const float v = 0.03f * env * (std::sin (6.2831853f * f * t) + 0.3f * std::sin (12.566f * f * t));
                b.addSample (0, i, v); b.addSample (1, i, v);
            }
        }
    }
    return b;   // peaks around -18 dBFS
}

static juce::AudioBuffer<float> makeSynth (double sr, int len)
{
    juce::AudioBuffer<float> b (2, len);
    float ph = 0;
    for (int i = 0; i < len; ++i)
    {
        const float f = (i / (int) (0.5 * sr)) % 2 ? 110.0f : 146.8f;
        ph += f / (float) sr; ph -= std::floor (ph);
        const float v = 0.7f * (2.0f * ph - 1.0f);
        b.setSample (0, i, v); b.setSample (1, i, v * 0.9f);
    }
    return b;   // about -3 dBFS
}

struct Result { float peak = 0, rms = 0; bool finite = true; };

static Result render (PrismRackProcessor& p, const juce::AudioBuffer<float>& in, juce::AudioBuffer<float>* keep = nullptr, int block = 256)
{
    Result r; double sum = 0; int count = 0;
    juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi;
    if (keep) keep->setSize (2, in.getNumSamples());
    for (int start = 0; start + block <= in.getNumSamples(); start += block)
    {
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, in, c, start, block);
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
        {
            const float* d = buf.getReadPointer (c);
            for (int i = 0; i < block; ++i)
            {
                if (! std::isfinite (d[i])) r.finite = false;
                r.peak = std::max (r.peak, std::fabs (d[i]));
                sum += (double) d[i] * d[i]; ++count;
            }
            if (keep) keep->copyFrom (c, start, buf, c, 0, block);
        }
    }
    r.rms = (float) std::sqrt (sum / std::max (1, count));
    return r;
}

static void soloModule (PrismRackProcessor& p, int solo)
{
    for (int m = 0; m < NumModules; ++m) setParam (p, pid (m, "bypass"), m == solo ? 0.0f : 1.0f);
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (auto* w = wav.createWriterFor (os.get(), sr, 2, 24, {}, 0))
    {
        os.release();
        std::unique_ptr<juce::AudioFormatWriter> writer (w);
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0; const int len = (int) (4.0 * sr);
    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
    {
        // Render the editor to PNGs (light, dark, expanded) for a visual check
        PrismRackProcessor sp; sp.setPlayConfigDetails (2, 2, sr, 256); sp.prepareToPlay (sr, 256);
        sp.apvts.getParameter (pid (Diffusion, "type"))->setValueNotifyingHost (0.5f);
        for (int pass = 0; pass < 3; ++pass)
        {
            sp.apvts.state.setProperty ("darkMode", pass == 1, nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> ed (sp.createEditor());
            if (pass == 2) if (auto* b = dynamic_cast<juce::Button*> (ed->getChildComponent (0)->getChildComponent (0))) (void) b;
            for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
            if (pass == 2)
            {
                // click SETUP
                std::function<void (juce::Component*)> find = [&] (juce::Component* c)
                {
                    if (auto* tb = dynamic_cast<juce::TextButton*> (c)) if (tb->getButtonText() == "SETUP") tb->triggerClick();
                    for (auto* ch : c->getChildren()) find (ch);
                };
                find (ed.get());
                for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
            }
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f (juce::String (argv[2]) + (pass == 0 ? "_light.png" : pass == 1 ? "_dark.png" : "_setup.png"));
            f.deleteFile();
            if (auto os = f.createOutputStream()) juce::PNGImageFormat().writeImageToStream (img, *os);
        }
        return 0;
    }
    juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File();
    if (outDir != juce::File()) outDir.createDirectory();

    PrismRackProcessor p;
    p.setPlayConfigDetails (2, 2, sr, 256);
    p.prepareToPlay (sr, 256);
    std::printf ("Latency: %d samples (%.2f ms)\n", p.getLatencySamples(), 1000.0 * p.getLatencySamples() / sr);

    const auto uke = makeUke (sr, len), synth = makeSynth (sr, len);
    int failures = 0;
    const float settings[][3] = { { 0.0f, 0.5f, 0.0f }, { 0.5f, 0.5f, 0.5f }, { 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f }, { 0.3f, 1.0f, 1.0f } };

    std::printf ("\n%-10s %-13s %-8s %8s %8s %8s %8s\n", "module", "effect", "source", "minPeak", "maxPeak", "maxRMS", "status");
    for (int m = 0; m < NumModules; ++m)
    {
        soloModule (p, m);
        for (int e = 0; e < 5; ++e)
        {
            setParam (p, pid (m, "type"), (float) e);
            for (int src = 0; src < 2; ++src)
            {
                setParam (p, ids::headroom, src == 0 ? 0.0f : 3.0f);   // Low for uke, Very High for synth
                float minPeak = 1e9f, maxPeak = 0, maxRms = 0; bool ok = true;
                for (auto& s : settings)
                {
                    setParam (p, pid (m, "amount"), s[0]); setParam (p, pid (m, "knobB"), s[1]);
                    if (m != Texture) setParam (p, pid (m, "sec"), s[2]);
                    p.prepareToPlay (sr, 256);  // fresh state for each setting
                    juce::AudioBuffer<float> keep;
                    const auto r = render (p, src == 0 ? uke : synth, outDir != juce::File() ? &keep : nullptr);
                    ok &= r.finite && r.peak < 3.9f;
                    minPeak = std::min (minPeak, r.peak); maxPeak = std::max (maxPeak, r.peak); maxRms = std::max (maxRms, r.rms);
                    if (outDir != juce::File() && &s == &settings[1])
                        writeWav (outDir.getChildFile (juce::String (moduleName (m)) + "_" + effectNames (m)[e] + (src ? "_synth" : "_uke") + ".wav"), keep, sr);
                }
                // silence check: at the middle setting every effect should pass signal
                const bool silent = maxPeak < 1e-4f;
                if (! ok || silent) ++failures;
                std::printf ("%-10s %-13s %-8s %8.3f %8.3f %8.3f %8s\n", moduleName (m), effectNames (m)[e].toRawUTF8(), src ? "synth" : "uke",
                             minPeak, maxPeak, maxRms, ok && ! silent ? "ok" : "FAIL");
            }
        }
        setParam (p, pid (m, "amount"), 0.4f); setParam (p, pid (m, "knobB"), 0.5f);
        if (m != Texture) setParam (p, pid (m, "sec"), 0.2f);
    }
    setParam (p, ids::headroom, 2.0f);

    // ---- full chain, reordering, switching, bypass, tempo modes ----
    std::printf ("\nChain tests\n");
    for (int m = 0; m < NumModules; ++m) setParam (p, pid (m, "bypass"), 0.0f);
    p.prepareToPlay (sr, 256);
    juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi;
    juce::Random rnd (7); bool ok = true; float peak = 0;
    for (int b = 0; b < 3000; ++b)   // ~16 s of random tweaking
    {
        const int start = (b * 256) % (len - 256);
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, (b / 400) % 2 ? synth : uke, c, start, 256);
        if (b % 37 == 0) setParam (p, ids::order, (float) rnd.nextInt (24));
        if (b % 23 == 0) { const int m = rnd.nextInt (4); setParam (p, pid (m, "type"), (float) rnd.nextInt (5)); }
        if (b % 11 == 0) { const int m = rnd.nextInt (4); setParam (p, pid (m, "amount"), rnd.nextFloat()); setParam (p, pid (m, "knobB"), rnd.nextFloat()); }
        if (b % 101 == 0) setParam (p, ids::bypass, rnd.nextBool() ? 1.0f : 0.0f);
        if (b % 131 == 0) setParam (p, pid (rnd.nextInt (4), "alwaysOn"), rnd.nextBool() ? 1.0f : 0.0f);
        if (b % 173 == 0) setParam (p, ids::syncMode, (float) rnd.nextInt (3));
        if (b % 59 == 0) setParam (p, ids::mix, rnd.nextFloat());
        if (b % 211 == 0) setParam (p, ids::headroom, (float) rnd.nextInt (4));
        if (b % 97 == 0) setParam (p, ids::filterMode, (float) rnd.nextInt (3));
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) { const float v = buf.getSample (c, i); ok &= std::isfinite (v); peak = std::max (peak, std::fabs (v)); }
    }
    std::printf ("Random tweak run: %s, peak %.3f\n", ok ? "ok" : "FAIL (non-finite)", peak);
    if (! ok) ++failures;

    // bypass = unity: all modules off should give the input back (delayed by latency)
    setParam (p, ids::bypass, 1.0f);
    for (int m = 0; m < NumModules; ++m) setParam (p, pid (m, "alwaysOn"), 0.0f);
    setParam (p, ids::trails, 0.0f);
    p.prepareToPlay (sr, 256);
    juce::AudioBuffer<float> out;
    render (p, uke, &out);
    const int lat = p.getLatencySamples();
    double err = 0;
    for (int i = 48000; i < len - 256 - lat; ++i) err = std::max (err, (double) std::fabs (out.getSample (0, i + lat) - uke.getSample (0, i)));
    std::printf ("Bypass null test: max error %.2e %s\n", err, err < 1e-4 ? "ok" : "FAIL");
    if (err >= 1e-4) ++failures;
    setParam (p, ids::bypass, 0.0f); setParam (p, ids::trails, 1.0f);

    // Pitch check: a 440 Hz sine through Pitch at +12 semitones should come out near 880 Hz
    {
        soloModule (p, Movement);
        setParam (p, pid (Movement, "type"), 4.0f); setParam (p, pid (Movement, "amount"), 1.0f);
        setParam (p, pid (Movement, "knobB"), 0.0f); setParam (p, pid (Movement, "sec"), 0.0f);
        setParam (p, ids::mix, 1.0f); setParam (p, ids::headroom, 2.0f);
        p.prepareToPlay (sr, 256);
        juce::AudioBuffer<float> sine (2, len), res;
        for (int i = 0; i < len; ++i) { const float v = 0.3f * std::sin (6.2831853f * 440.0f * (float) i / (float) sr); sine.setSample (0, i, v); sine.setSample (1, i, v); }
        render (p, sine, &res);
        int crossings = 0;
        for (int i = (int) sr + 1; i < 3 * (int) sr; ++i) if (res.getSample (0, i - 1) < 0 && res.getSample (0, i) >= 0) ++crossings;
        const float hz = crossings / 2.0f;
        std::printf ("Pitch +12 st: measured %.1f Hz (expect ~880) %s\n", hz, std::fabs (hz - 880.0f) < 20.0f ? "ok" : "FAIL");
        if (std::fabs (hz - 880.0f) >= 20.0f) ++failures;
        for (int m = 0; m < NumModules; ++m) setParam (p, pid (m, "bypass"), 0.0f);
    }

    // CPU: time the default patch
    p.prepareToPlay (sr, 128);
    juce::AudioBuffer<float> small (2, 128);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const int blocks = (int) (10.0 * sr / 128);
    for (int b = 0; b < blocks; ++b) { for (int c = 0; c < 2; ++c) small.copyFrom (c, 0, synth, c, (b * 128) % (len - 128), 128); p.processBlock (small, midi); }
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    std::printf ("CPU: 10 s of audio in %.0f ms (%.1f %% of one core)\n", ms, ms / 100.0);

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
