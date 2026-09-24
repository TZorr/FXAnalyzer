//
//  SpectrumAnalyser.cpp
//  FX Analyzer
//

#include "SpectrumAnalyser.h"
#include "ParameterIds.h"

#include <cmath>

//==============================================================================
void SpectrumAnalyser::prepare (double sampleRate, int fftOrder)
{
    rate  = sampleRate > 0.0 ? sampleRate : 48000.0;
    order = juce::jlimit (8, FXParams::maxSpectrumOrder, fftOrder);
    size  = 1 << order;

    allocate();
    reset();
}

void SpectrumAnalyser::setFftOrder (int fftOrder)
{
    const auto clamped = juce::jlimit (8, FXParams::maxSpectrumOrder, fftOrder);

    if (clamped == order)
        return;

    order = clamped;
    size  = 1 << order;

    allocate();
    reset();
}

void SpectrumAnalyser::allocate()
{
    fft    = std::make_unique<juce::dsp::FFT> (order);
    window = std::make_unique<juce::dsp::WindowingFunction<float>> (
                 (size_t) size, juce::dsp::WindowingFunction<float>::hann, false);

    fftData.assign ((size_t) size * 2, 0.0f);

    const auto bins = (size_t) getNumBins();
    instantDb.assign      (bins, floorDb);
    smoothedDb.assign     (bins, floorDb);
    peakDb.assign         (bins, floorDb);
    peakAgeSeconds.assign (bins, 0.0f);
    displayDb.assign      (bins, floorDb);
    displayPeakDb.assign  (bins, floorDb);
    scratchDb.assign      (bins, floorDb);
    powerPrefix.assign    (bins + 1, 0.0);

    rebuildBands();

    // Hann's coherent gain is exactly 0.5 for the periodic definition, but
    // JUCE's symmetric window is a sample short of periodic and its mean is
    // 0.5 * N/(N-1). At 4096 points that is 0.001 dB, which nobody would ever
    // see - it is computed rather than assumed because the same code has to
    // hold at 256 points, where it is 0.017 dB, and because a constant nobody
    // can derive is a constant nobody can check.
    std::vector<float> unity ((size_t) size, 1.0f);
    window->multiplyWithWindowingTable (unity.data(), (size_t) size);

    double sum = 0.0;

    for (auto value : unity)
        sum += (double) value;

    const double coherentGain = sum / (double) size;

    // 2, not 4. performFrequencyOnlyForwardTransform has already folded the
    // negative-frequency half into the bins it returns, so the mirrored energy
    // is present and does not need putting back. Deriving the factor as
    // 4/(N*CG) from the two-sided transform - which is what the textbook says -
    // reads every sine exactly 6.02 dB high, and 6 dB is small enough on a
    // -60 dB display to look like a loud signal rather than like a bug.
    calibrationDb = (float) (20.0 * std::log10 (2.0 / ((double) size * coherentGain)));
}

void SpectrumAnalyser::reset()
{
    std::fill (instantDb.begin(),      instantDb.end(),      floorDb);
    std::fill (smoothedDb.begin(),     smoothedDb.end(),     floorDb);
    std::fill (peakDb.begin(),         peakDb.end(),         floorDb);
    std::fill (peakAgeSeconds.begin(), peakAgeSeconds.end(), 0.0f);
}

void SpectrumAnalyser::resetPeaks()
{
    for (size_t i = 0; i < peakDb.size(); ++i)
    {
        peakDb[i]         = smoothedDb[i];
        peakAgeSeconds[i] = 0.0f;
    }
}

void SpectrumAnalyser::setOctaveSmoothing (float fractionOfOctave)
{
    const auto clamped = juce::jlimit (0.0f, 2.0f, fractionOfOctave);

    if (std::abs (clamped - octaveFraction) < 1.0e-6f)
        return;

    octaveFraction = clamped;
    rebuildBands();
}

