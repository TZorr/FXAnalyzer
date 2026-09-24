//
//  PitchDetector.h
//  FX Analyzer
//
//  YIN, in the time domain, because the frequency domain cannot answer this
//  question at the bottom of the piano.
//
//  The obvious implementation is to take the FFT that the Spectrum page has
//  already computed and read off its largest peak. It is wrong twice. A 4096
//  point transform at 48 kHz has bins 11.7 Hz apart, and the gap between the
//  two lowest notes on a bass guitar is 2.4 Hz - the transform cannot tell them
//  apart at all. And the largest peak of a real instrument is very often not
//  the fundamental: an open E on a bass puts more energy in its second harmonic
//  than in its first, so an FFT tuner reports the octave and the player spends
//  ten minutes wondering why the string is not going flat.
//
//  YIN answers "what period does this waveform repeat over", which is the
//  question a tuner is being asked, and it answers it identically for a sine
//  and for a sawtooth of the same pitch.
//
//  It costs a squared-difference sum per candidate period - a few million
//  operations for a window covering A0. That is why this only runs while the
//  Pitch page is on screen, and why it runs at half the panel's frame rate.
//  Both are decisions the AnalysisHub makes; this class just does the work when
//  asked.
//
//  Two departures from the paper are worth naming. The absolute threshold is
//  0.15 as published, but a candidate is only accepted if it is also a local
//  minimum, which stops a noisy window latching onto the first tau that happens
//  to dip. And the reported frequency is smoothed only when the new estimate is
//  within a semitone of the previous one - inside a note that steadies the
//  cents readout, and between notes it lets the display jump rather than glide
//  through every pitch in between, which is what a smoother without that test
//  does and it looks like the instrument is playing a portamento it did not.
//

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

class PitchDetector
{
public:
    PitchDetector() = default;

    /** Allocates for the lowest note the detector is asked to find.
        Message thread only. */
    void prepare (double sampleRate, float lowestHz, float highestHz);

    /** Number of samples the window needs. The caller reads exactly this many
        from the capture ring. */
    int getWindowSize() const noexcept { return windowSize + maxTau; }

    /** Runs one estimate. Not real-time safe in the sense that matters here -
        it is not allocating, but it is slow enough that it belongs on the
        message thread. */
    void process (const float* samples, int numSamples, float smoothingSeconds, float deltaSeconds);

    void reset();

    struct Result
    {
        float frequencyHz = 0.0f;
        float confidence  = 0.0f;   ///< 1 - the YIN dip depth: 1 is a perfect period
        float levelDb     = -200.0f;
        bool  valid       = false;
    };

    Result getResult() const noexcept { return result; }

private:
    double rate = 48000.0;

    int windowSize = 2048;
    int minTau     = 12;
    int maxTau     = 2048;

    std::vector<float> difference;      // d(tau)
    std::vector<float> cumulative;      // d'(tau), the cumulative mean normalised difference

    Result result;
    float smoothedFrequency = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchDetector)
};

//==============================================================================
/** Note naming, kept next to the detector because the two are only ever used
    together and a note name computed from a different reference pitch than the
    detector used is a bug nobody spots. */
struct NoteName
{
    juce::String name;       ///< "A", "C#" - sharps only; a tuner has no key signature to work from
    int          octave = 4; ///< scientific pitch notation, A440 is A4
    float        cents  = 0.0f;
    int          midiNote = 69;

    static NoteName fromFrequency (float frequencyHz, float referenceHz);
};
