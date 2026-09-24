//
//  SpectrumAnalyser.h
//  FX Analyzer
//
//  Windowed FFT, magnitudes in dB, with the two pieces of arithmetic that most
//  spectrum displays get wrong.
//
//  The first is calibration. juce::dsp::FFT hands back an unnormalised
//  magnitude, and dividing it by the transform length - the obvious move - is
//  wrong twice over: it ignores the window's coherent gain, and it ignores that
//  a real sine puts half its energy in the mirrored bin nobody looks at. A
//  full-scale sine has to read 0 dB, because every number on the panel is
//  compared against a fader somewhere, so the factor is 4/(N * coherentGain)
//  and it is derived here rather than tuned until the display looked right.
//  AnalyzerCheck asserts it at three frequencies and two window sizes.
//
//  The second is scalloping. A sine between two bin centres splits itself
//  across both, and with a Hann window that can cost 1.4 dB - enough to make
//  the same tone appear to change level as it drifts. Reading a single bin
//  therefore under-reads almost everything. The peak estimate here interpolates
//  a parabola through the three bins in dB, which for a Hann window is very
//  nearly exact, and recovers both the true frequency and the true level.
//
//  Smoothing runs in dB, not in magnitude. Averaging magnitudes lets one loud
//  frame dominate the average for a second, and the curve then falls in a way
//  that looks like a release envelope instead of a measurement. Smoothing the
//  logarithm is the behaviour of every analogue meter anybody has ever read.
//

#pragma once

#include <juce_dsp/juce_dsp.h>

#include <vector>

class SpectrumAnalyser
{
public:
    SpectrumAnalyser() = default;

    /** Allocates the transform, window and curves. Message thread only. */
    void prepare (double sampleRate, int fftOrder);

    /** Changes the transform size, keeping the sample rate. No-op if unchanged,
        which matters because the Settings page will ask on every repaint. */
    void setFftOrder (int fftOrder);

    int getFftOrder() const noexcept { return order; }
    int getFftSize()  const noexcept { return size; }
    int getNumBins()  const noexcept { return size / 2 + 1; }

    double getSampleRate() const noexcept { return rate; }

    float getBinFrequency (int bin) const noexcept
    {
        return (float) (rate * (double) bin / (double) size);
    }

    /** Windows the supplied block - which must be getFftSize() samples long -
        transforms it, and advances the smoothed curve and the peak-hold curve
        by deltaSeconds.

        smoothingSeconds is one time constant of the fall; attackSeconds is the
        same for the rise, where zero means instant. holdSeconds is how long a
        peak stays before it starts to fall. The two time constants are separate
        because the display is asked for two different things at different
        settings - see FXParams::reactivityAttackSeconds. */
    void process (const float* samples,
                  float smoothingSeconds,
                  float attackSeconds,
                  float holdSeconds,
                  float deltaSeconds);

    /** Fractional-octave smoothing across frequency: 1.0f/3.0f for third
        octave, 0 for none.

        This is a second, independent kind of averaging and it does not replace
        the time smoothing above. Time smoothing settles one bin down across
        frames; it can do nothing about the comb of peaks and nulls *between*
        neighbouring bins, because that comb is present in every frame equally.
        Only averaging across frequency removes it.

        The band is a constant fraction of an octave rather than a constant
        number of hertz, because that is the only width that means the same
        thing everywhere: a third of an octave is a third of an octave at 60 Hz
        and at 6 kHz, while the transform's own bins are a fixed distance apart
        and so span four octaves at the bottom of the display and a twentieth of
        one at the top. */
    void setOctaveSmoothing (float fractionOfOctave);

    float getOctaveSmoothing() const noexcept { return octaveFraction; }

    /** The smoothed curve, in dB, one entry per bin, before octave smoothing. */
    const std::vector<float>& getMagnitudesDb() const noexcept { return smoothedDb; }

    /** The peak-hold curve, in dB, before octave smoothing. */
    const std::vector<float>& getPeakDb() const noexcept { return peakDb; }

    /** What the panel draws: the two curves above, averaged across frequency by
        the current octave setting. With smoothing off these are the same arrays,
        not copies of them. */
    const std::vector<float>& getDisplayDb() const noexcept
    {
        return octaveFraction > 0.0f ? displayDb : smoothedDb;
    }

