//
//  AnalysisHub.cpp
//  FX Analyzer
//

#include "AnalysisHub.h"

#include <cmath>

//==============================================================================
void AnalysisHub::prepare (double sampleRate, int maximumBlockSize)
{
    const juce::ScopedLock lock (analysisLock);

    rate     = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock = juce::jmax (16, maximumBlockSize);

    // Two seconds of history, or three of the longest analysis window if that
    // is more. The 65536-point tier needs 65536 samples plus its 16384 hop
    // before it can even read one window, which at 44.1 kHz is most of a two
    // second ring - too little headroom for a buffer whose whole safety
    // argument is that it is far larger than any read. See RingBuffer.h.
    const auto longestWindow = 1 << FXParams::maxSpectrumOrder;

    // Three terms, and the third is not decoration. The scope's trigger search
    // reads *twice* its window, so a two second time base needs four seconds of
    // history. The FFT term is a fixed number of samples and so shrinks in time
    // as the rate rises, while the scope term grows with it - at 96 kHz and
    // above the FFT term alone left the ring too small, and the trigger would
    // have been searching a window that had already been overwritten.
    const auto scopeSamples = (int) std::ceil (rate * FXParams::longestScopeSeconds());

    ring.prepare (3, juce::jmax (juce::jmax ((int) (rate * 2.0), longestWindow * 3), scopeSamples * 2));

    scratch.setSize (3, maxBlock, false, true, true);
    scratch.clear();

    dcLeft.prepare  (rate);
    dcRight.prepare (rate);

    rebuildTiers();
    scope.prepare    (rate, scopeSamples);   // from the longest entry, never a second copy
    stereo.prepare   (rate);
    loudness.prepare (rate, 2);
    truePeak.prepare (2, maxBlock);
    pitch.prepare    (rate, FXParams::pitchMinHz, FXParams::pitchMaxHz);

    // One buffer big enough for whichever reader wants the most. Sized here
    // rather than in the readers so that a change of FFT size cannot allocate
    // on the message thread while the timer is mid-frame.
    window.assign ((size_t) juce::jmax (1 << FXParams::maxSpectrumOrder, pitch.getWindowSize()), 0.0f);

    lastUpdateMs = juce::Time::getMillisecondCounterHiRes();
}

//==============================================================================
void AnalysisHub::rebuildTiers()
{
    // Sizes, strictly increasing. Setting FFT Size to 16384 therefore gives two
    // tiers rather than a third that duplicates the second.
    std::vector<int> orders { requestedOrder };

    if (resolution == FXParams::SpectrumResolution::multi)
        for (auto order : FXParams::multiResolutionOrders)
            if (order > orders.back())
                orders.push_back (order);

    // Rebuilt rather than resized: the analysers hold transforms, windows and
    // band tables sized to one order, and there is no cheaper honest way to
    // change that than to make new ones.
    tiers.clear();
    boundaryHz.clear();

    for (size_t i = 0; i < orders.size(); ++i)
    {
        auto tier = std::make_unique<Tier>();
        tier->analyser = std::make_unique<SpectrumAnalyser>();
        tier->analyser->prepare (rate, orders[i]);
        tier->analyser->setOctaveSmoothing (octaveSmoothing);

        // There used to be a level constant here, one per tier, to make the
        // longer windows line up with the shortest. It is gone, and not because
        // the problem went away: SpectrumAnalyser now compensates for the band
        // width per bin against a fixed reference size, so every window already
        // reads the same level before anything here looks at it. Two
        // corrections for one effect would have put the long tiers twelve
        // decibels too high.
        //
        // The per-bin version also fixes what a constant could not. A constant
        // is right only where the smoothing band holds more than a bin or two,
        // and down in the bass it holds less than one - which is exactly where
        // the seam between tiers lives.
        // One window back, not at the present moment. The ring already holds
        // seconds of audio, and starting level with the write position means
        // the curve waits for a whole fresh window before it draws anything -
        // so changing FFT size or resolution blanks the display until new audio
        // arrives. Starting a window behind produces four frames on the very
        // first update, from audio that is already there.
        tier->nextAnalysisEnd = juce::jmax ((int64_t) 0,
                                            ring.getWritePosition() - tier->analyser->getFftSize());

        if (i + 1 < orders.size())
            boundaryHz.push_back (FXParams::tierCrossoverHz (rate, tier->analyser->getFftSize()));

        tiers.push_back (std::move (tier));
    }
}

AnalysisHub::TierChoice AnalysisHub::chooseTier (float hz) const noexcept
{
    TierChoice choice;

    // Walk down to the tier whose range this frequency falls in.
    while (choice.primary + 1 < getNumTiers() && hz < boundaryHz[(size_t) choice.primary])
        ++choice.primary;

    choice.secondary = choice.primary;

    // Inside a seam, mix the two either side of it. The boundaries are two
    // octaves apart and the blend is a third of one, so at most one seam can
    // ever be in range - no need to look for a second.
    for (int b = 0; b + 1 < getNumTiers(); ++b)
    {
        const auto distance = std::log2 (juce::jmax (1.0f, hz) / boundaryHz[(size_t) b]);

        if (std::abs (distance) < FXParams::tierBlendOctaves)
        {
            const auto t = 0.5f * (distance / FXParams::tierBlendOctaves + 1.0f);

            choice.primary         = b + 1;              // the longer window, below
            choice.secondary       = b;                  // the shorter one, above
            choice.secondaryWeight = t * t * (3.0f - 2.0f * t);
            break;
        }
    }

    return choice;
}

