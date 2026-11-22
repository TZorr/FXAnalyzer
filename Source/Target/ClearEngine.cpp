//
//  ClearEngine.cpp
//  FX Analyzer (copied from Clearbox - keep in sync, the TARGET page relies on identical numbers)
//

#include "ClearEngine.h"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

namespace
{
    constexpr float alpha = 0.23f;        // Zwicker's loudness exponent
    constexpr float tiny = 1.0e-12f;

    inline float dbToPower (float db) noexcept   { return std::pow (10.0f, 0.1f * db); }
    inline float powerToDb (float p) noexcept    { return 10.0f * std::log10 (p + tiny); }
    inline float clamp01 (float x) noexcept      { return std::clamp (x, 0.0f, 1.0f); }

    /** Specific loudness of energy e over threshold t (both SPL power), Zwicker's form. */
    inline float loud (float e, float t) noexcept
    {
        return std::pow (t, alpha) * (std::pow (0.5f + 0.5f * e / t, alpha) - 1.0f);
    }

    /** The energy whose loudness over threshold t is n - the inverse of loud(). */
    inline float energyForLoudness (float n, float t) noexcept
    {
        return t * (2.0f * std::pow (std::max (0.0f, n) / std::pow (t, alpha) + 1.0f, 1.0f / alpha) - 1.0f);
    }
}

ClearEngine::~ClearEngine()
{
    if (setup != nullptr)
        vDSP_destroy_fftsetup (setup);
}

float ClearEngine::erbNumber (float hz) noexcept   { return 21.4f * std::log10 (1.0f + 0.00437f * hz); }
float ClearEngine::erbToHz (float erb) noexcept    { return (std::pow (10.0f, erb / 21.4f) - 1.0f) / 0.00437f; }

//  Terhardt's approximation of the threshold in quiet, capped: above ~17 kHz
//  the formula climbs past anything a loudspeaker plays, and a cap keeps the
//  top band from reading as permanently inaudible.
float ClearEngine::thresholdInQuietDb (float hz, float capDb, float capHighDb) noexcept
{
    const auto f = std::max (hz, 20.0f) / 1000.0f;
    const auto db = 3.64f * std::pow (f, -0.8f) - 6.5f * std::exp (-0.6f * (f - 3.3f) * (f - 3.3f)) + 1.0e-3f * f * f * f * f;
    return std::min (db, hz > 4000.0f ? capHighDb : capDb);
}

float ClearEngine::lowEndDb (float lowestDb, float nextDb, float bandsBelow) noexcept
{
    const auto extra = std::clamp (0.5f * bandsBelow * (nextDb - lowestDb), -6.0f, 6.0f);
    return lowestDb + extra;
}

float ClearEngine::topFade (float hz) noexcept
{
    const auto t = std::clamp (std::log (std::max (hz, 1.0f) / 14500.0f) / std::log (18200.0f / 14500.0f), 0.0f, 1.0f);
    return 0.5f * (1.0f + std::cos ((float) M_PI * t));
}

float ClearEngine::targetShapeDb (float hz) noexcept
{
    // Read off the user's SPAN Plus screenshot (20 Hz ... 20 kHz), linear over log f.
    static constexpr float table[][2] {
        { 20.0f, -13.3f }, { 30.0f, -9.5f }, { 40.0f, -6.2f }, { 50.0f, -4.3f }, { 60.0f, -3.2f }, { 80.0f, -1.8f },
        { 100.0f, -1.1f }, { 200.0f, -0.3f }, { 300.0f, 0.0f }, { 2000.0f, 0.0f }, { 3000.0f, -0.4f }, { 4000.0f, -0.8f },
        { 5000.0f, -1.2f }, { 6000.0f, -1.8f }, { 8000.0f, -2.9f }, { 10000.0f, -4.3f }, { 15000.0f, -8.4f }, { 20000.0f, -12.2f } };
    constexpr int n = (int) (sizeof (table) / sizeof (table[0]));

    if (hz <= table[0][0])     return table[0][1];
    if (hz >= table[n - 1][0]) return table[n - 1][1];

    int i = 1;
    while (table[i][0] < hz)
        ++i;

    const auto t = std::log (hz / table[i - 1][0]) / std::log (table[i][0] / table[i - 1][0]);
    return table[i - 1][1] + t * (table[i][1] - table[i - 1][1]);
}