    const std::vector<float>& getDisplayPeakDb() const noexcept
    {
        return octaveFraction > 0.0f ? displayPeakDb : peakDb;
    }

    /** The instantaneous, unsmoothed curve. The peak estimator reads this so a
        held note's frequency does not drift while the smoother catches up. */
    const std::vector<float>& getInstantDb() const noexcept { return instantDb; }

    void resetPeaks();

    /** Clears everything to the floor. Used when the input goes away, so the
        curve does not hang at its last value looking like signal. */
    void reset();

    //==============================================================================
    struct PeakEstimate
    {
        float frequencyHz = 0.0f;
        float levelDb     = -200.0f;
        bool  valid       = false;
    };

    /** Largest peak in the instantaneous spectrum between the two frequencies,
        refined by parabolic interpolation. */
    PeakEstimate estimatePeak (float lowHz, float highHz) const;

    /** The floor every curve rests on and every level is clamped to. Chosen to
        sit a long way under the panel's -60 dB axis so a curve that is merely
        quiet still moves, rather than lying flat on the bottom pretending to be
        silence. */
    static constexpr float floorDb = -140.0f;

private:
    void allocate();
    void rebuildBands();

    /** Averages `source` into `destination` over the precomputed bands.

        The averaging is done on power, not on decibels, and that is the whole
        difference between a third-octave display that finds a resonance and one
        that hides it. Averaging decibels averages logarithms, which
        under-weights the loud bin in a band and drags a narrow peak down
        towards its quiet neighbours - the exact opposite of what somebody
        looking for that peak needs. Power averaging is the band's RMS, and it
        is what third-octave analysers have always shown.

        A prefix sum makes it one pass regardless of band width; summing each
        band directly would be O(bins x width), and at 1/1 octave near 20 kHz
        that width is thousands of bins. */
    void smoothOverBands (const std::vector<float>& source, std::vector<float>& destination);

    int    order = 12;
    int    size  = 4096;
    double rate  = 48000.0;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

    /** 4 / (N * coherentGain), as dB. Applied after the log, because doing it
        before means one multiply per bin per frame for a constant. */
    float calibrationDb = 0.0f;

    std::vector<float> fftData;      // 2N, as juce::dsp::FFT requires
    std::vector<float> instantDb;
    std::vector<float> smoothedDb;
    std::vector<float> peakDb;
    std::vector<float> peakAgeSeconds;

    /** Three box passes, each a third of the requested width, rather than one
        pass at the full width.

        A single rectangular band is wrong in a way that is easy to miss and was
        caught by AnalyzerCheck rather than by eye: a sliding average that
        divides by its bin count peaks wherever the window is *narrowest*, and a
        constant-fraction-of-an-octave window is always narrower below than
        above. A pure tone at bin 512 therefore came out reading loudest at bin
        457 - the smoothing moved the note. Cascading boxes gives a smooth,
        near-Gaussian weighting whose falloff outruns the change in bin density,
        and the peak stays where the tone is. Three passes cost three linear
        scans; the artefact costs a display that lies about frequency. */
    static constexpr int smoothingPasses = 3;

    /** Band edges in *fractional* bins, not whole ones.

        Rounding the edges to whole bins seemed harmless and is not. A band a
        fixed fraction of an octave wide is only a few bins across down at the
        bottom of the spectrum - a third of an octave at 50 Hz is four bins of a
        16384-point transform and less than one of a 4096-point one - so
        rounding changes its width by a third or more, and by a different third
        at every bin. Two transforms then no longer average over bin counts in
        the ratio of their sizes, which is the one assumption the splice between
        resolutions rests on: the seam stepped by 3.6 dB until this was
        fractional. Interpolating the prefix sum at fractional positions costs
        two multiplies per bin and removes the staircase at the low end as
        well. */
    float octaveFraction = 0.0f;
    std::vector<float>  bandFirst, bandLast;   // fractional bin edges, per pass

    /** What to add to the smoothed curve so that it reads the same at every
        window length - see FXParams::smoothingReferenceFftSize. Zero while
        smoothing is off, because there is then no band average to undo. */
    float smoothingCompensationDb = 0.0f;
    std::vector<double> powerPrefix;           // powerPrefix[k] = sum of bins 0..k-1
    std::vector<float>  displayDb, displayPeakDb, scratchDb;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyser)
};
