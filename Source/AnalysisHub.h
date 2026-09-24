//
//  AnalysisHub.h
//  FX Analyzer
//
//  Everything the panel measures, behind one door.
//
//  The processor owns one of these and does nothing else. The editor asks it
//  for numbers. Splitting it this way is what lets AnalyzerCheck link the whole
//  measurement chain - channel select, DC block, gain, all five analysers -
//  without dragging in a single line of JUCE's plugin machinery, and a
//  measurement harness that has to instantiate an AudioProcessor is a harness
//  nobody runs.
//
//  Two threads meet here and the split is deliberate:
//
//    pushBlock()      audio thread. Builds the analysis channels, feeds the
//                     meters that must not miss a sample - loudness, true peak,
//                     correlation - and drops the rest into the capture ring.
//                     Allocates nothing.
//
//    updateDisplays() message thread, from the editor's timer. Pulls windows
//                     out of the ring and runs the expensive, stateless work:
//                     the FFT, the scope's trigger search, YIN.
//
//  So the panel can be closed for an hour and the integrated loudness is still
//  right when it opens, while the spectrum costs nothing at all while nobody is
//  looking at it.
//
//  The analysis chain, in order: channel select, DC block, input gain. Input
//  gain is last because it is a display trim - putting it first would feed a
//  boosted signal to the DC blocker and change what the blocker does, and the
//  filter's job should not depend on the readability of the graph.
//

#pragma once

#include <atomic>

#include "DCBlocker.h"
#include "LoudnessMeter.h"
#include "Oscilloscope.h"
#include "ParameterIds.h"
#include "PitchDetector.h"
#include "RingBuffer.h"
#include "SpectrumAnalyser.h"
#include "StereoAnalyser.h"
#include "TruePeakMeter.h"

class AnalysisHub
{
public:
    AnalysisHub() = default;

    /** Allocates everything. Message thread only. */
    void prepare (double sampleRate, int maximumBlockSize);

    double getSampleRate() const noexcept { return rate; }

    //==============================================================================
    // Settings. Written by the message thread, read by both.

    void setChannel      (FXParams::Channel c) noexcept { channel.store ((int) c, std::memory_order_relaxed); }
    void setReactivity   (FXParams::Reactivity r) noexcept { reactivity.store ((int) r, std::memory_order_relaxed); }
    void setDcBlockEnabled (bool shouldBlock) noexcept { dcBlockEnabled.store (shouldBlock, std::memory_order_relaxed); }
    void setInputGainDb  (float db) noexcept;
    void setFrozen       (bool shouldFreeze) noexcept { frozen.store (shouldFreeze, std::memory_order_relaxed); }

    bool isFrozen() const noexcept { return frozen.load (std::memory_order_relaxed); }

    /** Which page is on screen. Only the pitch detector cares, and it cares a
        great deal: it is the one analyser expensive enough that running it
        unwatched would show up in a host's CPU meter. */
    void setActivePage (FXParams::Page p) noexcept { activePage = p; }

    /** Message thread only - it reallocates the transform. */
    void setFftOrder (int order);

    /** Fractional-octave smoothing of the spectrum display. 0 is off.
        Rebuilds a table, so it takes the lock like the other reallocations. */
    void setSpectrumSmoothing (float fractionOfOctave);

    /** Single or multi-resolution. Message thread only - it builds transforms.

        The hub's own default is Single, deliberately: the panel's default is
        Multi and arrives through applyDisplaySettings(), while a bare hub built
        by a test stays simple until the test asks for otherwise. */
    void setSpectrumResolution (FXParams::SpectrumResolution);

    /** The spectrum's rise time. Negative follows Reactivity, which is what the
        panel's Auto setting passes. */
    void setSpectrumAttackSeconds (float seconds) noexcept
    {
        attackOverride.store (seconds, std::memory_order_relaxed);
    }

    /** The spectrum's fall time. Negative follows Reactivity, as for attack. */
    void setSpectrumReleaseSeconds (float seconds) noexcept
    {
        releaseOverride.store (seconds, std::memory_order_relaxed);
    }

    void setPitchReferenceHz (float hz) noexcept { pitchReferenceHz = hz; }
    float getPitchReferenceHz() const noexcept { return pitchReferenceHz; }

    void setScopeWindowSeconds (float seconds) noexcept { scopeWindowSeconds = seconds; }
    void setScopeTrigger (FXParams::ScopeTrigger t) noexcept { scopeTrigger = t; }