void ClearEngine::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;

    // Same bin width at every rate: 1024 points at 44.1/48 kHz, 2048 at 88.2/96, 4096 above.
    fftSize = 1024;
    log2Size = 10;
    while (sampleRate / fftSize > 48000.0 / 1024.0 * 1.5)
    {
        fftSize *= 2;
        ++log2Size;
    }

    hop = fftSize / 4;
    numBins = fftSize / 2 + 1;

    if (setup != nullptr)
        vDSP_destroy_fftsetup (setup);
    setup = vDSP_create_fftsetup ((vDSP_Length) log2Size, kFFTRadix2);

    window.resize ((size_t) fftSize);
    for (int n = 0; n < fftSize; ++n)
        window[(size_t) n] = std::sqrt (0.5f - 0.5f * std::cos (2.0f * (float) M_PI * (float) n / (float) fftSize));

    // vDSP's real FFT out and back is 2N; sqrt-Hann twice at a quarter hop sums to 2.
    synthScale = 1.0f / (4.0f * (float) fftSize);

    inFifo.assign ((size_t) fftSize * 2, 0.0f);     // left, then right
    outAccum.assign ((size_t) fftSize * 2, 0.0f);
    frame.assign ((size_t) fftSize, 0.0f);
    re.assign ((size_t) fftSize, 0.0f);              // left half-spectrum, then right
    im.assign ((size_t) fftSize, 0.0f);
    power.assign ((size_t) numBins, 0.0f);
    binGain.assign ((size_t) numBins, 1.0f);

    // Calibration: a full-scale 1 kHz sine is tuning.fullScaleSplDb.
    {
        for (int n = 0; n < fftSize; ++n)
            frame[(size_t) n] = window[(size_t) n] * std::sin (2.0f * (float) M_PI * 1000.0f * (float) n / (float) sampleRate);

        DSPSplitComplex z { re.data(), im.data() };
        vDSP_ctoz ((const DSPComplex*) frame.data(), 2, &z, 1, (vDSP_Length) (fftSize / 2));
        vDSP_fft_zrip (setup, &z, 1, (vDSP_Length) log2Size, FFT_FORWARD);

        double sum = 0.0;
        for (int k = 1; k < fftSize / 2; ++k)
            sum += (double) re[(size_t) k] * re[(size_t) k] + (double) im[(size_t) k] * im[(size_t) k];

        powerToSpl = (float) (std::pow (10.0, tuning.fullScaleSplDb / 10.0) / sum);
    }

    // Bands: one per ERB from the first bin (or 20 Hz) to 20 kHz (or 0.45 fs).
    const auto binHz = (float) sampleRate / (float) fftSize;
    const auto zLo = erbNumber (std::max (20.0f, binHz));
    const auto zHi = erbNumber (std::min (20000.0f, 0.45f * (float) sampleRate));
    numBands = std::clamp ((int) std::floor (zHi - zLo) + 1, 2, maxBands);
    const auto spacing = (zHi - zLo) / (float) (numBands - 1);

    for (int b = 0; b < numBands; ++b)
    {
        bandErb[(size_t) b] = zLo + spacing * (float) b;
        bandHz[(size_t) b] = erbToHz (bandErb[(size_t) b]);
        topWeight[(size_t) b] = topFade (bandHz[(size_t) b]);

        // The measurement's release: 3 s at 50 Hz and below, 1.5 s at 10 kHz and above.
        const auto t = std::clamp (std::log (bandHz[(size_t) b] / 50.0f) / std::log (10000.0f / 50.0f), 0.0f, 1.0f);
        tauSeconds[(size_t) b] = 3.0f + t * (1.5f - 3.0f);
        thr[(size_t) b] = dbToPower (thresholdInQuietDb (bandHz[(size_t) b], tuning.quietCapDb, tuning.quietCapHighDb));
    }

    binLow.assign ((size_t) numBins, 0);
    binFrac.assign ((size_t) numBins, 0.0f);

    for (int k = 0; k < numBins; ++k)
    {
        const auto pos = (erbNumber ((float) k * binHz) - zLo) / spacing;

        if (pos <= 0.0f)                       { binLow[(size_t) k] = 0;              binFrac[(size_t) k] = pos; }   // below: extrapolated
        else if (pos >= (float) (numBands - 1)) { binLow[(size_t) k] = numBands - 2;   binFrac[(size_t) k] = 1.0f; }
        else                                   { binLow[(size_t) k] = (int) pos;      binFrac[(size_t) k] = pos - std::floor (pos); }
    }

    // Each band's width in Hz, as its bin weights add up (for the level per Hz the
    // TARGET view needs), and its share of the log-frequency axis (so the target
    // is matched per octave, the way SPAN shows it, not per ERB).
    bandWidthHz.fill (0.0f);
    for (int k = 1; k < fftSize / 2; ++k)
    {
        const auto f = std::max (0.0f, binFrac[(size_t) k]);
        bandWidthHz[(size_t) binLow[(size_t) k]] += (1.0f - f) * binHz;
        bandWidthHz[(size_t) (binLow[(size_t) k] + 1)] += f * binHz;
    }

    for (int b = 0; b < numBands; ++b)
    {
        const auto lo = std::log2 (bandHz[(size_t) std::max (0, b - 1)]);
        const auto hi = std::log2 (bandHz[(size_t) std::min (numBands - 1, b + 1)]);
        octaveWeight[(size_t) b] = 0.5f * (hi - lo);
        bandWidthHz[(size_t) b] = std::max (bandWidthHz[(size_t) b], 0.5f * binHz);
    }

    // Masking spread: from band `from` onto band `to`, as a power ratio.
    for (int to = 0; to < numBands; ++to)
        for (int from = 0; from < numBands; ++from)
        {
            const auto dz = bandErb[(size_t) to] - bandErb[(size_t) from];
            const auto slope = dz >= 0.0f ? tuning.slopeUpDb : tuning.slopeDownDb;
            spread[(size_t) (to * maxBands + from)] = dbToPower (-slope * std::abs (dz));
        }

    const auto frameSeconds = (float) hop / (float) sampleRate;
    energyCoeff  = std::exp (-frameSeconds / (0.001f * tuning.energyTauMs));
    attackCoeff  = std::exp (-frameSeconds / (0.001f * tuning.attackTauMs));
    releaseCoeff = std::exp (-frameSeconds / (0.001f * tuning.releaseTauMs));

    resonance.prepare (sampleRate, fftSize, hop);

    reset();
}