juce::String AnalysisHub::describeTiers() const
{
    juce::StringArray parts;

    for (const auto& tier : tiers)
        parts.add (juce::String (tier->analyser->getFftSize()));

    return parts.joinIntoString (" / ") + " pt";
}

void AnalysisHub::setInputGainDb (float db) noexcept
{
    inputGain.store (juce::Decibels::decibelsToGain (db, -100.0f), std::memory_order_relaxed);
}

void AnalysisHub::setFftOrder (int order)
{
    const juce::ScopedLock lock (analysisLock);

    const auto clamped = juce::jlimit (8, FXParams::maxSpectrumOrder, order);

    // Only if the size really changes. Rebuilding the tiers restarts every
    // schedule, and doing that unconditionally means any caller that re-applies
    // the current settings silently discards the analysis until new audio
    // arrives. EditorShot found this the moment it existed: it re-applies the
    // display settings before each shot, and the spectrum page rendered empty.
    if (clamped == requestedOrder)
        return;

    requestedOrder = clamped;
    rebuildTiers();
}

void AnalysisHub::setSpectrumResolution (FXParams::SpectrumResolution newResolution)
{
    const juce::ScopedLock lock (analysisLock);

    if (newResolution == resolution)
        return;

    resolution = newResolution;
    rebuildTiers();
}

void AnalysisHub::setSpectrumSmoothing (float fractionOfOctave)
{
    const juce::ScopedLock lock (analysisLock);

    octaveSmoothing = fractionOfOctave;

    for (auto& tier : tiers)
        tier->analyser->setOctaveSmoothing (fractionOfOctave);
}

void AnalysisHub::resetMeters()
{
    loudness.requestReset();
    truePeak.requestReset();

    for (auto& tier : tiers)
        tier->analyser->resetPeaks();
}

//==============================================================================
void AnalysisHub::pushBlock (const juce::AudioBuffer<float>& buffer) noexcept
{
    // Try, never wait. See the lock's declaration: the audio thread would
    // rather drop one block of analysis than block behind a reallocation.
    const juce::ScopedTryLock lock (analysisLock);

    if (! lock.isLocked())
        return;

    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || numSamples > scratch.getNumSamples())
        return;

    const auto inputChannels = buffer.getNumChannels();

    if (inputChannels <= 0)
        return;

    const auto* sourceLeft  = buffer.getReadPointer (0);
    const auto* sourceRight = buffer.getReadPointer (inputChannels > 1 ? 1 : 0);

    auto* analysis = scratch.getWritePointer (0);
    auto* left     = scratch.getWritePointer (1);
    auto* right    = scratch.getWritePointer (2);

    juce::FloatVectorOperations::copy (left,  sourceLeft,  numSamples);
    juce::FloatVectorOperations::copy (right, sourceRight, numSamples);

    if (dcBlockEnabled.load (std::memory_order_relaxed))
    {
        dcLeft.process  (left,  numSamples);
        dcRight.process (right, numSamples);
    }
    else
    {
        // Held clear rather than left holding whatever the last enabled block
        // put there, so that switching the blocker back on starts from a known
        // state instead of settling out of a stale one.
        dcLeft.reset();
        dcRight.reset();
    }

    const auto gain = inputGain.load (std::memory_order_relaxed);

    if (gain != 1.0f)
    {
        juce::FloatVectorOperations::multiply (left,  gain, numSamples);
        juce::FloatVectorOperations::multiply (right, gain, numSamples);
    }

    // The analysis channel. "L + R" and "Mid" are the same signal, and that is
    // not an oversight: the mono downmix of a stereo pair *is* its mid channel.
    // Both names are on the menu because they are the two different questions
    // people arrive with - "what does this sound like in mono" and "what is in
    // the middle" - and giving one of them a different scaling purely so the
    // two entries would differ would be inventing a distinction to justify a
    // menu item.
    const auto selected = (FXParams::Channel) channel.load (std::memory_order_relaxed);

    switch (selected)
    {
        case FXParams::Channel::left:
            juce::FloatVectorOperations::copy (analysis, left, numSamples);
            break;

        case FXParams::Channel::right:
            juce::FloatVectorOperations::copy (analysis, right, numSamples);
            break;

        case FXParams::Channel::side:
            for (int i = 0; i < numSamples; ++i)
                analysis[i] = 0.5f * (left[i] - right[i]);
            break;

        case FXParams::Channel::leftPlusRight:
        case FXParams::Channel::mid:
        default:
            for (int i = 0; i < numSamples; ++i)
                analysis[i] = 0.5f * (left[i] + right[i]);
            break;
    }

    // Loudness and true peak follow the channel selection, because a reading
    // labelled "Side" that silently measured the stereo pair would be a lie
    // told in a number. A stereo selection is measured as the pair, which is
    // what BS.1770 is defined over; the single-channel selections are measured
    // as mono, which is what it reduces to.
    const auto stereoSelection = selected == FXParams::Channel::leftPlusRight;

    if (stereoSelection)
    {
        const float* channels[] { left, right };
        loudness.processBlock (channels, 2, numSamples);
        truePeak.processBlock (channels, 2, numSamples);
    }
    else
    {
        const float* mono[]  { analysis };
        loudness.processBlock (mono, 1, numSamples);

        // True peak gets the same pointer twice rather than a single channel.
        // The peak of a signal against itself is that signal's peak, so the
        // answer is unchanged - but the oversampler was initialised for two
        // channels, and handing it one leaves the second holding whatever the
        // last stereo block upsampled into it. That stale channel is included
        // in the maximum, and it shows up as a true peak that refuses to fall
        // after switching from L + R to Side.
        const float* pair[] { analysis, analysis };
        truePeak.processBlock (pair, 2, numSamples);
    }

    // The stereo field is always measured on the real left and right pair. A
    // goniometer of "Left only" is a vertical line, and a correlation meter fed
    // one channel against itself reads +1 for ever - both are correct answers
    // to a question nobody asked.
    stereo.processBlock (left, right, numSamples, currentStereoWindowSeconds());

    const float* ringChannels[] { analysis, left, right };
    ring.write (ringChannels, 3, numSamples);
}

