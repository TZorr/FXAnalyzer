//
//  PitchDetector.cpp
//  FX Analyzer
//

#include "PitchDetector.h"

#include <cmath>

//==============================================================================
void PitchDetector::prepare (double sampleRate, float lowestHz, float highestHz)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;

    maxTau = (int) std::ceil (rate / (double) juce::jmax (10.0f, lowestHz));
    minTau = juce::jmax (2, (int) std::floor (rate / (double) juce::jmax (20.0f, highestHz)));

    // The analysis window is one full lowest-period long, so that even the
    // longest candidate period is compared against a whole cycle rather than
    // against a fragment. Shorter windows are cheaper and report the low notes
    // an octave high.
    windowSize = maxTau;

    difference.assign ((size_t) maxTau + 1, 0.0f);
    cumulative.assign ((size_t) maxTau + 1, 0.0f);

    reset();
}

void PitchDetector::reset()
{
    result = Result{};
    smoothedFrequency = 0.0f;
}

//==============================================================================
void PitchDetector::process (const float* samples, int numSamples,
                             float smoothingSeconds, float deltaSeconds)
{
    const auto required = getWindowSize();

    if (samples == nullptr || numSamples < required)
        return;

    // Level first, and it is a gate rather than a display nicety. Below about
    // -60 dBFS the difference function is measuring the noise floor, and YIN
    // will confidently return the period of whatever hum is down there.
    double sumSquares = 0.0;

    for (int i = 0; i < required; ++i)
        sumSquares += (double) samples[i] * (double) samples[i];

    const auto rms     = std::sqrt (sumSquares / (double) required);
    const auto levelDb = rms > 0.0 ? (float) (20.0 * std::log10 (rms)) : -200.0f;

    Result fresh;
    fresh.levelDb = levelDb;

    if (levelDb < -60.0f)
    {
        result = fresh;
        smoothedFrequency = 0.0f;
        return;
    }

    // Step 1: the squared difference function.
    difference[0] = 0.0f;

    for (int tau = 1; tau <= maxTau; ++tau)
    {
        double sum = 0.0;

        for (int i = 0; i < windowSize; ++i)
        {
            const double delta = (double) samples[i] - (double) samples[i + tau];
            sum += delta * delta;
        }

        difference[(size_t) tau] = (float) sum;
    }

    // Step 2: cumulative mean normalisation. This is what turns the difference
    // function into something with an absolute threshold: without it, d(tau)
    // simply grows with tau and there is no level at which "small" means
    // anything.
    cumulative[0] = 1.0f;
    double runningSum = 0.0;

    for (int tau = 1; tau <= maxTau; ++tau)
    {
        runningSum += (double) difference[(size_t) tau];

        cumulative[(size_t) tau] = runningSum > 0.0
            ? (float) ((double) difference[(size_t) tau] * (double) tau / runningSum)
            : 1.0f;
    }

    // Step 3: the first dip below the absolute threshold that is also a local
    // minimum. Taking the global minimum instead is the classic mistake - it
    // finds the octave below whenever the waveform is close to symmetric.
    constexpr float absoluteThreshold = 0.15f;

    int bestTau = -1;

    for (int tau = juce::jmax (2, minTau); tau < maxTau; ++tau)
    {
        if (cumulative[(size_t) tau] < absoluteThreshold
            && cumulative[(size_t) tau] <= cumulative[(size_t) tau - 1]
            && cumulative[(size_t) tau] <= cumulative[(size_t) tau + 1])
        {
            bestTau = tau;
            break;
        }
    }

    // Nothing crossed the threshold: fall back to the smallest dip in range,
    // and let the confidence number say how little it should be believed.
    if (bestTau < 0)
    {
        float smallest = 1.0f;

        for (int tau = juce::jmax (2, minTau); tau < maxTau; ++tau)
        {
            if (cumulative[(size_t) tau] < smallest)
            {
                smallest = cumulative[(size_t) tau];
                bestTau  = tau;
            }
        }
    }

    if (bestTau <= 0)
    {
        result = fresh;
        return;
    }

    // Step 4: parabolic interpolation on the normalised curve. Without it the
    // period is an integer number of samples, which at 1 kHz means the reading
    // can only ever be 1000.0 or 1020.4 Hz - 35 cents apart, on a display whose
    // whole purpose is to show single cents.
    float refinedTau = (float) bestTau;

    if (bestTau > 0 && bestTau < maxTau)
    {
        const auto left   = cumulative[(size_t) bestTau - 1];
        const auto centre = cumulative[(size_t) bestTau];
        const auto right  = cumulative[(size_t) bestTau + 1];

        const auto denominator = left - 2.0f * centre + right;

        if (std::abs (denominator) > 1.0e-12f)
            refinedTau += juce::jlimit (-0.5f, 0.5f, 0.5f * (left - right) / denominator);
    }

    fresh.frequencyHz = (float) (rate / (double) refinedTau);
    fresh.confidence  = juce::jlimit (0.0f, 1.0f, 1.0f - cumulative[(size_t) bestTau]);
    fresh.valid       = fresh.frequencyHz > 0.0f;

    // Step 5: smooth inside a note, jump between notes.
    if (fresh.valid)
    {
        const auto tau   = juce::jmax (1.0e-3f, smoothingSeconds);
        const auto alpha = 1.0f - std::exp (-juce::jmax (0.0f, deltaSeconds) / tau);

        const auto withinASemitone = smoothedFrequency > 0.0f
            && std::abs (std::log2 (fresh.frequencyHz / smoothedFrequency)) < (1.0f / 12.0f);

        smoothedFrequency = withinASemitone
            ? smoothedFrequency + (fresh.frequencyHz - smoothedFrequency) * alpha
            : fresh.frequencyHz;

        fresh.frequencyHz = smoothedFrequency;
    }
    else
    {
        smoothedFrequency = 0.0f;
    }

    result = fresh;
}

//==============================================================================
NoteName NoteName::fromFrequency (float frequencyHz, float referenceHz)
{
    NoteName note;

    if (frequencyHz <= 0.0f || referenceHz <= 0.0f)
        return note;

    static const char* const names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    const auto exactMidi = 69.0 + 12.0 * std::log2 ((double) frequencyHz / (double) referenceHz);
    const auto nearest   = (int) std::lround (exactMidi);

    note.midiNote = juce::jlimit (0, 127, nearest);
    note.cents    = (float) ((exactMidi - (double) nearest) * 100.0);
    note.name     = names[((note.midiNote % 12) + 12) % 12];

    // Scientific pitch notation: MIDI 60 is C4, and the octave changes at C.
    note.octave = note.midiNote / 12 - 1;

    return note;
}