void ClearEngine::reset()
{
    resonance.reset();
    std::fill (inFifo.begin(), inFifo.end(), 0.0f);
    std::fill (outAccum.begin(), outAccum.end(), 0.0f);
    std::fill (binGain.begin(), binGain.end(), 1.0f);
    fifoCount = 0;

    energy.fill (0.0f);
    smoothEnergy.fill (0.0f);
    midEnergy.fill (0.0f);
    slowDb.fill (-100.0f);
    warmSeconds = 0.0f;
    measured = false;
    holding.store (false, std::memory_order_relaxed);
    normOffsetDb = 0.0f;
    gainDb.fill (0.0f);
    heldTarget.fill (0.0f);
    smoothGain.fill (0.0f);

    for (int b = 0; b < maxBands; ++b)
    {
        displayGain[(size_t) b].store (0.0f, std::memory_order_relaxed);
        displayLevel[(size_t) b].store (-100.0f, std::memory_order_relaxed);
        displayDeviation[(size_t) b].store (0.0f, std::memory_order_relaxed);
        displayConfidence[(size_t) b].store (0.0f, std::memory_order_relaxed);
    }
}

void ClearEngine::process (float* left, float* right, int numSamples, const Settings& settings) noexcept
{
    const auto mono = right == nullptr || right == left;
    auto* inL = inFifo.data();
    auto* inR = inFifo.data() + fftSize;
    auto* outL = outAccum.data();
    auto* outR = outAccum.data() + fftSize;
    const auto writeAt = fftSize - hop;

    for (int i = 0; i < numSamples; ++i)
    {
        inL[writeAt + fifoCount] = left[i];
        left[i] = outL[fifoCount];

        if (! mono)
        {
            inR[writeAt + fifoCount] = right[i];
            right[i] = outR[fifoCount];
        }

        // The bass notches of the resonance stage, on the output itself.
        if (resonance.bassActive())
            resonance.filterBass (left[i], mono ? nullptr : right + i);

        if (++fifoCount == hop)
        {
            fifoCount = 0;

            if (mono)
                std::copy (inL, inL + fftSize, inR);

            processFrame (settings);
        }
    }
}

