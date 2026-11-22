//
//  ClearEngine.h
//  FX Analyzer (copied from Clearbox - keep in sync, the TARGET page relies on identical numbers)
//
//  A target-curve dynamic EQ: it measures the spectrum over about two seconds
//  and pulls it towards a rounded 4.5 dB/oct curve - lifting what is below,
//  trimming what is above - while the overall loudness stays where it was.
//
//  1. STFT, sqrt-Hann analysis and synthesis, four-fold overlap. 1024 points at
//     44.1/48 kHz (21 ms latency), scaled with the sample rate so the frequency
//     resolution stays the same.
//  2. Band energies on the ERB-number scale (Glasberg & Moore), one band per
//     ERB, triangular weights so the bands add up to every bin exactly once.
//     Levels are calibrated as SPL: a full-scale sine is 94 dB.
//  3. The measurement: each band's level per Hz, power-averaged over 0.2 s and
//     then in dB with an attack and a release per band - a rise is followed in
//     half the time a fall is, and the bass is averaged longer than the top
//     (release 3 s at 50 Hz down to 1.5 s at 10 kHz) - seen with a 4.5 dB/oct
//     slope as SPAN Plus shows it. A global gate holds it all while there is
//     too little signal (below about -55 dBFS): silence and reverb tails do
//     not teach it anything.
//  4. The decision: that view against the target (targetShapeDb), tilted by
//     Brighten, matched per octave, with a dead zone set by Bias. Trim cuts
//     what is above the curve, Lift raises what is below - 100 % the whole
//     distance. A confidence gate sits between the deviation and the gain: a
//     band more than 12 dB below the curve fades out of the reference and out
//     of Lift's reach, and at 24 dB below it counts as missing - a passage
//     without bass is left without bass rather than filled with noise. Lift
//     also never raises a band far below the threshold of hearing (Terhardt).
//  5. Boost: the balance as it would be heard Boost dB quieter (positive) or
//     louder, put back - an equal-loudness correction that follows the signal.
//  6. Loudness normalisation: one offset (Zwicker's loudness, bands spread onto
//     their neighbours) so the total loudness is what came in, following at
//     the measurement's 2 s. A/B is fair by construction.
//  7. Time smoothing on the gains (faster down than up), and the band gains
//     interpolated to every bin - the curve is smooth in frequency, so the STFT
//     filter does not ring.
//  8. The ends: above 14.5 kHz the curve fades to 0 dB by 18.2 kHz, so air is
//     never lifted; below the lowest band it carries on at half its slope.
//  9. The resonance stage (ResonanceStage.h): narrow, quick cuts on ringing
//     peaks, from its own longer analysis, added to every bin's gain. It stays
//     out of the loudness normalisation; at Resonance 0 it is not there at all.
//
//  Stereo is linked: one curve from the power of both channels. Real-time
//  safe: everything is allocated in prepare().
//
//  Plain C++ and Accelerate, no JUCE, so the checks link it as is.
//

#pragma once

#include <Accelerate/Accelerate.h>

#include "ResonanceStage.h"

#include <array>
#include <atomic>
#include <vector>

class ClearEngine
{
public:
    static constexpr int maxBands = 48;

    struct Settings
    {
        float lift     = 0.0f;      // 0 ... 1: share of the distance below the curve made up
        float trim     = 0.0f;      // 0 ... 1: share of the distance above it taken off
        float bias     = 0.0f;      // -1 ... 1: above 0 a dead zone
        float brighten = 0.0f;      // -1 ... 1: tilts the target by up to 1.5 dB/oct
        float boostDb  = 0.0f;      // -50 ... 50: loudness contour
        float resonance   = 0.0f;   // 0 ... 1: how far ringing peaks are brought down to the threshold
        float sensitivity = 0.5f;   // 0 ... 1: threshold 12 dB (0) ... 3 dB (1) of prominence
    };

    /** The model's constants. Public so a harness can move them. */
    struct Tuning
    {
        float toleranceMaxDb = 6.0f;    // dead zone at Bias +100 %; none at 0 and below
        float brightenDbPerOct = 1.5f;  // target tilt at Brighten +-100 %
        float slopeUpDb      = 30.0f;   // spread towards higher bands for the loudness sum, dB per ERB
        float slopeDownDb    = 36.0f;   // ... towards lower bands
        float quietGateDb    = -26.0f;  // level re threshold in quiet where Lift starts
        float energyTauMs    = 30.0f;
        float attackTauMs    = 30.0f;   // gain moving down
        float releaseTauMs   = 40.0f;   // gain moving up
        float maxCutDb       = -24.0f;
        float maxLiftDb      = 18.0f;
        float fullScaleSplDb = 94.0f;   // a full-scale sine heard at this level
        float quietCapDb     = 75.0f;   // threshold in quiet never above this ...
        float quietCapHighDb = 10.0f;   // ... and above 4 kHz never above this
        float boostScale     = 1.9f;    // Boost dB as heard-level shift
    };