void SpectrumAnalyser::rebuildBands()
{
    const auto bins = getNumBins();

    bandFirst.assign ((size_t) bins, 0.0f);
    bandLast.assign  ((size_t) bins, 0.0f);
    smoothingCompensationDb = 0.0f;

    if (octaveFraction <= 0.0f || bins <= 0)
        return;

    // What makes the smoothed curve read the same whatever the window length.
    //
    // The band average divides by the number of bins in the band, and that
    // count is proportional to the window, so without this the whole curve
    // slides down the screen as FFT Size goes up - 9.8 dB from 4096 to 65536,
    // measured. It is the same correction the tiers used to carry one apiece,
    // moved to where it belongs: here it also covers the single-tier case,
    // which is where changing FFT Size actually happens.
    //
    // A per-bin version was tried, using the ratio of the band widths the
    // smoother really used, on the theory that it would also handle the bass -
    // where a third of an octave is narrower than one bin and no averaging
    // happens at all. It does handle tones there, and it breaks noise: with no
    // averaging, a tone's level is independent of the window and a noise floor
    // still falls with it, and no single number can follow both. That is not a
    // shortcoming of either attempt but a property of the situation - a tone
    // occupies at least one bin, so a band narrower than that cannot hold it -
    // and it is the same fact recorded at multiResolutionAvailable.
    //
    // So the correction is the one that keeps *noise* exact, because that is
    // what makes the tiers splice without a step and the floor sit still. A
    // tone below about 150 Hz is then still window-dependent at the two
    // shortest sizes: 6 dB at 1024 and 3 dB at 2048. AnalyzerCheck measures
    // both the fix and what it leaves behind.
    smoothingCompensationDb = FXParams::smoothingLevelOffsetDb (size);

    // Half a band either side, in octaves, as a frequency ratio - and only a
    // third of the requested width, because three of these passes are cascaded.
    // Box widths add through a cascade, so three thirds make the whole.
    const auto ratio = std::pow (2.0, (double) octaveFraction * 0.5 / (double) smoothingPasses);

    for (int i = 0; i < bins; ++i)
    {
        // Bin index and frequency are proportional, so the band's edges in bin
        // space are just the index scaled by the same ratio - no need to go
        // through hertz and back.
        // Fractional edges, half a bin either side of the centre at minimum so
        // that a band narrower than one bin still covers the bin it belongs to
        // rather than collapsing to nothing.
        const auto centre = (double) i + 0.5;

        bandFirst[(size_t) i] = (float) juce::jlimit (0.0, centre,
                                                      juce::jmin (centre / ratio, centre - 0.5));
        bandLast[(size_t) i]  = (float) juce::jlimit (centre, (double) bins,
                                                      juce::jmax (centre * ratio, centre + 0.5));
    }
}

void SpectrumAnalyser::smoothOverBands (const std::vector<float>& source, std::vector<float>& destination)
{
    const auto bins = (int) source.size();

    if (bins <= 0 || (int) bandFirst.size() != bins || (int) powerPrefix.size() < bins + 1)
        return;

    // Prefix sum of linear power, so a band of any width costs two lookups.
    powerPrefix[0] = 0.0;

    for (int i = 0; i < bins; ++i)
        powerPrefix[(size_t) i + 1] = powerPrefix[(size_t) i]
                                    + std::pow (10.0, (double) source[(size_t) i] * 0.1);

    // The prefix sum, read at fractional positions. powerPrefix[k] is the total
    // of bins 0..k-1, so it is a function of a bin *edge*; interpolating it
    // linearly between edges is the natural reading and is what makes the band
    // width exact rather than rounded.
    const auto totalUpTo = [this, bins] (double edge)
    {
        const auto clamped = juce::jlimit (0.0, (double) bins, edge);
        const auto lower   = (int) std::floor (clamped);
        const auto upper   = juce::jmin (bins, lower + 1);
        const auto part    = clamped - lower;

        return powerPrefix[(size_t) lower]
             + part * (powerPrefix[(size_t) upper] - powerPrefix[(size_t) lower]);
    };

    for (int i = 0; i < bins; ++i)
    {
        const auto first = (double) bandFirst[(size_t) i];
        const auto last  = (double) bandLast[(size_t) i];
        const auto width = juce::jmax (1.0e-6, last - first);

        const auto mean = (totalUpTo (last) - totalUpTo (first)) / width;

        destination[(size_t) i] = mean > 0.0 ? juce::jmax (floorDb, (float) (10.0 * std::log10 (mean)))
                                             : floorDb;
    }
}