void ClearEngine::processFrame (const Settings& settings) noexcept
{
    const auto half = fftSize / 2;

    // Analysis, both channels.
    for (int c = 0; c < 2; ++c)
    {
        vDSP_vmul (inFifo.data() + c * fftSize, 1, window.data(), 1, frame.data(), 1, (vDSP_Length) fftSize);
        DSPSplitComplex z { re.data() + c * half, im.data() + c * half };
        vDSP_ctoz ((const DSPComplex*) frame.data(), 2, &z, 1, (vDSP_Length) half);
        vDSP_fft_zrip (setup, &z, 1, (vDSP_Length) log2Size, FFT_FORWARD);
    }

    // Linked: the mean power of the two channels. Bin 0 holds DC and Nyquist; neither is heard.
    power[0] = 0.0f;
    power[(size_t) half] = 0.0f;
    for (int k = 1; k < half; ++k)
    {
        const auto l = re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k];
        const auto r = re[(size_t) (half + k)] * re[(size_t) (half + k)] + im[(size_t) (half + k)] * im[(size_t) (half + k)];
        power[(size_t) k] = 0.5f * (l + r) * powerToSpl;
    }

    computeBandGains (settings);

    // The resonance stage: the newest hop into its longer analysis, then its cuts.
    resonance.push (inFifo.data() + fftSize - hop, inFifo.data() + 2 * fftSize - hop, hop);
    const auto resonating = resonance.analyse (settings.resonance, settings.sensitivity, ! holding.load (std::memory_order_relaxed));

    // Band gains to every bin, interpolated in dB along the ERB axis, plus the resonance cuts.
    for (int k = 0; k <= half; ++k)
    {
        const auto lo = binLow[(size_t) k];
        const auto frac = binFrac[(size_t) k];
        // Below the lowest band: the curve carried on at half its slope.
        auto db = frac < 0.0f ? lowEndDb (smoothGain[0], smoothGain[1], frac)
                              : smoothGain[(size_t) lo] + frac * (smoothGain[(size_t) (lo + 1)] - smoothGain[(size_t) lo]);
        if (resonating)
            db += resonance.mainBinDb (k);
        binGain[(size_t) k] = db == 0.0f ? 1.0f : std::pow (10.0f, 0.05f * db);
    }

    // Shift out the hop just played, then synthesis, both channels.
    for (int c = 0; c < 2; ++c)
    {
        auto* acc = outAccum.data() + c * fftSize;
        std::copy (acc + hop, acc + fftSize, acc);
        std::fill (acc + fftSize - hop, acc + fftSize, 0.0f);

        auto* r = re.data() + c * half;
        auto* m = im.data() + c * half;
        r[0] *= binGain[0];
        m[0] *= binGain[(size_t) half];   // Nyquist, packed

        for (int k = 1; k < half; ++k)
        {
            r[k] *= binGain[(size_t) k];
            m[k] *= binGain[(size_t) k];
        }

        DSPSplitComplex z { r, m };
        vDSP_fft_zrip (setup, &z, 1, (vDSP_Length) log2Size, FFT_INVERSE);
        vDSP_ztoc (&z, 1, (DSPComplex*) frame.data(), 2, (vDSP_Length) half);

        for (int n = 0; n < fftSize; ++n)
            acc[n] += frame[(size_t) n] * window[(size_t) n] * synthScale;

        auto* in = inFifo.data() + c * fftSize;
        std::copy (in + hop, in + fftSize, in);
    }
}