    //==============================================================================
    /** Real-time safe. The buffer is read and never written. */
    void pushBlock (const juce::AudioBuffer<float>& buffer) noexcept;

    /** Message thread, from the editor's timer. Does nothing while frozen. */
    void updateDisplays();

    /** Clears the integrated loudness, the held true peak and the peak-hold
        curve. Safe from the message thread while audio is running: the meters
        defer the actual clearing to their own next block. */
    void resetMeters();

    //==============================================================================
    /** The shortest window - the one that used to be the only one. Kept as the
        name everything not concerned with tiers still uses. */
    const SpectrumAnalyser& getSpectrum() const noexcept { return *tiers.front()->analyser; }

    int getNumTiers() const noexcept { return (int) tiers.size(); }

    const SpectrumAnalyser& getTierAnalyser (int index) const noexcept
    {
        return *tiers[(size_t) juce::jlimit (0, getNumTiers() - 1, index)]->analyser;
    }

    /** Which tier answers for a frequency, and how much of the neighbouring one
        to mix in across the seam. `secondaryWeight` is the share of
        `secondary`; zero means `primary` alone. */
    struct TierChoice
    {
        int   primary = 0;
        int   secondary = 0;
        float secondaryWeight = 0.0f;
    };

    TierChoice chooseTier (float hz) const noexcept;

    /** Human-readable summary for the panel's info line, e.g.
        "4096 / 16384 / 65536 pt". */
    juce::String describeTiers() const;
    const Oscilloscope&     getScope()    const noexcept { return scope; }
    const StereoAnalyser&   getStereo()   const noexcept { return stereo; }
    const LoudnessMeter&    getLoudness() const noexcept { return loudness; }
    const TruePeakMeter&    getTruePeak() const noexcept { return truePeak; }
    const CaptureRing&      getRing()     const noexcept { return ring; }

    PitchDetector::Result getPitch() const noexcept { return pitch.getResult(); }

    /** How many FFT frames the shortest tier has analysed since prepare().
        Exposed so that AnalyzerCheck can assert the overlap is really 75%
        rather than take the hop arithmetic's word for it. */
    int64_t getSpectrumFrameCount() const noexcept { return tiers.front()->frames; }

    /** Samples between the start of one analysis window and the next, for a
        given window size. Never more than a display frame's worth, so no tier
        can update more slowly than the panel repaints. */
    int hopSamplesFor (int fftSize) const noexcept
    {
        return juce::jmax (1, juce::jmin (fftSize / overlapFactor,
                                          (int) (rate / (double) analysisFramesPerSecond)));
    }

    int getSpectrumHopSamples() const noexcept { return hopSamplesFor (getSpectrum().getFftSize()); }

    /** Four means each window starts a quarter of a window after the last, so
        every sample is seen by at least four transforms. */
    static constexpr int overlapFactor = 4;

    /** A floor on how often every tier is transformed, matching the editor's
        timer.

        75% overlap alone is not enough once the tiers get long. A 65536-point
        window at 48 kHz hops 16384 samples, which is 341 ms - so the bass third
        of the curve stood still for a third of a second and then jumped, while
        the 4096-point tier above it updated 47 times a second. The result reads
        as the bass stuttering, and it is worst at Very Slow, where everything
        else on screen is calm enough for the stepping to stand out.

        Capping the hop in *time* rather than in samples fixes it at the source:
        every tier is transformed at least thirty times a second whatever its
        length, so the whole curve moves at one rate. Nothing is interpolated -
        each frame is a real transform of real audio, the windows simply overlap
        more (97.6% for the longest tier).

        It is affordable because vDSP is quick: measured on this machine, one
        65536-point transform costs 0.262 ms, so thirty a second is 0.79% of a
        core, and all three tiers together are about 1%. Guessing would have put
        it an order of magnitude higher and rejected the honest fix. */
    static constexpr int analysisFramesPerSecond = FXParams::displayFramesPerSecond;

    /** Ring channel layout, named so that no page has to remember which index
        the goniometer wants. */
    static constexpr int ringAnalysis = 0;
    static constexpr int ringLeft     = 1;
    static constexpr int ringRight    = 2;

private:
    float currentSmoothingSeconds() const noexcept
    {
        const auto override_ = releaseOverride.load (std::memory_order_relaxed);

        if (override_ >= 0.0f)
            return override_;

        return FXParams::reactivitySeconds ((FXParams::Reactivity) reactivity.load (std::memory_order_relaxed));
    }

    float currentStereoWindowSeconds() const noexcept
    {
        return FXParams::stereoWindowSeconds ((FXParams::Reactivity) reactivity.load (std::memory_order_relaxed));
    }