//==============================================================================
void SpectrumAnalyser::process (const float* samples,
                                float smoothingSeconds,
                                float attackSeconds,
                                float holdSeconds,
                                float deltaSeconds)
{
    if (fft == nullptr || samples == nullptr)
        return;

    juce::FloatVectorOperations::copy (fftData.data(), samples, size);
    juce::FloatVectorOperations::clear (fftData.data() + size, size);

    window->multiplyWithWindowingTable (fftData.data(), (size_t) size);
    fft->performFrequencyOnlyForwardTransform (fftData.data(), true);

    const auto bins = getNumBins();

    // DC and Nyquist have no mirrored twin, so the factor of two that accounts
    // for the negative-frequency half does not apply to them. Getting this
    // wrong shows up as a 6 dB step at the very left of the display - a fake
    // rumble on any signal with an offset.
    const float edgeCorrectionDb = -6.0206f;

    // One time constant per frame. exp() rather than a fixed 0.8: the timer is
    // not perfectly regular, and a coefficient that ignores the frame it is
    // applied over gives a different decay on a busy machine than on an idle
    // one, which is a display whose meaning depends on CPU load.
    const auto step  = juce::jmax (0.0f, deltaSeconds);
    const auto tau   = juce::jmax (1.0e-4f, smoothingSeconds);
    const auto alpha = 1.0f - std::exp (-step / tau);

    // Zero attack means instant, and it is written as a coefficient of exactly
    // one rather than as a branch, so the inner loop has one shape.
    const auto attackAlpha = attackSeconds <= 0.0f
                                 ? 1.0f
                                 : 1.0f - std::exp (-step / juce::jmax (1.0e-4f, attackSeconds));

    // Once a peak starts to fall it does so at a fixed rate rather than
    // exponentially, because a peak marker is read as "how far down from there
    // am I now" and an exponential tail never quite arrives anywhere. The rate
    // itself is in ParameterIds.h with the reasoning for its value.

    for (int i = 0; i < bins; ++i)
    {
        const auto magnitude = fftData[(size_t) i];
        auto db = magnitude > 0.0f ? 20.0f * std::log10 (magnitude) + calibrationDb
                                   : floorDb;

        if (i == 0 || i == bins - 1)
            db += edgeCorrectionDb;

        db = juce::jmax (floorDb, db);

        instantDb[(size_t) i] = db;

        auto& smoothed = smoothedDb[(size_t) i];

        // Two coefficients, one per direction. At every setting but Very Slow
        // the rise is instant and only the fall is smoothed, which is what
        // catches a transient at its real height; at Very Slow both are equal
        // and the curve becomes a running average instead.
        smoothed += (db - smoothed) * (db > smoothed ? attackAlpha : alpha);

        auto& peak = peakDb[(size_t) i];
        auto& age  = peakAgeSeconds[(size_t) i];

        if (smoothed >= peak)
        {
            peak = smoothed;
            age  = 0.0f;
        }
        else
        {
            age += deltaSeconds;

            if (age > holdSeconds)
                peak = juce::jmax (smoothed, peak - FXParams::peakFallDbPerSecond * deltaSeconds);
        }
    }

    // Across frequency, after across time. This order is not interchangeable:
    // smoothing across frequency first and then across time would average the
    // already-averaged bands again every frame, and the display would keep
    // creeping wider the longer it ran.
    if (octaveFraction > 0.0f)
    {
        const auto cascade = [this] (const std::vector<float>& source, std::vector<float>& destination)
        {
            smoothOverBands (source, destination);

            for (int pass = 1; pass < smoothingPasses; ++pass)
            {
                smoothOverBands (destination, scratchDb);
                destination.swap (scratchDb);
            }
        };

        cascade (smoothedDb, displayDb);
        cascade (peakDb,     displayPeakDb);

        // Once, after the cascade, not once per pass. Three boxes in series
        // have unit gain each, so what the cascade does to a peak is set by its
        // total support rather than by the number of passes: measured, the
        // level falls by exactly 3.01 dB per doubling of the window, not by
        // three times that.
        for (int i = 0; i < bins; ++i)
        {
            displayDb[(size_t) i]     = juce::jmax (floorDb, displayDb[(size_t) i]     + smoothingCompensationDb);
            displayPeakDb[(size_t) i] = juce::jmax (floorDb, displayPeakDb[(size_t) i] + smoothingCompensationDb);
        }
    }
}

//==============================================================================
SpectrumAnalyser::PeakEstimate SpectrumAnalyser::estimatePeak (float lowHz, float highHz) const
{
    PeakEstimate result;

    if (instantDb.size() < 3)
        return result;

    const auto bins    = getNumBins();
    const auto lowBin  = juce::jlimit (1, bins - 2, (int) std::floor ((double) lowHz  * size / rate));
    const auto highBin = juce::jlimit (1, bins - 2, (int) std::ceil  ((double) highHz * size / rate));

    int   bestBin = -1;
    float bestDb  = floorDb;

    for (int i = lowBin; i <= highBin; ++i)
    {
        if (instantDb[(size_t) i] > bestDb)
        {
            bestDb  = instantDb[(size_t) i];
            bestBin = i;
        }
    }

    if (bestBin < 1 || bestBin > bins - 2)
        return result;

    // Parabola through three dB values. For a Hann window this is not a
    // convenience, it is close to the exact answer: the window's main lobe is
    // very nearly a parabola in dB, which is why Hann and this estimator are
    // chosen together rather than separately.
    const auto left   = instantDb[(size_t) bestBin - 1];
    const auto centre = instantDb[(size_t) bestBin];
    const auto right  = instantDb[(size_t) bestBin + 1];

    const auto denominator = left - 2.0f * centre + right;
    const auto offset = std::abs (denominator) > 1.0e-9f
                            ? 0.5f * (left - right) / denominator
                            : 0.0f;

    result.frequencyHz = (float) (rate * ((double) bestBin + (double) juce::jlimit (-0.5f, 0.5f, offset)) / (double) size);
    result.levelDb     = centre - 0.25f * (left - right) * offset;
    result.valid       = centre > floorDb + 1.0f;

    return result;
}