float ClearEngine::loudnessSum (const float* gain, float offsetDb) const noexcept
{
    std::array<float, maxBands> e {};
    for (int j = 0; j < numBands; ++j)
        e[(size_t) j] = smoothEnergy[(size_t) j] * dbToPower (gain[j] + topWeight[(size_t) j] * offsetDb);

    float total = 0.0f;
    for (int b = 0; b < numBands; ++b)
    {
        float ex = 0.0f;
        for (int j = 0; j < numBands; ++j)
            ex += spread[(size_t) (b * maxBands + j)] * e[(size_t) j];

        total += loud (ex, thr[(size_t) b]);
    }

    return total;
}

void ClearEngine::computeBandGains (const Settings& s) noexcept
{
    const auto B = numBands;

    // Band energies.
    energy.fill (0.0f);
    for (int k = 1; k < fftSize / 2; ++k)
    {
        const auto lo = binLow[(size_t) k];
        const auto f = std::max (0.0f, binFrac[(size_t) k]);
        energy[(size_t) lo] += (1.0f - f) * power[(size_t) k];
        energy[(size_t) (lo + 1)] += f * power[(size_t) k];
    }

    // The measurement. Power over 0.2 s first (enough frames that its dB value is
    // unbiased); the 30 ms energy feeds the loudness sum.
    const auto frameSeconds = (float) hop / (float) sampleRate;
    const auto midCoeff = std::exp (-frameSeconds / 0.2f);

    float total = 0.0f;
    for (int b = 0; b < B; ++b)
    {
        smoothEnergy[(size_t) b] = energyCoeff * smoothEnergy[(size_t) b] + (1.0f - energyCoeff) * energy[(size_t) b];
        midEnergy[(size_t) b] = midCoeff * midEnergy[(size_t) b] + (1.0f - midCoeff) * energy[(size_t) b];
        total += smoothEnergy[(size_t) b];
    }

    // Global gate: is there enough signal to learn from? 0 below -60 dBFS, 1 above -50,
    // smooth between, on the 30 ms energy so it closes within a few hundred ms of a stop
    // (a gap between notes does not get that low). It scales every update below, so in
    // silence or a reverb tail the measurement - and with it the curve - simply holds.
    const auto levelDbfs = powerToDb (total) - tuning.fullScaleSplDb;
    const auto x = clamp01 ((levelDbfs + 60.0f) / 10.0f);
    const auto gate = x * x * (3.0f - 2.0f * x);
    holding.store (gate < 0.5f, std::memory_order_relaxed);

    // Then in dB, with an attack and a release per band: a rise is followed in half the
    // time a fall is, so what gets too loud is trimmed promptly and a gap is filled only
    // slowly. In dB, because a power average forgets a loud past far more slowly than it
    // notices a loud present. The times grow from 0.25 s after a reset (counting only
    // time with signal): a running mean, not a fade-in from silence.
    warmSeconds = std::min (warmSeconds + frameSeconds * gate, 1.0e6f);

    for (int b = 0; b < B; ++b)
    {
        const auto db = powerToDb (midEnergy[(size_t) b]);

        if (! measured)
        {
            slowDb[(size_t) b] = db;
            continue;
        }

        const auto release = std::clamp (warmSeconds, 0.25f, tauSeconds[(size_t) b]);
        const auto tau = db > slowDb[(size_t) b] ? 0.5f * release : release;
        const auto step = (1.0f - std::exp (-frameSeconds / tau)) * gate;
        slowDb[(size_t) b] += step * (db - slowDb[(size_t) b]);
    }

    measured = measured || gate > 0.5f;
    const auto slowCoeff = std::exp (-frameSeconds / std::clamp (warmSeconds, 0.25f, 2.0f));

    // The view: level per Hz with a 4.5 dB/oct slope, against the (tilted) target.
    std::array<float, maxBands> view {}, aim {}, audible {}, confidence {};
    for (int b = 0; b < B; ++b)
    {
        const auto f = bandHz[(size_t) b];
        view[(size_t) b] = slowDb[(size_t) b] - 10.0f * std::log10 (bandWidthHz[(size_t) b]) + targetSlopeDb * std::log2 (f / 1000.0f);
        aim[(size_t) b] = targetShapeDb (f) + s.brighten * tuning.brightenDbPerOct * std::log2 (f / 1000.0f);
        audible[(size_t) b] = clamp01 ((slowDb[(size_t) b] - powerToDb (thr[(size_t) b]) - tuning.quietGateDb) / 10.0f);
        confidence[(size_t) b] = 1.0f;
    }

    // The reference: the mean distance to the curve, octave-weighted over the bands
    // loud enough to hear - twice: the second time without the bands the first one
    // found missing (confidence), so an empty bass does not drag it down and make
    // everything else look too loud.
    const auto meanDistance = [&]
    {
        float wSum = 0.0f, dSum = 0.0f;
        for (int b = 0; b < B; ++b)
        {
            const auto w = octaveWeight[(size_t) b] * topWeight[(size_t) b] * audible[(size_t) b] * confidence[(size_t) b];
            wSum += w;
            dSum += w * (view[(size_t) b] - aim[(size_t) b]);
        }
        return std::make_pair (wSum, wSum > 1.0e-4f ? dSum / wSum : 0.0f);
    };

    auto [wSum, offset] = meanDistance();
    for (int pass = 0; pass < 2; ++pass)
    {
        // Confidence: 1 down to 12 dB below the curve, 0 from 24 dB below - missing, not quiet.
        for (int b = 0; b < B; ++b)
        {
            const auto below = -(view[(size_t) b] - offset - aim[(size_t) b]);
            const auto t = clamp01 ((24.0f - below) / 12.0f);
            confidence[(size_t) b] = t * t * (3.0f - 2.0f * t);
        }

        std::tie (wSum, offset) = meanDistance();
    }

    for (int b = 0; b < B; ++b)
    {
        displayLevel[(size_t) b].store (view[(size_t) b] - offset, std::memory_order_relaxed);

        // For the analyzer: against the pure target - Brighten tilts what Lift and Trim
        // aim for, not what the curve is.
        const auto f = bandHz[(size_t) b];
        const auto tilt = s.brighten * tuning.brightenDbPerOct * std::log2 (f / 1000.0f);
        displayDeviation[(size_t) b].store (view[(size_t) b] - offset - (aim[(size_t) b] - tilt), std::memory_order_relaxed);
        displayConfidence[(size_t) b].store (confidence[(size_t) b], std::memory_order_relaxed);
    }

    const auto active = s.lift > 0.0f || s.trim > 0.0f || s.boostDb != 0.0f;
    std::array<float, maxBands> target {};

    if (! active)
        normOffsetDb = 0.0f;   // so turning a knob up again starts from level ground

    if (active && wSum > 1.0e-4f)
    {
        const auto tolerance = tuning.toleranceMaxDb * std::max (0.0f, s.bias);

        // Boost: the loudness ratio a listener Boost dB quieter would hear overall ...
        std::array<float, maxBands> boostDb {};
        if (s.boostDb != 0.0f)
        {
            const auto shift = dbToPower (-s.boostDb * tuning.boostScale);
            float before = 0.0f, after = 0.0f;
            for (int b = 0; b < B; ++b)
            {
                const auto e = smoothEnergy[(size_t) b];
                before += topWeight[(size_t) b] * loud (e, thr[(size_t) b]);
                after  += topWeight[(size_t) b] * loud (e * shift, thr[(size_t) b]);
            }

            // ... and per band the gain that gives each band that same ratio at that level.
            if (before > tiny)
            {
                const auto ratio = after / before;
                for (int b = 0; b < B; ++b)
                {
                    const auto e = smoothEnergy[(size_t) b] + tiny;
                    const auto needed = energyForLoudness (ratio * loud (e, thr[(size_t) b]), thr[(size_t) b]);
                    const auto g = std::clamp (powerToDb (needed / (e * shift)), -24.0f, 24.0f);
                    const auto heard = clamp01 ((powerToDb (e / thr[(size_t) b]) - tuning.quietGateDb) / 10.0f);
                    boostDb[(size_t) b] = g * heard;
                }
            }
        }

        // The distance to the curve: Trim takes off what is above, Lift makes up
        // what is below - gated by confidence, so a missing band is left missing, and
        // never for a band too quiet to hear, where it would only lift noise.
        for (int b = 0; b < B; ++b)
        {
            const auto d = view[(size_t) b] - offset - aim[(size_t) b];
            const auto cut = -s.trim * std::max (0.0f, d - tolerance);
            const auto lift = s.lift * std::max (0.0f, -d - tolerance) * audible[(size_t) b] * confidence[(size_t) b];
            gainDb[(size_t) b] = topWeight[(size_t) b] * std::clamp (cut + lift + boostDb[(size_t) b], tuning.maxCutDb, tuning.maxLiftDb);
        }

        // Loudness normalisation: one offset, found by secant steps, and then
        // followed at the measurement's 2 s - a faster offset over slow gains
        // overshoots whenever the spectrum changes.
        const auto zeroGain = std::array<float, maxBands> {};
        const auto targetLoudness = loudnessSum (zeroGain.data(), 0.0f);

        if (targetLoudness > tiny)
        {
            const auto miss = [&] (float o) { return std::log (std::max (loudnessSum (gainDb.data(), o), tiny) / targetLoudness); };
            float o0 = 0.0f, f0 = miss (o0);
            float o1 = -f0 / (alpha * std::log (10.0f) / 10.0f), f1 = 0.0f;

            for (int step = 0; step < 4; ++step)
            {
                o1 = std::clamp (o1, -24.0f, 24.0f);
                f1 = miss (o1);
                if (std::abs (f1) < 1.0e-4f || std::abs (f1 - f0) < 1.0e-9f)
                    break;

                const auto next = o1 - f1 * (o1 - o0) / (f1 - f0);
                o0 = o1; f0 = f1; o1 = next;
            }

            normOffsetDb += (1.0f - slowCoeff) * gate * (std::clamp (o1, -24.0f, 24.0f) - normOffsetDb);

            for (int b = 0; b < B; ++b)
                target[(size_t) b] = std::clamp (gainDb[(size_t) b] + topWeight[(size_t) b] * normOffsetDb,
                                                 tuning.maxCutDb, tuning.maxLiftDb);
        }
    }

    // Holding: while there is too little signal the curve stays where it was, rather
    // than relaxing - so when the music comes back it meets the curve it left.
    if (active && gate < 0.5f && measured)
        target = heldTarget;
    else
        heldTarget = target;

    // Time: down at the attack rate, up at the release rate. Back at 0 it is exactly 0,
    // so a curve that has settled flat is a wire again.
    for (int b = 0; b < B; ++b)
    {
        auto& g = smoothGain[(size_t) b];
        const auto t = target[(size_t) b];
        const auto c = t < g ? attackCoeff : releaseCoeff;
        g = c * g + (1.0f - c) * t;

        if (! active && std::abs (g) < 1.0e-3f)
            g = 0.0f;

        displayGain[(size_t) b].store (g, std::memory_order_relaxed);
    }
}
