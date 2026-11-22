//
//  ResonanceStage.h
//  FX Analyzer (copied from Clearbox - keep in sync, the TARGET page relies on identical numbers)
//
//  Tames ringing peaks: narrow resonances that stand out of their
//  neighbourhood get a narrow, short-lived cut - in tens of milliseconds,
//  where the target curve works over seconds.
//
//  The idea follows DyERS (Thiago Lobato, AGPL): find prominent spectral peaks
//  and lay a Gaussian notch on each, on a log-frequency axis. This is an
//  independent implementation with these differences:
//
//  - Its own, longer analyses, so peaks are found where they are, while the
//    latency does not grow:
//    - above the bass: 4x the main FFT (4096 points at 48 kHz, 11.7 Hz
//      apart), power averaged over ~60 ms;
//    - the bass (30 ... 400 Hz): 8x (8192 points, 5.9 Hz apart), averaged
//      over ~150 ms - bass resonances ring longer, and noise fluctuates more
//      in narrow bins.
//    A single frame of noise has random peaks of 7 dB and more; the averaging
//    is what a resonance has to stand out from.
//  - Prominence is measured against the mean power of the neighbourhood over
//    +-1/3 octave (at least 8 bins), without the peak's own +-1/12 octave, so
//    its skirts do not raise its own reference. It is in dB and independent
//    of level.
//  - Sensitivity decides what counts as a peak (3 ... 12 dB of prominence).
//    Resonance decides how flat it is made: 100 % flattens it into its
//    neighbourhood, with a 3 dB soft knee above the threshold. There is no
//    single fixed depth.
//  - Each notch is as wide as its resonance, measured at half its prominence
//    (1/24 ... 1/6 octave), where DyERS has one Sharpness for all. Where
//    notches overlap, the deepest counts.
//  - Each bin has an attack (~10 ms) and a release (~150 ms). This carries a
//    peak from frame to frame instead of letting it flutter.
//  - How the cut is applied:
//    - Above ~300 Hz it goes into the main STFT's bins, averaged in dB and
//      weighted by power: a resonance carrying most of a bin's energy cuts
//      that bin nearly as deep as its own notch.
//    - Below ~300 Hz the main STFT is too coarse (47 Hz bins, smeared over
//      +-2 of them), so up to three real notch filters (RBJ biquads) take
//      over, on the deepest dips found there, gliding as they move.
//    - 250 ... 350 Hz cross over.
//
//  Plain C++ and Accelerate, no JUCE. Allocates only in prepare().
//

#pragma once

#include <Accelerate/Accelerate.h>

#include <array>
#include <atomic>
#include <vector>

class ResonanceStage
{
public:
    static constexpr int gridPoints = 256;   // the cut curve for the panel, 20 Hz ... 20 kHz on log f
    static constexpr int numNotches = 3;

    ResonanceStage() = default;
    ResonanceStage (const ResonanceStage&) = delete;
    ResonanceStage& operator= (const ResonanceStage&) = delete;

    /** mainFftSize and hop are the main STFT's. */
    void prepare (double sampleRate, int mainFftSize, int hop);
    void reset();

    /** The newest `hop` samples of each channel (right may equal left). */
    void push (const float* left, const float* right, int count) noexcept;

    /** Once per main frame. amount 0 ... 1 (1 flattens a peak into its neighbourhood),
        sensitivity 0 ... 1; `listening` false while the global gate holds (nothing new
        is learnt, cuts let go). Returns true while the main bins have a cut to apply. */
    bool analyse (float amount, float sensitivity, bool listening) noexcept;

    /** The cut for main bin k in dB (0 or below), valid after analyse(). */
    float mainBinDb (int k) const noexcept { return mainDb[(size_t) k]; }

    /** The bass notches on one output sample of each channel (right may be null). */
    void filterBass (float& left, float* right) noexcept;
    bool bassActive() const noexcept { return bassOn; }

    /** The cut curve for the panel. */
    float getDisplayDb (int i) const noexcept { return displayDb[(size_t) i].load (std::memory_order_relaxed); }
    static float gridHz (int i) noexcept;

    /** The threshold Sensitivity stands for, in dB of prominence. */
    static float thresholdDb (float sensitivity) noexcept { return 12.0f - 9.0f * sensitivity; }

private:
    /** One analysis: FFT, averaged power, neighbourhood, peaks, notches, cut over time. */
    struct Analysis
    {
        Analysis() = default;
        ~Analysis();
        Analysis (const Analysis&) = delete;
        Analysis& operator= (const Analysis&) = delete;

        void prepare (double sampleRate, int size, float lowHz, float highHz, float averageSeconds, float hopSeconds);
        void reset();
        /** Returns the deepest cut (dB, <= 0). */
        float run (const std::vector<float>& samples, int newest, float amount, float threshold, bool listening,
                   float attack, float release) noexcept;
        float cutAt (float hz) const noexcept;

        double sampleRate = 48000.0;
        int size = 4096, log2Size = 12, half = 2048, lowBin = 2, highBin = 2000;
        float binHz = 11.7f, averageCoeff = 0.0f, floorDb = -200.0f;
        FFTSetup setup = nullptr;
        std::vector<float> window, frame, re, im, powerDb, averagePower, reference, targetDb, cutDb, log2Hz;
        std::vector<double> prefix;
        std::vector<int> refLo, refHi;
    };

    struct Notch
    {
        float log2Hz = 0.0f, depthDb = 0.0f, q = 4.0f;
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double l1 = 0, l2 = 0, r1 = 0, r2 = 0;
    };

    void updateBassNotches() noexcept;
    static float highShare (float hz) noexcept;   // the STFT's share of a cut: 0 below 250 Hz, 1 above 350 Hz

    double sampleRate = 48000.0;
    int mainBins = 513, mainFft = 1024;
    Analysis wide, bass;
    std::vector<float> history;                 // as long as the longest analysis
    int writePos = 0;

    std::vector<float> mainDb;
    std::vector<int> mainFrom, mainTo;          // the wide analysis's bins under each main bin
    std::array<Notch, numNotches> notches {};
    std::array<std::atomic<float>, gridPoints> displayDb {};

    float attackCoeff = 0.0f, releaseCoeff = 0.0f, glideCoeff = 0.0f;
    bool cutting = false, bassOn = false;
};