    float currentPitchSmoothingSeconds() const noexcept
    {
        return FXParams::pitchSmoothingSeconds ((FXParams::Reactivity) reactivity.load (std::memory_order_relaxed));
    }

    float currentAttackSeconds() const noexcept
    {
        const auto override_ = attackOverride.load (std::memory_order_relaxed);

        if (override_ >= 0.0f)
            return override_;

        // Auto at Very Slow means "the same as the fall", and it has to mean
        // the fall actually in use. Reading Reactivity's two seconds here while
        // Release had been set to four would quietly turn the one setting that
        // averages back into a slow peak envelope - see reactivityAttackSeconds
        // for what that costs on alternating material.
        const auto r = (FXParams::Reactivity) reactivity.load (std::memory_order_relaxed);

        if (r == FXParams::Reactivity::verySlow)
            return currentSmoothingSeconds();

        return FXParams::reactivityAttackSeconds (r);
    }

    float currentHoldSeconds() const noexcept
    {
        return FXParams::peakHoldSeconds ((FXParams::Reactivity) reactivity.load (std::memory_order_relaxed));
    }

    /** Guards every buffer in this class against being resized underneath a
        reader.

        The three entry points run on three different threads and two of them
        reallocate. prepareToPlay - which is not guaranteed to be the message
        thread - resizes the ring, the scratch, the FFT and every analyser's
        vectors; the editor's timer walks those same vectors thirty times a
        second; the audio thread writes into them. A host re-preparing while the
        panel is open is not an edge case, it is what happens every time
        somebody drags a plugin to another slot during playback, and without
        this lock it is a segfault. AnalyzerCheck reproduces it.

        The audio thread takes it with a *try* lock and skips its analysis when
        it cannot have it. Blocking there would trade a crash for a dropout,
        which is not a trade worth making: a missed block of analysis is one
        frame of a display nobody can see at thirty frames a second. */
    juce::CriticalSection analysisLock;

    double rate = 48000.0;
    int    maxBlock = 512;

    // Settings shared across the thread boundary.
    std::atomic<int>   channel        { (int) FXParams::Channel::leftPlusRight };
    std::atomic<int>   reactivity     { (int) FXParams::Reactivity::medium };
    std::atomic<bool>  dcBlockEnabled { true };
    std::atomic<float> inputGain      { 1.0f };
    std::atomic<bool>  frozen         { false };
    std::atomic<float> attackOverride { -1.0f };   // negative: follow Reactivity
    std::atomic<float> releaseOverride { -1.0f };  // negative: follow Reactivity

    // Message-thread only; no atomic needed and none pretended.
    FXParams::Page         activePage         = FXParams::Page::spectrum;
    FXParams::ScopeTrigger scopeTrigger       = FXParams::ScopeTrigger::rising;
    float                  scopeWindowSeconds = 0.02f;
    float                  pitchReferenceHz   = FXParams::pitchRefDefaultHz;

    // Audio-thread scratch. Three channels: the analysis signal, then left and
    // right as the display sees them.
    juce::AudioBuffer<float> scratch;
    std::vector<float>       window;

    DCBlocker dcLeft, dcRight;

    /** One window length, with the schedule that advances it.

        Each tier has its own next-window position because the hop is a quarter
        of *its* window - 1024 samples for the 4096-point tier, 16384 for the
        65536-point one - so a single shared position would be a quarter of the
        wrong thing for two of the three. */
    struct Tier
    {
        std::unique_ptr<SpectrumAnalyser> analyser;
        int64_t nextAnalysisEnd = 0;
        int64_t frames = 0;
    };

    void rebuildTiers();
    void advanceTier (Tier&, int64_t writePosition, float smoothing, float attack, float hold);

    std::vector<std::unique_ptr<Tier>> tiers;

    /** boundaryHz[i] separates tier i (used above it) from tier i+1 (below). */
    std::vector<float> boundaryHz;

    FXParams::SpectrumResolution resolution = FXParams::SpectrumResolution::single;
    int   requestedOrder  = 12;
    float octaveSmoothing = 0.0f;   // remembered, so a rebuild keeps it

    CaptureRing      ring;
    Oscilloscope     scope;
    StereoAnalyser   stereo;
    LoudnessMeter    loudness;
    TruePeakMeter    truePeak;
    PitchDetector    pitch;

    double lastUpdateMs   = 0.0;
    int    pitchFrameFlip = 0;



    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisHub)
};