//==============================================================================
void AnalysisHub::advanceTier (Tier& tier, int64_t writePosition,
                               float smoothing, float attack, float hold)
{
    const auto fftSize = tier.analyser->getFftSize();
    const auto hop     = hopSamplesFor (fftSize);

    if ((int) window.size() < fftSize)
        return;

    // Behind by more than the ring can still hold: the message thread stalled
    // or audio jumped. Catching up frame by frame would transform data that has
    // already been overwritten, so the schedule resynchronises to the present.
    if (writePosition - tier.nextAnalysisEnd > ring.getCapacity() - fftSize)
        tier.nextAnalysisEnd = juce::jmax ((int64_t) 0, writePosition - fftSize);

    const auto hopSeconds = (float) ((double) hop / rate);

    // A ceiling on the work one update may do, so a long stall cannot become a
    // visible freeze while the display walks through a second of backlog.
    constexpr int maxFramesPerUpdate = 32;
    int framesThisUpdate = 0;

    while (tier.nextAnalysisEnd + hop <= writePosition && framesThisUpdate < maxFramesPerUpdate)
    {
        tier.nextAnalysisEnd += hop;
        ++framesThisUpdate;
        ++tier.frames;

        ring.read (ringAnalysis, tier.nextAnalysisEnd, window.data(), fftSize);
        tier.analyser->process (window.data(), smoothing, attack, hold, hopSeconds);
    }
}

//==============================================================================
void AnalysisHub::updateDisplays()
{
    const juce::ScopedLock lock (analysisLock);

    const auto now     = juce::Time::getMillisecondCounterHiRes();
    const auto delta   = (float) juce::jlimit (0.001, 0.5, (now - lastUpdateMs) / 1000.0);
    lastUpdateMs = now;

    if (frozen.load (std::memory_order_relaxed))
        return;

    const auto smoothing = currentSmoothingSeconds();
    const auto attack    = currentAttackSeconds();
    const auto hold      = currentHoldSeconds();

    // The spectrum and the scope run whichever page is open. They cost tens of
    // microseconds, and paying that unwatched is cheaper than the alternative,
    // which is a curve that has to build itself up from the floor every time
    // somebody switches back to the Spectrum tab.
    // ---- the spectrum, one schedule per tier -----------------------------
    //
    // Windows advance by a fixed quarter of a window - 75% overlap - regardless
    // of how often anybody looks, and each tier by a quarter of its own window.
    // Every sample is seen by exactly four transforms at every resolution, the
    // analysis rate follows the sample rate rather than the frame rate, and the
    // smoother's time constants mean what they say.
    const auto writePosition = ring.getWritePosition();

    for (auto& tier : tiers)
        advanceTier (*tier, writePosition, smoothing, attack, hold);

    scope.capture (ring, rate, scopeWindowSeconds, scopeTrigger);

    // YIN is the one analyser expensive enough to gate. It also runs at half
    // the frame rate: a tuner display that updates fifteen times a second is
    // already faster than anybody can turn a peg, and the halving is the
    // difference between a measurable and an unmeasurable CPU cost.
    if (activePage == FXParams::Page::pitch)
    {
        pitchFrameFlip ^= 1;

        if (pitchFrameFlip == 0)
        {
            const auto needed = pitch.getWindowSize();

            if ((int) window.size() >= needed)
            {
                ring.readLatest (ringAnalysis, window.data(), needed);
                pitch.process (window.data(), needed, currentPitchSmoothingSeconds(), delta * 2.0f);
            }
        }
    }
}
