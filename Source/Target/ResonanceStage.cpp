//
//  ResonanceStage.cpp
//  FX Analyzer (copied from Clearbox - keep in sync, the TARGET page relies on identical numbers)
//

#include "ResonanceStage.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float minSigma = 1.0f / 24.0f, maxSigma = 1.0f / 6.0f;   // a notch's width, in octaves
    constexpr float maxDepthDb = 18.0f;
    constexpr float crossLowHz = 250.0f, crossHighHz = 350.0f;
}

float ResonanceStage::gridHz (int i) noexcept
{
    return 20.0f * std::pow (1000.0f, (float) i / (float) (gridPoints - 1));
}

float ResonanceStage::highShare (float hz) noexcept
{
    const auto t = std::clamp (std::log2 (std::max (hz, 1.0f) / crossLowHz) / std::log2 (crossHighHz / crossLowHz), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

//==============================================================================
ResonanceStage::Analysis::~Analysis()
{
    if (setup != nullptr)
        vDSP_destroy_fftsetup (setup);
}

void ResonanceStage::Analysis::prepare (double newSampleRate, int newSize, float lowHz, float highHz,
                                        float averageSeconds, float hopSeconds)
{
    sampleRate = newSampleRate;
    size = newSize;
    log2Size = (int) std::lround (std::log2 ((double) size));
    half = size / 2;
    binHz = (float) sampleRate / (float) size;

    if (setup != nullptr)
        vDSP_destroy_fftsetup (setup);
    setup = vDSP_create_fftsetup ((vDSP_Length) log2Size, kFFTRadix2);

    window.resize ((size_t) size);
    for (int n = 0; n < size; ++n)
        window[(size_t) n] = 0.5f - 0.5f * std::cos (2.0f * (float) M_PI * (float) n / (float) size);

    frame.assign ((size_t) size, 0.0f);
    re.assign ((size_t) half, 0.0f);
    im.assign ((size_t) half, 0.0f);
    powerDb.assign ((size_t) half, -300.0f);
    averagePower.assign ((size_t) half, 0.0f);
    reference.assign ((size_t) half, 0.0f);
    targetDb.assign ((size_t) half, 0.0f);
    cutDb.assign ((size_t) half, 0.0f);
    log2Hz.assign ((size_t) half, 0.0f);
    prefix.assign ((size_t) half + 1, 0.0);
    refLo.assign ((size_t) half, 1);
    refHi.assign ((size_t) half, 1);

    for (int l = 0; l < half; ++l)
    {
        log2Hz[(size_t) l] = std::log2 ((float) std::max (l, 1) * binHz);
        refLo[(size_t) l] = std::clamp (std::min (l - 8, (int) std::floor ((float) l * std::pow (2.0f, -1.0f / 3.0f))), 1, half - 1);
        refHi[(size_t) l] = std::clamp (std::max (l + 8, (int) std::ceil ((float) l * std::pow (2.0f, 1.0f / 3.0f))), 1, half - 1);
    }

    lowBin = std::max (2, (int) std::ceil (lowHz / binHz));
    highBin = std::min (half - 2, (int) std::floor (highHz / binHz));
    averageCoeff = std::exp (-hopSeconds / averageSeconds);

    // A bin's floor: -100 dB under a full-scale sine's peak bin, so nothing is taken
    // for a resonance in the noise floor.
    const auto k = (float) std::round (1000.0f / binHz);
    for (int n = 0; n < size; ++n)
        frame[(size_t) n] = window[(size_t) n] * std::sin (2.0f * (float) M_PI * k * (float) n / (float) size);
    DSPSplitComplex z { re.data(), im.data() };
    vDSP_ctoz ((const DSPComplex*) frame.data(), 2, &z, 1, (vDSP_Length) half);
    vDSP_fft_zrip (setup, &z, 1, (vDSP_Length) log2Size, FFT_FORWARD);
    floorDb = 10.0f * std::log10 (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]) - 100.0f;

    reset();
}

void ResonanceStage::Analysis::reset()
{
    std::fill (averagePower.begin(), averagePower.end(), 0.0f);
    std::fill (cutDb.begin(), cutDb.end(), 0.0f);
}

float ResonanceStage::Analysis::run (const std::vector<float>& samples, int newest, float amount, float threshold,
                                     bool listening, float attack, float release) noexcept
{
    // The newest `size` samples of the history ring.
    const auto ring = (int) samples.size();
    const auto start = (newest - size + ring) % ring;
    for (int n = 0; n < size; ++n)
        frame[(size_t) n] = samples[(size_t) ((start + n) % ring)] * window[(size_t) n];

    DSPSplitComplex z { re.data(), im.data() };
    vDSP_ctoz ((const DSPComplex*) frame.data(), 2, &z, 1, (vDSP_Length) half);
    vDSP_fft_zrip (setup, &z, 1, (vDSP_Length) log2Size, FFT_FORWARD);

    // Power per bin, averaged over time.
    powerDb[0] = -300.0f;
    averagePower[0] = 0.0f;
    for (int l = 1; l < half; ++l)
    {
        const auto p = re[(size_t) l] * re[(size_t) l] + im[(size_t) l] * im[(size_t) l];
        auto& a = averagePower[(size_t) l];
        a = averageCoeff * a + (1.0f - averageCoeff) * p;
        powerDb[(size_t) l] = 10.0f * std::log10 (a + 1.0e-30f);
    }

    // The neighbourhood: the mean power over +-1/3 octave (at least +-8 bins), without
    // the peak's own +-1/12 octave (at least +-2 bins) - in a power mean, not a dB
    // mean, which sits 2.5 dB low on noise.
    const auto first = std::max (1, refLo[(size_t) lowBin]), last = std::min (half - 1, refHi[(size_t) highBin]);
    prefix[(size_t) first] = 0.0;
    for (int l = first; l <= last; ++l)
        prefix[(size_t) l + 1] = prefix[(size_t) l] + (double) averagePower[(size_t) l];

    for (int l = lowBin; l <= highBin; ++l)
    {
        const auto lo = refLo[(size_t) l], hi = refHi[(size_t) l];
        const auto core = std::max (2, (int) std::ceil ((float) l * (std::pow (2.0f, 1.0f / 12.0f) - 1.0f)));
        const auto coreLo = std::max (lo, l - core), coreHi = std::min (hi, l + core);
        const auto sum = (prefix[(size_t) hi + 1] - prefix[(size_t) lo]) - (prefix[(size_t) coreHi + 1] - prefix[(size_t) coreLo]);
        const auto count = (hi - lo + 1) - (coreHi - coreLo + 1);
        reference[(size_t) l] = count > 0 ? (float) (10.0 * std::log10 (sum / count + 1.0e-30)) : powerDb[(size_t) l];
    }

    // Peaks above the threshold get a Gaussian notch, as wide as the resonance and,
    // at Resonance 100 %, as deep as it stands out, with a 3 dB soft knee. Where
    // notches overlap the deepest counts: a broad resonance has several local maxima.
    std::fill (targetDb.begin(), targetDb.end(), 0.0f);

    if (listening && amount > 0.0f)
        for (int l = lowBin; l <= highBin; ++l)
        {
            const auto p = powerDb[(size_t) l];
            const auto prominence = p - reference[(size_t) l];
            if (prominence < threshold || p <= floorDb || p < powerDb[(size_t) l - 1] || p <= powerDb[(size_t) l + 1])
                continue;

            const auto x = std::min (1.0f, (prominence - threshold) / 3.0f);
            const auto depth = std::min (maxDepthDb, amount * prominence * x * x * (3.0f - 2.0f * x));
            const auto centre = log2Hz[(size_t) l];

            // Its width: where it has fallen to half its prominence on either side.
            const auto halfHeight = reference[(size_t) l] + 0.5f * prominence;
            auto left = l, right = l;
            while (left > lowBin && powerDb[(size_t) left - 1] > halfHeight && centre - log2Hz[(size_t) left - 1] < 0.5f)
                --left;
            while (right < highBin && powerDb[(size_t) right + 1] > halfHeight && log2Hz[(size_t) right + 1] - centre < 0.5f)
                ++right;
            const auto widthOctaves = log2Hz[(size_t) right] - log2Hz[(size_t) left] + (log2Hz[(size_t) l + 1] - log2Hz[(size_t) l]);
            const auto sigma = std::clamp (widthOctaves / 2.355f, minSigma, maxSigma);
            const auto spread = 4.0f * sigma;

            for (int m = l; m >= 1 && centre - log2Hz[(size_t) m] <= spread; --m)
            {
                const auto d = (log2Hz[(size_t) m] - centre) / sigma;
                targetDb[(size_t) m] = std::min (targetDb[(size_t) m], -depth * std::exp (-0.5f * d * d));
            }
            for (int m = l + 1; m < half && log2Hz[(size_t) m] - centre <= spread; ++m)
            {
                const auto d = (log2Hz[(size_t) m] - centre) / sigma;
                targetDb[(size_t) m] = std::min (targetDb[(size_t) m], -depth * std::exp (-0.5f * d * d));
            }
        }

    // Over time: in fast, out slowly - this carries a peak from frame to frame.
    auto deepest = 0.0f;
    for (int l = 1; l < half; ++l)
    {
        auto& c = cutDb[(size_t) l];
        const auto t = std::max (targetDb[(size_t) l], -24.0f);
        const auto k = t < c ? attack : release;
        c = k * c + (1.0f - k) * t;
        if (c > -1.0e-4f)
            c = 0.0f;
        deepest = std::min (deepest, c);
    }

    return deepest;
}

float ResonanceStage::Analysis::cutAt (float hz) const noexcept
{
    const auto pos = std::clamp (hz / binHz, 1.0f, (float) (half - 2));
    const auto l = (int) pos;
    const auto f = pos - (float) l;
    return cutDb[(size_t) l] + f * (cutDb[(size_t) l + 1] - cutDb[(size_t) l]);
}

//==============================================================================
void ResonanceStage::prepare (double newSampleRate, int mainFftSize, int hop)
{
    sampleRate = newSampleRate;
    mainFft = mainFftSize;
    mainBins = mainFftSize / 2 + 1;

    const auto hopSeconds = (float) hop / (float) sampleRate;
    wide.prepare (sampleRate, 4 * mainFftSize, 200.0f, 16000.0f, 0.060f, hopSeconds);
    bass.prepare (sampleRate, 8 * mainFftSize, 30.0f, 400.0f, 0.150f, hopSeconds);

    history.assign ((size_t) (8 * mainFftSize), 0.0f);
    mainDb.assign ((size_t) mainBins, 0.0f);
    mainFrom.assign ((size_t) mainBins, 1);
    mainTo.assign ((size_t) mainBins, 1);
    for (int k = 1; k < mainBins; ++k)
    {
        mainFrom[(size_t) k] = std::max (1, k * 4 - 2);
        mainTo[(size_t) k] = std::min (wide.half - 1, k * 4 + 2);
    }

    attackCoeff = std::exp (-hopSeconds / 0.010f);
    releaseCoeff = std::exp (-hopSeconds / 0.150f);
    glideCoeff = std::exp (-hopSeconds / 0.020f);

    reset();
}

void ResonanceStage::reset()
{
    std::fill (history.begin(), history.end(), 0.0f);
    std::fill (mainDb.begin(), mainDb.end(), 0.0f);
    wide.reset();
    bass.reset();
    writePos = 0;
    cutting = false;
    bassOn = false;
    notches = {};

    for (auto& d : displayDb)
        d.store (0.0f, std::memory_order_relaxed);
}

void ResonanceStage::push (const float* left, const float* right, int count) noexcept
{
    const auto ring = (int) history.size();
    for (int i = 0; i < count; ++i)
    {
        history[(size_t) writePos] = 0.5f * (left[i] + right[i]);
        if (++writePos == ring)
            writePos = 0;
    }
}

bool ResonanceStage::analyse (float amount, float sensitivity, bool listening) noexcept
{
    if (amount <= 0.0f && ! cutting)
        return false;

    const auto threshold = thresholdDb (sensitivity);
    const auto deepest = std::min (wide.run (history, writePos, amount, threshold, listening, attackCoeff, releaseCoeff),
                                   bass.run (history, writePos, amount, threshold, listening, attackCoeff, releaseCoeff));
    cutting = deepest < -0.01f;

    updateBassNotches();

    if (! cutting)
    {
        notches = {};
        bassOn = false;
        wide.reset();
        bass.reset();
        std::fill (mainDb.begin(), mainDb.end(), 0.0f);
        for (auto& d : displayDb)
            d.store (0.0f, std::memory_order_relaxed);
        return false;
    }

    // Above the bass, onto the main bins: the wide analysis's notch averaged in dB
    // over the bins under each main bin, weighted by their power.
    auto any = false;
    mainDb[0] = 0.0f;
    for (int k = 1; k < mainBins; ++k)
    {
        double weight = 0.0, weighted = 0.0, plain = 0.0;
        for (int l = mainFrom[(size_t) k]; l <= mainTo[(size_t) k]; ++l)
        {
            const auto share = highShare ((float) l * wide.binHz);
            const auto power = (double) wide.averagePower[(size_t) l];
            weight += power;
            weighted += power * wide.cutDb[(size_t) l] * share;
            plain += wide.cutDb[(size_t) l] * share;
        }
        const auto n = mainTo[(size_t) k] - mainFrom[(size_t) k] + 1;
        mainDb[(size_t) k] = weight > 1.0e-30 ? (float) (weighted / weight) : (float) (plain / n);
        any = any || mainDb[(size_t) k] < -1.0e-4f;
    }

    // The panel: the bass analysis below the crossover, the wide one above.
    for (int i = 0; i < gridPoints; ++i)
    {
        const auto hz = gridHz (i);
        const auto share = highShare (hz);
        displayDb[(size_t) i].store (share * wide.cutAt (hz) + (1.0f - share) * bass.cutAt (hz), std::memory_order_relaxed);
    }

    return any;
}

//  The bass: the deepest dips of the bass analysis's cut below the crossover become
//  notch filters. A dip near an existing notch moves that notch; a new one takes a
//  free or the weakest one; a notch without a dip lets go at the release rate.
//  Frequency and Q glide over 20 ms, so a moving resonance is followed.
void ResonanceStage::updateBassNotches() noexcept
{
    struct Dip { float log2Hz, depth, q; };
    std::array<Dip, numNotches> found {};
    int count = 0;

    const auto& a = bass;
    const auto top = std::min (a.highBin, (int) std::ceil (crossHighHz / a.binHz));
    const auto cutAtBin = [&] (int l) { return a.cutDb[(size_t) l] * (1.0f - highShare ((float) l * a.binHz)); };

    for (int l = a.lowBin + 1; l < top; ++l)
    {
        const auto c = cutAtBin (l), prev = cutAtBin (l - 1), next = cutAtBin (l + 1);
        if (c > -0.3f || c > prev || c >= next)
            continue;

        // The centre between bins (a parabola through the three), and the width
        // where the dip is half as deep: Q = centre / width.
        const auto denom = prev - 2.0f * c + next;
        const auto shift = std::abs (denom) > 1.0e-6f ? std::clamp (0.5f * (prev - next) / denom, -0.5f, 0.5f) : 0.0f;
        const auto hz = ((float) l + shift) * a.binHz;

        auto left = l, right = l;
        while (left > 1 && a.cutDb[(size_t) left - 1] < 0.5f * c) --left;
        while (right < a.half - 2 && a.cutDb[(size_t) right + 1] < 0.5f * c) ++right;
        const auto width = std::max ((float) (right - left + 1) * a.binHz, 2.0f);

        const Dip d { std::log2 (hz), -c, std::clamp (hz / width, 1.5f, 30.0f) };

        if (count < numNotches)
            found[(size_t) count++] = d;
        else
        {
            auto weakest = 0;
            for (int i = 1; i < numNotches; ++i)
                if (found[(size_t) i].depth < found[(size_t) weakest].depth) weakest = i;
            if (d.depth > found[(size_t) weakest].depth)
                found[(size_t) weakest] = d;
        }
    }

    std::array<bool, numNotches> taken {};
    std::array<Dip, numNotches> aim {};

    for (int i = 0; i < count; ++i)
    {
        const auto& d = found[(size_t) i];
        auto best = -1;
        for (int s = 0; s < numNotches; ++s)
        {
            const auto& n = notches[(size_t) s];
            if (! taken[(size_t) s] && n.depthDb > 0.05f && std::abs (n.log2Hz - d.log2Hz) < 1.0f / 6.0f
                && (best < 0 || std::abs (n.log2Hz - d.log2Hz) < std::abs (notches[(size_t) best].log2Hz - d.log2Hz)))
                best = s;
        }

        if (best < 0)   // a new resonance: a free notch, or the weakest
        {
            for (int s = 0; s < numNotches; ++s)
                if (! taken[(size_t) s] && (best < 0 || notches[(size_t) s].depthDb < notches[(size_t) best].depthDb))
                    best = s;
            if (best < 0)
                continue;
            if (notches[(size_t) best].depthDb <= 0.05f)
            {
                notches[(size_t) best].log2Hz = d.log2Hz;   // a silent notch jumps straight there
                notches[(size_t) best].q = d.q;
            }
        }

        taken[(size_t) best] = true;
        aim[(size_t) best] = d;
    }

    bassOn = false;
    for (int s = 0; s < numNotches; ++s)
    {
        auto& n = notches[(size_t) s];
        if (taken[(size_t) s])
        {
            n.log2Hz = aim[(size_t) s].log2Hz + (n.log2Hz - aim[(size_t) s].log2Hz) * glideCoeff;
            n.q = aim[(size_t) s].q + (n.q - aim[(size_t) s].q) * glideCoeff;
            n.depthDb = aim[(size_t) s].depth;   // the cut curve is smoothed in time already
        }
        else
            n.depthDb *= releaseCoeff;

        if (n.depthDb < 0.01f)
        {
            n.depthDb = 0.0f;
            n.l1 = n.l2 = n.r1 = n.r2 = 0.0;
            continue;
        }

        bassOn = true;

        // RBJ peaking filter with a negative gain: a cut of depthDb at log2Hz, Q q.
        const auto f = std::clamp ((double) std::exp2 (n.log2Hz), 10.0, 0.45 * sampleRate);
        const auto A = std::pow (10.0, -n.depthDb / 40.0);
        const auto w = 2.0 * M_PI * f / sampleRate;
        const auto alpha = std::sin (w) / (2.0 * n.q);
        const auto a0 = 1.0 + alpha / A;
        n.b0 = (1.0 + alpha * A) / a0;
        n.b1 = -2.0 * std::cos (w) / a0;
        n.b2 = (1.0 - alpha * A) / a0;
        n.a1 = n.b1;
        n.a2 = (1.0 - alpha / A) / a0;
    }
}

void ResonanceStage::filterBass (float& left, float* right) noexcept
{
    for (auto& n : notches)
    {
        if (n.depthDb <= 0.0f)
            continue;

        const double x = left, y = n.b0 * x + n.l1;
        n.l1 = n.b1 * x - n.a1 * y + n.l2;
        n.l2 = n.b2 * x - n.a2 * y;
        left = (float) y;

        if (right != nullptr)
        {
            const double xr = *right, yr = n.b0 * xr + n.r1;
            n.r1 = n.b1 * xr - n.a1 * yr + n.r2;
            n.r2 = n.b2 * xr - n.a2 * yr;
            *right = (float) yr;
        }
    }
}