    ClearEngine() = default;
    ~ClearEngine();
    ClearEngine (const ClearEngine&) = delete;
    ClearEngine& operator= (const ClearEngine&) = delete;

    /** Also re-reads `tuning` (time constants and spreading are baked here). */
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** In place. Either pointer may be the same buffer (mono). */
    void process (float* left, float* right, int numSamples, const Settings&) noexcept;

    int getLatencySamples() const noexcept  { return fftSize; }
    int getNumBands() const noexcept         { return numBands; }
    float getBandFrequency (int b) const noexcept { return bandHz[(size_t) b]; }

    /** The curve last applied, per band in dB; written by the audio thread, read by the panel. */
    float getDisplayGainDb (int b) const noexcept { return displayGain[(size_t) b].load (std::memory_order_relaxed); }
    /** Band level in the 4.5 dB/oct view relative to the target (0 = on the curve),
        for the panel's faint input trace. */
    float getDisplayLevelDb (int b) const noexcept { return displayLevel[(size_t) b].load (std::memory_order_relaxed); }

    /** How far each band lies from the pure target curve (dB; above 0 = too loud),
        whatever the knobs, and how sure the confidence gate is that the band is
        there at all (0 ... 1) - for the analyzer. */
    float getDisplayDeviationDb (int b) const noexcept { return displayDeviation[(size_t) b].load (std::memory_order_relaxed); }
    float getDisplayConfidence (int b) const noexcept  { return displayConfidence[(size_t) b].load (std::memory_order_relaxed); }

    /** The resonance stage's cuts for the panel, on its own log grid. */
    const ResonanceStage& getResonance() const noexcept { return resonance; }

    /** True while there is too little signal to measure (silence, a reverb tail):
        the measurement and the curve are frozen until the music comes back. */
    bool isHolding() const noexcept { return holding.load (std::memory_order_relaxed); }

    Tuning tuning;

    // ---- the model's pieces, public for the checks ----
    static float erbNumber (float hz) noexcept;
    static float erbToHz (float erb) noexcept;
    static float thresholdInQuietDb (float hz, float capDb = 70.0f, float capHighDb = 30.0f) noexcept;

    /** Below the lowest band the curve carries on at half its slope there, limited
        to 6 dB beyond the band's own gain. `bandsBelow` is negative: how many band
        spacings under the lowest band. The panel draws its left end with this too. */
    static float lowEndDb (float lowestDb, float nextDb, float bandsBelow) noexcept;

    /** 1 up to 14.5 kHz, 0 from 18.2 kHz, a raised cosine on log frequency between:
        air is never lifted. */
    static float topFade (float hz) noexcept;

    /** The target curve, dB relative to its plateau, as SPAN Plus shows it with a
        4.5 dB/oct slope: flat from 300 Hz to 2 kHz, rounded off at both ends.
        As a real spectrum it falls at 4.5 dB/oct (power per Hz). */
    static float targetShapeDb (float hz) noexcept;

    /** The slope the target view adds, dB per octave re 1 kHz. */
    static constexpr float targetSlopeDb = 4.5f;

private:
    void processFrame (const Settings&) noexcept;
    void computeBandGains (const Settings&) noexcept;
    float loudnessSum (const float* gainDb, float offsetDb) const noexcept;

    double sampleRate = 48000.0;
    int fftSize = 1024, log2Size = 10, hop = 256, numBins = 513;
    int numBands = 0;

    FFTSetup setup = nullptr;
    std::vector<float> window, inFifo, outAccum, frame, re, im, power, binGain;
    int fifoCount = 0;
    float synthScale = 1.0f, powerToSpl = 1.0f;

    // bins -> bands: each bin sits between two band centres
    std::vector<int> binLow;
    std::vector<float> binFrac;

    std::array<float, maxBands> bandHz {}, bandErb {}, topWeight {}, bandWidthHz {}, octaveWeight {}, midEnergy {}, slowDb {}, thr {}, energy {}, smoothEnergy {}, gainDb {}, smoothGain {};
    std::array<float, maxBands * maxBands> spread {};   // spread[to * maxBands + from], power ratio
    std::array<std::atomic<float>, maxBands> displayGain {}, displayLevel {}, displayDeviation {}, displayConfidence {};
    float warmSeconds = 0.0f;    // signal time since reset, for the measurement's warm-up
    bool measured = false;       // the measurement has started (from the first frame with signal)
    std::array<float, maxBands> tauSeconds {};   // per band: the release time of the measurement
    std::array<float, maxBands> heldTarget {};   // the curve last decided while there was signal
    std::atomic<bool> holding { false };
    float normOffsetDb = 0.0f;   // the loudness offset applied, following at 2 s

    float energyCoeff = 0, attackCoeff = 0, releaseCoeff = 0;

    ResonanceStage resonance;
};
