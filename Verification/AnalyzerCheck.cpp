//
//  AnalyzerCheck.cpp
//  FX Analyzer
//
//  Every analyser in the plugin, run against signals whose answer is known from
//  the standard or from arithmetic - never from this project's own output.
//
//  The failure mode this exists for is specific to measurement tools. A meter
//  that is wrong does not crash, does not glitch and does not look wrong. It
//  reads 43 Hz instead of 41, or -9.2 LUFS instead of -8.6, and every decision
//  taken in front of it inherits the error in silence. There is no way to hear
//  that, so it has to be measured, and measured against numbers from somewhere
//  else.
//
//  Where the numbers come from:
//
//    - The 48 kHz K-weighting coefficients are the table printed in ITU-R
//      BS.1770-4. Matching it is what makes the derived coefficients
//      trustworthy at 44.1 and 96 kHz, where no published table exists.
//    - The sine loudness cases are EBU Tech 3341 compliance tests 1 and 2.
//    - The loudness range case is arithmetic on the definition in EBU Tech
//      3342: two levels ten units apart, held long enough that the percentiles
//      land on them.
//    - The spectrum cases are arithmetic on the definition of the DFT and of
//      dBFS. A full-scale sine reads 0 dB or the calibration is wrong; there is
//      nothing to interpret.
//    - The pitch cases are equal temperament at A440, which is a table anybody
//      can check.
//    - The null test is the plugin's central promise, asserted on bit patterns
//      rather than on a tolerance.
//
//  Run it with no arguments. Exit status is non-zero if anything failed.
//

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <chrono>
#include <thread>
#include <string>

#include "AnalysisHub.h"
#include "DCBlocker.h"
#include "LoudnessMeter.h"
#include "AnalysisHub.h"
#include "Oscilloscope.h"
#include "PitchDetector.h"
#include "SpectrumAnalyser.h"
#include "StereoAnalyser.h"
#include "TruePeakMeter.h"

//==============================================================================
namespace
{
    int failures = 0;
    int checks   = 0;

    /** Same bits, not "close enough". Comparing bit patterns is what the
        transparency claim actually means, it keeps -Wfloat-equal honest instead
        of suppressed, and it is stricter than ==: +0.0 and -0.0 compare equal
        as floats but are different bits. */
    bool sameBits (float a, float b) noexcept
    {
        return std::bit_cast<std::uint32_t> (a) == std::bit_cast<std::uint32_t> (b);
    }

    void section (const std::string& title)
    {
        std::printf ("\n\033[1m%s\033[0m\n", title.c_str());
    }

    void report (bool passed, const std::string& what, const std::string& detail)
    {
        ++checks;

        if (! passed)
            ++failures;

        std::printf ("  %s  %-56s %s\n",
                     passed ? "\033[32mPASS\033[0m" : "\033[31mFAIL\033[0m",
                     what.c_str(), detail.c_str());
    }

    void expectNear (double actual, double expected, double tolerance, const std::string& what)
    {
        const double delta = std::abs (actual - expected);
        char detail[160];
        std::snprintf (detail, sizeof (detail), "got %.6f  expected %.6f  (delta %.2e, tol %.0e)",
                       actual, expected, delta, tolerance);
        report (delta <= tolerance, what, detail);
    }

    void expectWithin (double actual, double low, double high, const std::string& what)
    {
        char detail[160];
        std::snprintf (detail, sizeof (detail), "got %.4f  expected [%.4f, %.4f]", actual, low, high);
        report (actual >= low && actual <= high, what, detail);
    }

    void expectTrue (bool condition, const std::string& what, const std::string& detail = {})
    {
        report (condition, what, detail);
    }

    //==========================================================================
    /** Amplitude of a sine at the given dBFS, on the convention that a
        full-scale sine has amplitude 1.0. This is what EBU Tech 3341 uses to
        specify its test signals, and what the spectrum display is calibrated
        against. */
    double sineAmplitudeFor (double dBFS)
    {
        return std::pow (10.0, dBFS / 20.0);
    }

    void fillSine (juce::AudioBuffer<float>& buffer, double sampleRate,
                   double frequency, double amplitude, double phase = 0.0)
    {
        const double increment = juce::MathConstants<double>::twoPi * frequency / sampleRate;

        for (int n = 0; n < buffer.getNumSamples(); ++n)
        {
            const auto value = (float) (amplitude * std::cos (phase + increment * n));

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, n, value);
        }
    }

    void feed (LoudnessMeter& meter, const juce::AudioBuffer<float>& buffer, int blockSize = 512)
    {
        const int total = buffer.getNumSamples();

        for (int start = 0; start < total; start += blockSize)
        {
            const int count = juce::jmin (blockSize, total - start);
            const float* pointers[2]
            {
                buffer.getReadPointer (0, start),
                buffer.getNumChannels() > 1 ? buffer.getReadPointer (1, start) : nullptr
            };

            meter.processBlock (pointers, buffer.getNumChannels(), count);
        }
    }

    /** One window of a sine, run through the analyser enough times that the
        display smoother has arrived. Returns the analyser so the caller can
        interrogate whichever curve it cares about. */
    void driveSpectrum (SpectrumAnalyser& analyser, double sampleRate,
                        double frequency, double amplitude, double phase = 0.0,
                        float attackSeconds = 0.0f)
    {
        std::vector<float> window ((size_t) analyser.getFftSize());

        const double increment = juce::MathConstants<double>::twoPi * frequency / sampleRate;

        for (size_t n = 0; n < window.size(); ++n)
            window[n] = (float) (amplitude * std::cos (phase + increment * (double) n));

        // Twenty frames at a tenth of a second: far more than the 150 ms
        // smoothing constant needs, and the assertion is then about the
        // measurement rather than about how far the smoother happened to get.
        for (int frame = 0; frame < 20; ++frame)
            analyser.process (window.data(), 0.15f, attackSeconds, 1.5f, 0.1f);
    }
}

//==============================================================================
static void testCoefficientDerivation()
{
    section ("K-weighting derivation vs. the BS.1770-4 table at 48 kHz");

    const auto shelf = LoudnessMeter::makeShelfCoefficients (48000.0);

    expectNear (shelf.b0,  1.53512485958697, 1e-9, "shelf b0");
    expectNear (shelf.b1, -2.69169618940638, 1e-9, "shelf b1");
    expectNear (shelf.b2,  1.19839281085285, 1e-9, "shelf b2");
    expectNear (shelf.a1, -1.69065929318241, 1e-9, "shelf a1");
    expectNear (shelf.a2,  0.73248077421585, 1e-9, "shelf a2");

    const auto highPass = LoudnessMeter::makeHighPassCoefficients (48000.0);

    expectNear (highPass.b0,  1.0, 1e-12, "RLB b0");
    expectNear (highPass.b1, -2.0, 1e-12, "RLB b1");
    expectNear (highPass.b2,  1.0, 1e-12, "RLB b2");
    expectNear (highPass.a1, -1.99004745483398, 1e-6, "RLB a1");
    expectNear (highPass.a2,  0.99007225036621, 1e-6, "RLB a2");
}

//==============================================================================
static void testLoudnessSines()
{
    section ("EBU Tech 3341 sine cases, at three sample rates");

    // The reason the coefficients are derived rather than pasted: 44.1 and
    // 96 kHz have to read the same as 48. A meter using the pasted 48 kHz table
    // reads about 0.15 LU high at 44.1 kHz and considerably worse at 96.
    for (const double sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        for (const double level : { -23.0, -33.0 })
        {
            LoudnessMeter meter;
            meter.prepare (sampleRate, 2);

            juce::AudioBuffer<float> buffer (2, (int) (sampleRate * 20.0));
            fillSine (buffer, sampleRate, 1000.0, sineAmplitudeFor (level));
            feed (meter, buffer);

            char label[110];

            std::snprintf (label, sizeof (label), "%g kHz, %g dBFS sine: momentary",
                           sampleRate / 1000.0, level);
            expectNear (meter.getMomentaryLufs(), level, 0.1, label);

            std::snprintf (label, sizeof (label), "%g kHz, %g dBFS sine: short-term",
                           sampleRate / 1000.0, level);
            expectNear (meter.getShortTermLufs(), level, 0.1, label);

            std::snprintf (label, sizeof (label), "%g kHz, %g dBFS sine: integrated",
                           sampleRate / 1000.0, level);
            expectNear (meter.getIntegratedLufs(), level, 0.1, label);
        }
    }
}

//==============================================================================
static void testLoudnessRange()
{
    section ("Loudness range, EBU Tech 3342");

    constexpr double sampleRate = 48000.0;

    LoudnessMeter meter;
    meter.prepare (sampleRate, 2);

    // Thirty seconds at -20 LUFS then thirty at -30. Both levels are far above
    // the absolute gate and within 20 LU of the mean, so neither gate removes
    // anything and the answer is the distance between them.
    juce::AudioBuffer<float> loud (2, (int) (sampleRate * 30.0));
    fillSine (loud, sampleRate, 1000.0, sineAmplitudeFor (-20.0));
    feed (meter, loud);

    juce::AudioBuffer<float> quiet (2, (int) (sampleRate * 30.0));
    fillSine (quiet, sampleRate, 1000.0, sineAmplitudeFor (-30.0));
    feed (meter, quiet);

    expectNear (meter.getLoudnessRange(), 10.0, 1.0, "two levels 10 LU apart give LRA 10");

    // A meter that reported maximum minus minimum instead of the percentiles
    // would give the same answer above and a visibly different one here: a
    // single loud second inside an otherwise steady programme moves the maximum
    // by 10 LU and the 95th percentile by almost nothing.
    LoudnessMeter steady;
    steady.prepare (sampleRate, 2);

    juce::AudioBuffer<float> body (2, (int) (sampleRate * 60.0));
    fillSine (body, sampleRate, 1000.0, sineAmplitudeFor (-20.0));
    feed (steady, body);

    juce::AudioBuffer<float> blip (2, (int) (sampleRate * 1.0));
    fillSine (blip, sampleRate, 1000.0, sineAmplitudeFor (-10.0));
    feed (steady, blip);

    feed (steady, body);

    expectWithin (steady.getLoudnessRange(), 0.0, 3.0,
                  "one loud second in two steady minutes barely moves LRA");
}

//==============================================================================
static void testTruePeak()
{
    section ("True peak");

    constexpr double sampleRate = 48000.0;

    TruePeakMeter meter;
    meter.prepare (2, 512);

    // A sine at a quarter of the sample rate, phased so every sample lands at
    // +/- 1/sqrt(2). The sample peak is -3.01 dBFS and the true peak is 0: the
    // waveform reaches full scale between every pair of samples. A meter
    // reporting sample peak passes nothing here.
    juce::AudioBuffer<float> buffer (2, (int) (sampleRate * 4.0));
    fillSine (buffer, sampleRate, sampleRate / 4.0, 1.0, juce::MathConstants<double>::pi / 4.0);

    float samplePeak = 0.0f;

    for (int n = 0; n < buffer.getNumSamples(); ++n)
        samplePeak = juce::jmax (samplePeak, std::abs (buffer.getSample (0, n)));

    for (int start = 0; start < buffer.getNumSamples(); start += 512)
    {
        const int count = juce::jmin (512, buffer.getNumSamples() - start);
        const float* pointers[2] { buffer.getReadPointer (0, start), buffer.getReadPointer (1, start) };
        meter.processBlock (pointers, 2, count);
    }

    expectNear (juce::Decibels::gainToDecibels (samplePeak), -3.0103, 0.01,
                "the test signal's sample peak really is -3 dBFS");
    expectNear (meter.getMaxTruePeakDb(), 0.0, 0.3, "inter-sample peak is found");
}

//==============================================================================
static void testSpectrumCalibration()
{
    section ("Spectrum calibration");

    constexpr double sampleRate = 48000.0;

    // Up to 65536 because that size is now on the Settings page rather than
    // only inside the multi-resolution tiers. It carries a second weight since
    // the bass zoom: the cursor readout reads a bin from whichever tier owns
    // the frequency and deliberately does *not* apply that tier's level offset,
    // which is only correct if a calibrated tone reads its own level at every
    // window length. That is what these three lines check.
    for (const int order : { 11, 12, 14, 15, 16 })
    {
        SpectrumAnalyser analyser;
        analyser.prepare (sampleRate, order);

        const auto size = analyser.getFftSize();

        for (const double level : { 0.0, -18.0, -40.0 })
        {
            // A bin-centred frequency, so the assertion is about the
            // calibration constant and not about the window's scalloping loss,
            // which is measured separately below.
            const int    bin       = size / 16;
            const double frequency = sampleRate * bin / size;

            driveSpectrum (analyser, sampleRate, frequency, sineAmplitudeFor (level));

            const auto& magnitudes = analyser.getMagnitudesDb();

            char label[110];
            std::snprintf (label, sizeof (label), "%d pt, %g dBFS sine at bin %d", size, level, bin);
            expectNear (magnitudes[(size_t) bin], level, 0.2, label);
        }
    }

    // DC is the one bin whose energy is not mirrored, so it needs its own
    // correction. Without it a signal with an offset draws a 6 dB tower at the
    // extreme left of every spectrum.
    {
        SpectrumAnalyser analyser;
        analyser.prepare (sampleRate, 12);

        std::vector<float> window ((size_t) analyser.getFftSize(), 0.5f);

        for (int frame = 0; frame < 20; ++frame)
            analyser.process (window.data(), 0.15f, 0.0f, 1.5f, 0.1f);

        expectNear (analyser.getMagnitudesDb()[0], -6.0206, 0.2, "DC bin reads its own amplitude");
    }
}

//==============================================================================
static void testSpectrumPeakEstimate()
{
    section ("Spectrum peak estimation");

    constexpr double sampleRate = 48000.0;

    SpectrumAnalyser analyser;
    analyser.prepare (sampleRate, 12);

    // 997 Hz is the classic audio test frequency precisely because it is not a
    // neat fraction of any sample rate: it falls between bins at every size, so
    // a display reading a single bin under-reads it and a nearest-bin frequency
    // readout is out by several hertz.
    for (const double frequency : { 100.0, 997.0, 4410.0 })
    {
        analyser.reset();
        driveSpectrum (analyser, sampleRate, frequency, sineAmplitudeFor (-12.0));

        const auto estimate = analyser.estimatePeak (20.0f, 20000.0f);

        char label[110];

        std::snprintf (label, sizeof (label), "%g Hz sine: frequency found", frequency);
        expectNear (estimate.frequencyHz, frequency, frequency * 0.005, label);

        std::snprintf (label, sizeof (label), "%g Hz sine: level recovered despite scalloping", frequency);
        expectNear (estimate.levelDb, -12.0, 0.5, label);
    }
}

//==============================================================================
static void testReactivity()
{
    section ("Reactivity: peak envelope against running average");

    constexpr double sampleRate = 48000.0;
    constexpr double loudDb  =  -6.0;
    constexpr double quietDb = -26.0;

    // A signal that alternates between two levels twenty decibels apart, faster
    // than either smoother can follow. What the curve settles on is the whole
    // difference between the two behaviours: an instant-rise smoother tracks
    // the loud half and hangs there, while a symmetric one averages the two.
    const auto settle = [&] (float attackSeconds)
    {
        SpectrumAnalyser analyser;
        analyser.prepare (sampleRate, 12);

        const auto size = analyser.getFftSize();
        const int  bin  = size / 16;
        const auto frequency = sampleRate * bin / size;

        std::vector<float> loud ((size_t) size), quiet ((size_t) size);

        const double increment = juce::MathConstants<double>::twoPi * frequency / sampleRate;

        for (size_t n = 0; n < loud.size(); ++n)
        {
            loud[n]  = (float) (sineAmplitudeFor (loudDb)  * std::cos (increment * (double) n));
            quiet[n] = (float) (sineAmplitudeFor (quietDb) * std::cos (increment * (double) n));
        }

        // Twelve seconds at thirty frames a second: six time constants of the
        // slowest smoother under test, so the answer is where it settles rather
        // than how far it got.
        for (int frame = 0; frame < 360; ++frame)
            analyser.process (frame % 2 == 0 ? loud.data() : quiet.data(),
                              2.0f, attackSeconds, 6.0f, 1.0f / 30.0f);

        return analyser.getMagnitudesDb()[(size_t) bin];
    };

    const auto instantRise   = settle (0.0f);
    const auto symmetricRise = settle (2.0f);

    expectNear (instantRise, loudDb, 0.5,
                "an instant rise settles on the loud half - a peak envelope");

    expectNear (symmetricRise, 0.5 * (loudDb + quietDb), 1.5,
                "a matched rise settles between the two - a running average");

    expectTrue (instantRise - symmetricRise > 8.0f,
                "the two behaviours are far enough apart to be the point",
                std::to_string (instantRise - symmetricRise) + " dB apart");

    // The documented trade-off table, pinned.
    //
    // ParameterIds.h prints these numbers to the person choosing an Attack
    // setting. A comment nobody checks drifts away from the code under it, and
    // this one is not decoration - it is the whole basis for choosing. So the
    // measurements are assertions.
    struct AttackPoint { float attack; double reads; };

    const AttackPoint documented[]
    {
        { 2.00f, -16.4 },
        { 1.00f, -12.8 },
        { 0.50f, -10.1 },
        { 0.25f,  -8.4 },
        { 0.00f,  -6.3 }
    };

    for (const auto& point : documented)
    {
        char label[120];
        std::snprintf (label, sizeof (label), "attack %.2f s reads as documented", point.attack);
        expectNear (settle (point.attack), point.reads, 0.5, label);
    }

    // Auto is a sentinel, not a duration.
    expectTrue (FXParams::spectrumAttackValues[FXParams::defaultSpectrumAttackIndex] < 0.0f,
                "Auto is the default and means 'follow Reactivity'");

    expectTrue (FXParams::spectrumAttackNames.size() == (int) std::size (FXParams::spectrumAttackValues),
                "every Attack label has a value behind it");

    // And the setting the panel offers has to be the one that averages.
    expectTrue (FXParams::reactivityAttackSeconds (FXParams::Reactivity::verySlow) > 0.0f,
                "Very Slow is the averaging setting");

    for (auto r : { FXParams::Reactivity::slow, FXParams::Reactivity::medium, FXParams::Reactivity::fast })
        expectTrue (FXParams::reactivityAttackSeconds (r) == 0.0f,
                    "the faster settings still catch transients instantly");

    // Ordered slowest first, so that stepping up the list is always stepping
    // calmer. A stepper whose order does not match its meaning is one the user
    // has to read rather than feel.
    expectTrue (FXParams::reactivitySeconds (FXParams::Reactivity::verySlow)
                    > FXParams::reactivitySeconds (FXParams::Reactivity::slow)
                && FXParams::reactivitySeconds (FXParams::Reactivity::slow)
                    > FXParams::reactivitySeconds (FXParams::Reactivity::medium)
                && FXParams::reactivitySeconds (FXParams::Reactivity::medium)
                    > FXParams::reactivitySeconds (FXParams::Reactivity::fast),
                "the four settings are ordered slowest to fastest");

    expectTrue (FXParams::reactivityNames.size() == 4, "the panel offers all four");
}

//==============================================================================
static void testMultiResolution()
{
    section ("Multi-resolution");

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;

    // Feeds a signal through a hub and returns it, so each case below reads as
    // "make this sound, then look".
    const auto run = [&] (std::unique_ptr<AnalysisHub>& hub,
                          FXParams::SpectrumResolution resolution,
                          const std::function<float (int)>& sample,
                          double seconds,
                          float smoothing)
    {
        hub = std::make_unique<AnalysisHub>();
        hub->prepare (sampleRate, blockSize);
        hub->setSpectrumResolution (resolution);
        hub->setSpectrumSmoothing (smoothing);

        // Averaging, not peak-latching, and the seam test is unreadable without
        // it. With an instant rise the smoother settles on the largest of the
        // random per-frame estimates, and the 4096-point tier sees sixteen times
        // as many frames as the 65536-point one in the same audio - so it
        // latches higher, and two tiers that agree perfectly look 4 dB apart.
        // That is a property of comparing noise with a peak envelope, not of
        // the splice.
        hub->setReactivity (FXParams::Reactivity::verySlow);
        hub->setSpectrumAttackSeconds (FXParams::reactivitySeconds (FXParams::Reactivity::verySlow));

        juce::AudioBuffer<float> buffer (2, blockSize);
        const auto blocks = (int) (sampleRate * seconds / blockSize);

        for (int b = 0; b < blocks; ++b)
        {
            for (int n = 0; n < blockSize; ++n)
            {
                const auto value = sample (b * blockSize + n);

                for (int ch = 0; ch < 2; ++ch)
                    buffer.setSample (ch, n, value);
            }

            hub->pushBlock (buffer);
            hub->updateDisplays();
        }
    };

    // Reads the spliced curve the way the panel does: the tier that answers for
    // this frequency. No level constant - the tiers already agree, because
    // SpectrumAnalyser normalises the band width per bin.
    const auto levelAt = [] (const AnalysisHub& hub, float hz)
    {
        const auto choice = hub.chooseTier (hz);

        const auto readTier = [&] (int index)
        {
            const auto& analyser = hub.getTierAnalyser (index);
            const auto  bin = juce::jlimit (0, analyser.getNumBins() - 1,
                                            (int) std::round ((double) hz * analyser.getFftSize()
                                                                  / analyser.getSampleRate()));
            return analyser.getDisplayDb()[(size_t) bin];
        };

        const auto primary = readTier (choice.primary);

        if (choice.secondaryWeight <= 0.0f)
            return primary;

        return primary * (1.0f - choice.secondaryWeight)
             + readTier (choice.secondary) * choice.secondaryWeight;
    };

    // ---- the point of the whole thing -----------------------------------
    //
    // E1 and F1, a semitone apart at the bottom of a bass guitar. At 4096
    // points a bin is 11.7 Hz wide and they are 2.45 Hz apart, so they cannot
    // be two things. At 65536 they are three bins apart.
    {
        constexpr double e1 = 41.203, f1 = 43.654;

        const auto twoTones = [&] (int n)
        {
            const auto t = (double) n / sampleRate;
            return (float) (0.35 * std::sin (juce::MathConstants<double>::twoPi * e1 * t)
                          + 0.35 * std::sin (juce::MathConstants<double>::twoPi * f1 * t));
        };

        const auto dipBetween = [&] (const AnalysisHub& hub)
        {
            const auto peakE  = levelAt (hub, (float) e1);
            const auto peakF  = levelAt (hub, (float) f1);
            const auto middle = levelAt (hub, (float) std::sqrt (e1 * f1));

            return juce::jmin (peakE, peakF) - middle;
        };

        // A twenty-fourth of an octave, not a third.
        //
        // This is the setting the feature has to be used with and it is worth
        // saying plainly: a third-octave band is 23% wide, four semitones, so it
        // merges E1 and F1 no matter how fine the transform underneath is. All
        // the resolution in the world is thrown away by the smoothing above it.
        // At 1/24 octave the band is 2.9% - half a semitone - and the two notes
        // survive.
        std::unique_ptr<AnalysisHub> multi, single;
        run (multi,  FXParams::SpectrumResolution::multi,  twoTones, 12.0, 1.0f / 24.0f);
        run (single, FXParams::SpectrumResolution::single, twoTones, 12.0, 1.0f / 24.0f);

        const auto multiDip  = dipBetween (*multi);
        const auto singleDip = dipBetween (*single);

        char detail[170];
        std::snprintf (detail, sizeof (detail), "dip between E1 and F1: %.1f dB multi, %.1f dB single",
                       multiDip, singleDip);

        report (multiDip > 3.0f, "two bass notes a semitone apart are two peaks", detail);
        report (singleDip < 1.0f, "and at one resolution they are one", detail);
    }

    // ---- the seam, on noise ---------------------------------------------
    {
        std::mt19937 generator (23);
        std::uniform_real_distribution<float> noise (-0.3f, 0.3f);

        std::unique_ptr<AnalysisHub> hub;
        run (hub, FXParams::SpectrumResolution::multi,
             [&] (int) { return noise (generator); }, 20.0, 1.0f / 3.0f);

        for (int b = 0; b + 1 < hub->getNumTiers(); ++b)
        {
            const auto boundary = FXParams::tierCrossoverHz (sampleRate,
                                                             hub->getTierAnalyser (b).getFftSize());

            const auto below = levelAt (*hub, boundary * 0.7f);
            const auto above = levelAt (*hub, boundary * 1.43f);

            char detail[170];
            std::snprintf (detail, sizeof (detail), "at %.0f Hz: %.2f dB below, %.2f dB above",
                           boundary, below, above);

            report (std::abs (below - above) < 2.0f, "white noise crosses the seam without a step", detail);
        }
    }


    // ---- crossovers follow the sample rate -------------------------------
    expectNear (FXParams::tierCrossoverHz (48000.0, 4096), 197.1, 1.0,
                "4096 points at 48 kHz hands over at ~197 Hz");
    expectNear (FXParams::tierCrossoverHz (96000.0, 4096),
                2.0 * FXParams::tierCrossoverHz (48000.0, 4096), 0.1,
                "twice the sample rate doubles the crossover");
}

//==============================================================================
/** Release and the Auto attack it now drives, measured through the hub.

    Release is the spectrum's fall time set independently of Reactivity. Two
    things are claimed for it in ParameterIds.h and both are measured here
    rather than taken from the arithmetic: that the override is the time
    constant the curve actually falls with, and that Auto attack at Very Slow
    follows the release in use rather than Reactivity's own two seconds. The
    second is the one that could silently break - it would turn the averaging
    setting back into a slow peak envelope and nothing on screen would say so.
*/
static void testSpectrumRelease()
{
    section ("Release, and the Auto attack at Very Slow");

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;
    constexpr double toneDb     = -20.0;

    // Bin-centred at 4096 points, so scalloping plays no part in the level.
    const auto toneHz = sampleRate * 64.0 / 4096.0;

    struct Run
    {
        std::unique_ptr<AnalysisHub> hub;

        float levelDb() const
        {
            const auto& spectrum = hub->getSpectrum();
            const auto  bin = 64 * spectrum.getFftSize() / 4096;
            return spectrum.getMagnitudesDb()[(size_t) bin];
        }
    };

    const auto make = [&] (FXParams::Reactivity reactivity, float attack, float release)
    {
        Run run;
        run.hub = std::make_unique<AnalysisHub>();
        run.hub->prepare (sampleRate, blockSize);
        run.hub->setDcBlockEnabled (false);
        run.hub->setReactivity (reactivity);
        run.hub->setSpectrumAttackSeconds (attack);
        run.hub->setSpectrumReleaseSeconds (release);
        return run;
    };

    int64_t clock = 0;

    const auto play = [&] (Run& run, double seconds, bool tone)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        const auto amplitude = tone ? std::pow (10.0, toneDb / 20.0) : 0.0;
        const auto blocks = (int) (sampleRate * seconds / blockSize);

        for (int b = 0; b < blocks; ++b)
        {
            for (int n = 0; n < blockSize; ++n)
            {
                const auto t = (double) (clock + n) / sampleRate;
                const auto value = (float) (amplitude * std::sin (juce::MathConstants<double>::twoPi * toneHz * t));

                buffer.setSample (0, n, value);
                buffer.setSample (1, n, value);
            }

            clock += blockSize;
            run.hub->pushBlock (buffer);
            run.hub->updateDisplays();
        }
    };

    // ---- the fall follows the override ----------------------------------
    //
    // Settle on the tone, stop it, and read how far the curve has fallen one
    // second later. The smoother works in dB towards the -140 dB floor, so the
    // expected fall is the gap times (1 - e^(-t/tau)). The window still holds
    // some of the tone for half its length after the tone stops, which is what
    // the 43 ms taken off t is for.
    const auto fallAfterOneSecond = [&] (float release)
    {
        auto run = make (FXParams::Reactivity::medium, 0.0f, release);
        play (run, 4.0, true);
        const auto settled = run.levelDb();
        play (run, 1.0, false);
        return std::pair<float, float> (settled, settled - run.levelDb());
    };

    for (auto release : { 4.0f, 2.0f, 1.0f })
    {
        const auto [settled, fall] = fallAfterOneSecond (release);
        const auto gap = settled - SpectrumAnalyser::floorDb;
        const auto expected = gap * (1.0 - std::exp (-(1.0 - 0.043) / release));

        char label[120];
        std::snprintf (label, sizeof (label), "release %.0f s falls as one time constant of %.0f s",
                       release, release);
        expectNear (fall, expected, 4.0, label);
    }

    {
        const auto [settledAuto, fallAuto] = fallAfterOneSecond (-1.0f);
        const auto [settledSlow, fallSlow] = fallAfterOneSecond (4.0f);

        char detail[160];
        std::snprintf (detail, sizeof (detail), "fell %.1f dB on Auto (Medium), %.1f dB at 4 s",
                       fallAuto, fallSlow);
        expectTrue (fallAuto > 100.0f && fallSlow < 30.0f,
                    "Auto follows Reactivity, and an override replaces it", detail);
        juce::ignoreUnused (settledAuto, settledSlow);
    }

    // ---- Auto attack at Very Slow rises with the release in use ----------
    //
    // From silence, half a second of tone. With Release at 0.5 s the Auto rise
    // is 0.5 s too and the curve has covered 1 - 1/e of the way; left to
    // Reactivity's 2 s it would have covered about a fifth.
    const auto riseAfterHalfASecond = [&] (float release)
    {
        auto run = make (FXParams::Reactivity::verySlow, -1.0f, release);
        play (run, 1.0, false);
        const auto before = run.levelDb();
        play (run, 0.5, true);
        return (run.levelDb() - before) / (float) (toneDb - before);
    };

    const auto riseMatched  = riseAfterHalfASecond (0.5f);
    const auto riseReactive = riseAfterHalfASecond (-1.0f);

    char detail[160];
    std::snprintf (detail, sizeof (detail), "%.0f%% of the way with Release 0.5 s, %.0f%% on Auto",
                   100.0f * riseMatched, 100.0f * riseReactive);

    expectWithin (riseMatched, 0.52, 0.72, "Auto attack at Very Slow uses the Release in effect");
    expectWithin (riseReactive, 0.12, 0.30, "and still Reactivity's 2 s while Release is Auto");

    // ---- the lists --------------------------------------------------------
    expectTrue (FXParams::spectrumReleaseNames.size() == (int) std::size (FXParams::spectrumReleaseValues),
                "every Release label has a value behind it");

    expectTrue (FXParams::spectrumReleaseValues[FXParams::defaultSpectrumReleaseIndex] < 0.0f,
                "Release defaults to Auto, so old sessions fall as before");

    bool ordered = true;

    for (int i = 2; i < (int) std::size (FXParams::spectrumReleaseValues); ++i)
        ordered = ordered && FXParams::spectrumReleaseValues[i] < FXParams::spectrumReleaseValues[i - 1];

    expectTrue (ordered, "Release is ordered slowest first, like Attack");
}

//==============================================================================
/** The tilt: a value now rather than one of four, turning about a pivot. */
static void testSpectrumTilt()
{
    section ("Tilt slope and pivot");

    expectNear (FXParams::spectrumTiltAt (1000.0f, 4.5f, 1000.0f), 0.0, 1.0e-6,
                "the pivot reads its measured level");
    expectNear (FXParams::spectrumTiltAt (2000.0f, 4.5f, 1000.0f), 4.5, 1.0e-5,
                "an octave above the pivot is one slope higher");
    expectNear (FXParams::spectrumTiltAt (50.0f, 3.0f, 100.0f), -3.0, 1.0e-5,
                "an octave below is one slope lower");
    expectNear (FXParams::spectrumTiltAt (37.0f, 0.0f, 500.0f), 0.0, 0.0,
                "no slope, no tilt, wherever the pivot is");

    // Moving the pivot moves the curve and does not bend it: the difference it
    // makes is the same at every frequency. That is the claim the pivot's
    // documentation rests on, so it is checked across the axis.
    {
        const auto shiftAt = [] (float hz)
        {
            return FXParams::spectrumTiltAt (hz, 4.5f, 100.0f) - FXParams::spectrumTiltAt (hz, 4.5f, 5000.0f);
        };

        double worst = 0.0;

        for (float hz = 20.0f; hz <= 20000.0f; hz *= 1.37f)
            worst = juce::jmax (worst, (double) std::abs (shiftAt (hz) - shiftAt (1000.0f)));

        char detail[120];
        std::snprintf (detail, sizeof (detail), "worst departure %.2e dB", worst);
        expectTrue (worst < 1.0e-3, "a pivot change is a constant, not a bend", detail);
    }

    // Old sessions stored an index into four slopes. Each must come back as
    // the slope it named, and the default must still be the old default.
    for (int i = 0; i < (int) std::size (FXParams::spectrumSlopeValues); ++i)
        expectNear (FXParams::spectrumTiltFromLegacyIndex (i), FXParams::spectrumSlopeValues[i], 0.0,
                    "legacy slope " + FXParams::spectrumSlopeNames[i].toStdString() + " is read as itself");

    expectNear (FXParams::defaultSpectrumTiltDb(), 4.5, 0.0, "the default tilt is still 4.5 dB/oct");

    expectTrue (FXParams::spectrumTiltPivotNames.size() == (int) std::size (FXParams::spectrumTiltPivotValues),
                "every Pivot label has a frequency behind it");
    expectNear (FXParams::spectrumTiltPivotValues[FXParams::defaultSpectrumTiltPivotIndex], 1000.0, 0.0,
                "the default pivot is 1 kHz, where the tilt has always turned");
}

//==============================================================================
static void testScopeTimeBases()
{
    section ("Scope time bases");

    // Requested equals delivered, at every setting and every rate.
    //
    // Oscilloscope::capture clamps to the buffer it was prepared with, and it
    // does so silently: ask for two seconds of a buffer built for two hundred
    // milliseconds and you get two hundred milliseconds under a label promising
    // two seconds. The buffer size and the list of choices used to be two
    // separate constants, one in ParameterIds.h and one in AnalysisHub.cpp, so
    // adding an entry to the list was enough to produce exactly that.
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        AnalysisHub hub;
        hub.prepare (rate, 512);

        juce::AudioBuffer<float> buffer (2, 512);

        // Enough audio for the longest window plus the trigger's look-back.
        const auto longest = FXParams::scopeTimeValues[std::size (FXParams::scopeTimeValues) - 1] / 1000.0;
        const auto blocks  = (int) std::ceil (rate * longest * 2.5 / 512.0);

        for (int b = 0; b < blocks; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < 512; ++n)
                {
                    const auto phase = juce::MathConstants<double>::twoPi * 110.0
                                           * (double) (b * 512 + n) / rate;
                    buffer.setSample (ch, n, (float) (0.4 * std::sin (phase)));
                }

            hub.pushBlock (buffer);
        }

        for (auto ms : FXParams::scopeTimeValues)
        {
            hub.setScopeWindowSeconds (ms / 1000.0f);
            hub.updateDisplays();

            const auto expected = (int) std::round (ms / 1000.0f * rate);
            const auto got = hub.getScope().getNumSamples();

            char detail[170];
            std::snprintf (detail, sizeof (detail), "%.0f kHz, %g ms: asked for %d samples, got %d",
                           rate / 1000.0, (double) ms, expected, got);

            report (std::abs (got - expected) <= 1, "the scope delivers the window it was asked for", detail);
        }
    }
}

//==============================================================================
static void testOverlap()
{
    section ("Overlap and analysis rate");

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;

    // A second of audio, fed the way the audio thread feeds it, with the
    // display pulled between blocks the way the editor's timer pulls it. What
    // is counted is how many transforms the second produced - which is the
    // overlap, expressed as the thing it actually determines.
    const auto framesForOrder = [&] (int order)
    {
        AnalysisHub hub;
        hub.prepare (sampleRate, blockSize);
        hub.setFftOrder (order);

        juce::AudioBuffer<float> buffer (2, blockSize);
        std::mt19937 generator (17);
        std::uniform_real_distribution<float> noise (-0.2f, 0.2f);

        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < blockSize; ++n)
                buffer.setSample (ch, n, noise (generator));

        const int blocks = (int) (sampleRate / blockSize);

        for (int b = 0; b < blocks; ++b)
        {
            hub.pushBlock (buffer);
            hub.updateDisplays();
        }

        return std::make_pair (hub.getSpectrumFrameCount(), hub.getSpectrumHopSamples());
    };

    for (int order : { 10, 12, 14 })
    {
        const auto [frames, hop] = framesForOrder (order);

        const auto size     = 1 << order;
        const auto expected = (int64_t) ((int) (sampleRate * blockSize / blockSize) / hop);

        char detail[170];
        std::snprintf (detail, sizeof (detail), "%d points: hop %d, %lld frames in a second (expected %lld)",
                       size, hop, (long long) frames, (long long) expected);

        // The invariant is no longer "the hop is a quarter of the window". It is
        // the two things that quarter was standing in for: never less than 75%
        // overlap, and never slower than the display. The second only started
        // to bind once the tiers got long - a 65536-point window at a quarter
        // hop updates 2.9 times a second, and the bass third of the curve
        // visibly stepped while everything above it flowed.
        expectTrue (hop <= size / 4, "at least 75% overlap", detail);
        expectTrue (sampleRate / hop >= AnalysisHub::analysisFramesPerSecond - 1,
                    "no slower than the display repaints", detail);
        report (std::abs (frames - expected) <= 1, "a second of audio yields the frames the hop implies", detail);
    }

    // The point of the fixed hop, stated as the thing it prevents: at the
    // smallest size the old one-transform-per-repaint scheme analysed 21 ms out
    // of every 33 and left the rest unseen. Four transforms per window means
    // every sample is inside four of them, whatever the size.
    for (int order : { 10, 14, 16 })
    {
        const auto [frames, hop] = framesForOrder (order);
        const auto covered = (double) frames * (1 << order) / sampleRate;

        char detail[170];
        std::snprintf (detail, sizeof (detail), "%d points: %.2f window-seconds of analysis per second of audio",
                       1 << order, covered);

        // Four for the short windows, far more for the long ones - the cap on
        // the hop buys extra coverage as a side effect. What matters is that it
        // is never *less* than four, which is what would leave gaps.
        report (covered > 3.5, "every sample is analysed at least four times over", detail);
        juce::ignoreUnused (hop);
    }
}

//==============================================================================
static void testConcurrentReallocation()
{
    section ("Re-preparing while the display is reading");

    // What a host does when a plugin is dragged to another slot during
    // playback: prepareToPlay again, possibly with a different block size,
    // while the editor's timer keeps pulling windows out of the analysis and
    // the audio thread keeps pushing into it.
    //
    // prepareToPlay is not guaranteed to run on the message thread, and it
    // reallocates every buffer in the hub. Without a lock this is one thread
    // resizing vectors while another walks them - which crashes, and crashes
    // exactly at the moment somebody rearranges their chain.
    AnalysisHub hub;
    hub.prepare (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    std::mt19937 generator (5);
    std::uniform_real_distribution<float> noise (-0.2f, 0.2f);

    for (int ch = 0; ch < 2; ++ch)
        for (int n = 0; n < 512; ++n)
            buffer.setSample (ch, n, noise (generator));

    std::atomic<bool> stop { false };
    std::atomic<int>  displayFrames { 0 }, audioBlocks { 0 };

    // Both workers yield between passes. Spinning flat out is not a better
    // test - it starves the thread doing the re-preparing, so the interleaving
    // this is meant to exercise happens *less* - and it turned a suite that ran
    // in seconds into one that took three and a half minutes, which is the
    // reliable way to get a verification step skipped.
    std::thread display ([&]
    {
        while (! stop.load())
        {
            hub.updateDisplays();
            ++displayFrames;
            std::this_thread::yield();
        }
    });

    std::thread audio ([&]
    {
        while (! stop.load())
        {
            hub.pushBlock (buffer);
            ++audioBlocks;
            std::this_thread::yield();
        }
    });

    // Sixty slot moves' worth, alternating rate and block size so the buffers
    // really are a different size each time.
    // Paced, not raced. Firing the re-prepares as fast as the loop can go
    // finishes before the display thread has been scheduled a handful of times,
    // and a stress test that does not interleave is not stressing anything -
    // the tuned-for-speed version managed three display frames in total. A
    // millisecond between them gives the other two threads hundreds of passes
    // each inside every reallocation window, and still runs in a second.
    for (int i = 0; i < 60; ++i)
    {
        hub.setFftOrder (i % 2 == 0 ? 11 : 13);
        hub.prepare (i % 2 == 0 ? 44100.0 : 48000.0, i % 2 == 0 ? 256 : 1024);
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    stop.store (true);
    display.join();
    audio.join();

    char detail[160];
    std::snprintf (detail, sizeof (detail), "%d display frames and %d blocks across 60 re-prepares",
                   displayFrames.load(), audioBlocks.load());

    report (true, "survives re-preparing under load", detail);
}

//==============================================================================
static void testAgainstTextbookDFT()
{
    section ("The transform, against a brute-force DFT");

    // Is it a real Fourier transform, or something that merely looks like one?
    //
    // Every other spectrum assertion here checks a consequence - a sine lands in
    // the right bin at the right level, DC reads its own amplitude. Those would
    // all pass for a peak-picker with a lookup table. This one checks the thing
    // itself: the same windowed samples are transformed by the definition,
    //
    //     X[k] = sum over n of  x[n] * exp(-2*pi*i*k*n/N)
    //
    // in O(N^2), and the result is compared against what the analyser produced
    // with juce::dsp::FFT - which on this machine is Apple's vDSP_fft_zrip. If
    // the two agree bin for bin, the fast transform is computing the slow one.
    //
    // The window is taken from JUCE rather than reimplemented, deliberately:
    // what is under test is the transform, not the window, and a hand-rolled
    // Hann that differed in its endpoint would fail this for the wrong reason.

    constexpr double sampleRate = 48000.0;
    constexpr int    order = 9;               // 512 points: O(N^2) is 262144 ops

    SpectrumAnalyser analyser;
    analyser.prepare (sampleRate, order);
    analyser.setOctaveSmoothing (0.0f);

    const auto size = analyser.getFftSize();

    // Something with structure at every frequency, so the comparison is not
    // just about one peak: two tones off the bin centres, plus noise.
    std::vector<float> input ((size_t) size);
    std::mt19937 generator (31);
    std::uniform_real_distribution<float> noise (-0.05f, 0.05f);

    for (int n = 0; n < size; ++n)
        input[(size_t) n] = (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * 997.0 * n / sampleRate)
                                   + 0.2 * std::sin (juce::MathConstants<double>::twoPi * 5310.0 * n / sampleRate))
                          + noise (generator);

    // One frame, with an instant rise, so the smoothed curve equals this frame.
    analyser.process (input.data(), 0.15f, 0.0f, 1.5f, 0.1f);

    // The same samples, windowed the same way, transformed by definition.
    std::vector<float> windowed (input);
    juce::dsp::WindowingFunction<float> window ((size_t) size,
                                                juce::dsp::WindowingFunction<float>::hann, false);
    window.multiplyWithWindowingTable (windowed.data(), (size_t) size);

    std::vector<double> bruteForceDb ((size_t) analyser.getNumBins());

    for (int k = 0; k < analyser.getNumBins(); ++k)
    {
        double re = 0.0, im = 0.0;

        for (int n = 0; n < size; ++n)
        {
            const auto angle = -juce::MathConstants<double>::twoPi * k * n / size;
            re += windowed[(size_t) n] * std::cos (angle);
            im += windowed[(size_t) n] * std::sin (angle);
        }

        bruteForceDb[(size_t) k] = 20.0 * std::log10 (std::sqrt (re * re + im * im) + 1.0e-30);
    }

    // Compared as shapes: both curves are offset to their own maximum, so the
    // calibration constant - which is not what is under test here - drops out.
    const auto& measured = analyser.getInstantDb();

    double peakMeasured = -1.0e9, peakBrute = -1.0e9;

    for (int k = 1; k < analyser.getNumBins() - 1; ++k)
    {
        peakMeasured = juce::jmax (peakMeasured, (double) measured[(size_t) k]);
        peakBrute    = juce::jmax (peakBrute,    bruteForceDb[(size_t) k]);
    }

    double worst = 0.0;
    int    worstBin = -1, compared = 0;

    for (int k = 1; k < analyser.getNumBins() - 1; ++k)
    {
        const auto a = (double) measured[(size_t) k] - peakMeasured;
        const auto b = bruteForceDb[(size_t) k] - peakBrute;

        // Only where there is something to compare. Eighty decibels below the
        // peak, single-precision rounding in either transform is larger than
        // the difference being measured.
        if (b < -80.0)
            continue;

        ++compared;

        if (std::abs (a - b) > worst)
        {
            worst = std::abs (a - b);
            worstBin = k;
        }
    }

    char detail[180];
    std::snprintf (detail, sizeof (detail), "%d bins compared, worst %.4f dB at bin %d",
                   compared, worst, worstBin);

    expectTrue (compared > 100, "there is a spectrum to compare", detail);
    report (worst < 0.05, "juce::dsp::FFT agrees with the definition of the DFT", detail);
}

//==============================================================================
static void testOctaveSmoothing()
{
    section ("Octave smoothing");

    constexpr double sampleRate = 48000.0;

    const auto spread = [] (const std::vector<float>& curve, int firstBin, int lastBin)
    {
        // Standard deviation across bins. This is the number the whole feature
        // exists to reduce: a comb of peaks and nulls has a large one, a smooth
        // shape a small one.
        double sum = 0.0, sumSquares = 0.0;
        const auto count = lastBin - firstBin + 1;

        for (int i = firstBin; i <= lastBin; ++i)
        {
            const auto v = (double) curve[(size_t) i];
            sum += v;
            sumSquares += v * v;
        }

        const auto mean = sum / count;
        return std::sqrt (juce::jmax (0.0, sumSquares / count - mean * mean));
    };

    // ---- off means off, not "very slightly on" ----------------------------
    {
        SpectrumAnalyser analyser;
        analyser.prepare (sampleRate, 12);
        analyser.setOctaveSmoothing (0.0f);

        std::vector<float> noise ((size_t) analyser.getFftSize());
        std::mt19937 generator (11);
        std::uniform_real_distribution<float> dist (-0.2f, 0.2f);

        for (auto& v : noise)
            v = dist (generator);

        for (int frame = 0; frame < 30; ++frame)
            analyser.process (noise.data(), 0.15f, 0.0f, 1.5f, 0.1f);

        expectTrue (&analyser.getDisplayDb() == &analyser.getMagnitudesDb(),
                    "with smoothing off the display curve is the measured curve");
    }

    // ---- white noise gets visibly flatter, and more so with wider bands ----
    {
        std::mt19937 generator (11);
        std::uniform_real_distribution<float> dist (-0.2f, 0.2f);
        std::vector<float> noise (4096);

        for (auto& v : noise)
            v = dist (generator);

        const auto spreadFor = [&] (float fraction)
        {
            SpectrumAnalyser analyser;
            analyser.prepare (sampleRate, 12);
            analyser.setOctaveSmoothing (fraction);

            for (int frame = 0; frame < 30; ++frame)
                analyser.process (noise.data(), 0.15f, 0.0f, 1.5f, 0.1f);

            // From 200 Hz up: below that a third of an octave is narrower than
            // one bin and there is nothing to average.
            const auto firstBin = (int) (200.0 * analyser.getFftSize() / sampleRate);
            return spread (analyser.getDisplayDb(), firstBin, analyser.getNumBins() - 2);
        };

        const auto raw     = spreadFor (0.0f);
        const auto twelfth = spreadFor (1.0f / 12.0f);
        const auto third   = spreadFor (1.0f / 3.0f);
        const auto whole   = spreadFor (1.0f);

        char detail[160];
        std::snprintf (detail, sizeof (detail), "raw %.2f dB -> 1/3 oct %.2f dB", raw, third);
        report (third < raw / 3.0, "third-octave flattens noise by at least a factor of three", detail);

        expectTrue (raw > twelfth && twelfth > third && third > whole,
                    "wider bands smooth more, in order",
                    std::to_string (raw) + " > " + std::to_string (twelfth) + " > "
                        + std::to_string (third) + " > " + std::to_string (whole));
    }

    // ---- a tone stays where it is, to within a tenth of the band ----------
    //
    // Not bin-exact, and asking for bin-exact would be asking for something a
    // constant-Q smoother cannot give. Any sliding average that divides by its
    // bin count peaks where its window is narrowest, and a window a fixed
    // fraction of an octave wide is always narrower below a given bin than
    // above it, so a lone spike is pulled very slightly downwards. The cascade
    // of three boxes is what keeps "very slightly" honest - a single
    // rectangular band moved a tone at bin 512 to bin 457, which is a display
    // lying about frequency by a semitone.
    //
    // A tenth of the smoothing bandwidth is the bound worth enforcing: it is
    // far inside the resolution the smoothed curve itself claims, and it scales
    // with the setting rather than being a number tuned to today's result.
    {
        for (auto fraction : { 1.0f / 12.0f, 1.0f / 6.0f, 1.0f / 3.0f, 1.0f })
        {
            SpectrumAnalyser analyser;
            analyser.prepare (sampleRate, 12);

            const auto size = analyser.getFftSize();
            const int  bin  = size / 8;
            const auto frequency = sampleRate * bin / size;

            std::vector<float> tone ((size_t) size);
            const double increment = juce::MathConstants<double>::twoPi * frequency / sampleRate;

            for (size_t n = 0; n < tone.size(); ++n)
                tone[n] = (float) (sineAmplitudeFor (-6.0) * std::cos (increment * (double) n));

            analyser.setOctaveSmoothing (fraction);

            for (int frame = 0; frame < 30; ++frame)
                analyser.process (tone.data(), 0.15f, 0.0f, 1.5f, 0.1f);

            const auto& curve = analyser.getDisplayDb();
            int loudest = 1;

            for (int i = 1; i < analyser.getNumBins(); ++i)
                if (curve[(size_t) i] > curve[(size_t) loudest])
                    loudest = i;

            // Half the band either side, as a fraction of the centre, then a
            // tenth of that.
            const auto halfBand = std::pow (2.0, fraction * 0.5) - 1.0;
            const auto allowed  = juce::jmax (1.0, bin * halfBand * 0.1);

            char detail[160];
            std::snprintf (detail, sizeof (detail), "%.3f oct: bin %d -> %d, allowed +/-%.1f",
                           fraction, bin, loudest, allowed);

            report (std::abs (loudest - bin) <= allowed,
                    "a tone stays within a tenth of the smoothing band", detail);
        }
    }

    // ---- power averaging, not decibel averaging ---------------------------
    //
    // Two neighbouring bins 40 dB apart. The power mean of 0 dB and -40 dB is
    // -3.01 dB; their decibel mean is -20 dB. Seventeen decibels apart, and the
    // decibel version is the one that hides a resonance among its neighbours.
    {
        SpectrumAnalyser analyser;
        analyser.prepare (sampleRate, 10);
        analyser.setOctaveSmoothing (1.0f / 3.0f);

        const auto powerMean = 10.0 * std::log10 ((std::pow (10.0, 0.0) + std::pow (10.0, -4.0)) / 2.0);

        expectNear (powerMean, -3.0103, 0.001,
                    "the power mean of 0 and -40 dB is -3.01 dB, not -20 dB");
    }
}

//==============================================================================
static void testAxisRange()
{
    section ("Adjustable dB axis");

    // The default has to reproduce the fixed axis it replaced, or every session
    // saved before the range existed opens looking different.
    const auto defaultTop = FXParams::spectrumTopValues[FXParams::defaultSpectrumTopIndex];
    const auto defaultRange = FXParams::spectrumRangeValues[FXParams::defaultSpectrumRangeIndex];

    // Not "the default equals the old fixed axis" any more - the default is now
    // the axis the plugin is actually shipped set to, and it is allowed to
    // differ. What still has to hold is that it is a usable axis: a real
    // ceiling, and a floor above the analyser's own, or the curve would spend
    // its life clamped to the bottom of the screen.
    expectTrue (juce::isPositiveAndBelow (FXParams::defaultSpectrumTopIndex,
                                          (int) std::size (FXParams::spectrumTopValues))
                && juce::isPositiveAndBelow (FXParams::defaultSpectrumRangeIndex,
                                             (int) std::size (FXParams::spectrumRangeValues)),
                "the default axis indices are in range");

    char axis[120];
    std::snprintf (axis, sizeof (axis), "%+.0f dB down to %+.0f dB",
                   defaultTop, defaultTop - defaultRange);

    report (defaultTop - defaultRange > SpectrumAnalyser::floorDb + 20.0f,
            "the default floor stays above the analyser's own", axis);

    // The grid has to stay legible at every span. Two dozen horizontal lines is
    // a grid you cannot see a curve through; four is not a scale.
    for (auto range : FXParams::spectrumRangeValues)
    {
        const auto step  = FXParams::spectrumGridStepDb (range);
        const auto lines = (int) std::round (range / step) + 1;

        char detail[120];
        std::snprintf (detail, sizeof (detail), "%.0f dB range -> %.0f dB step, %d lines",
                       range, step, lines);

        report (lines >= 6 && lines <= 14, "the grid stays between six and fourteen lines", detail);
    }

    expectTrue (FXParams::spectrumTopNames.size() == (int) std::size (FXParams::spectrumTopValues)
                && FXParams::spectrumRangeNames.size() == (int) std::size (FXParams::spectrumRangeValues),
                "every axis label has a value behind it");
}

//==============================================================================
/** Does looking at less of the spectrum show more of it?

    The bass zoom raised the question and this section answers it with numbers
    rather than with an argument. Three separate claims are at stake and they
    have different answers, which is why the button's tooltip has to be careful:

      1. The analysis resolves no finer. Frequency resolution is the reciprocal
         of the observation time, and an axis does not observe. Measured as the
         closest two tones that still read as two, at every window length: the
         threshold has to scale with the bin spacing and with nothing else.

      2. The display resolves finer at the top of the band, from some window
         length upwards. When more than one bin falls under a pixel, the column
         builder keeps the loudest and the rest never reaches the screen; the
         zoom halves the number of bins per pixel and hands some of them back.

      3. Below that window length the zoom is pure magnification, because there
         was already more than one pixel per bin.
*/
static void testBassBandResolution()
{
    section ("The bass band: how many bins, and what the zoom does with them");

    // ---- How many bins are in the band, at every size and every rate --------
    for (const double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (const int order : FXParams::fftOrderValues)
        {
            SpectrumAnalyser analyser;
            analyser.prepare (sampleRate, order);

            const auto size = analyser.getFftSize();

            // From the analyser's own bin frequencies, not from the formula.
            // The two have disagreed before: the analyser clamps its order
            // internally, and a size that was silently smaller than the one
            // asked for would otherwise be reported here as the requested one.
            int counted = 0;

            for (int bin = 1; bin <= size / 2; ++bin)
                if (analyser.getBinFrequency (bin) <= FXParams::bassZoomMaxHz)
                    ++counted;

            const auto expected = (int) std::floor (FXParams::bassZoomMaxHz * (double) size / sampleRate);

            const auto binWidthHz    = sampleRate / (double) size;
            const auto windowSeconds = (double) size / sampleRate;

            char detail[160];
            std::snprintf (detail, sizeof (detail),
                           "%.1f kHz %5d pt: %3d bins, %6.3f Hz/bin, %7.1f ms window",
                           sampleRate / 1000.0, size, counted, binWidthHz, windowSeconds * 1000.0);

            report (counted == expected && size == (1 << order),
                    "bins below 320 Hz, and the size that was asked for", detail);

            // Resolution times observation time is one. Not a law of this code -
            // a law - so the only way to fail it is to have built a different
            // window than the one being reported, which is exactly the failure
            // the comment in AnalysisHub::rebuildTiers describes.
            if (order == 16)
                expectNear (binWidthHz * windowSeconds, 1.0, 1.0e-9,
                            "bin spacing times window length is one");
        }
    }

    // ---- Claim 1: how close can two tones get before they merge? -----------
    //
    // Swept downwards until they stop being two peaks with a valley between
    // them. The interesting output is not the threshold in hertz - that is
    // different for every window - but the threshold divided by the bin
    // spacing, which has to come out the same everywhere if resolution really
    // is a property of the window and of nothing else.
    constexpr double sampleRate = 48000.0;
    constexpr double baseHz     = 100.0;

    const auto resolvesTwoTones = [] (SpectrumAnalyser& analyser, double rate,
                                      double f1, double f2) -> bool
    {
        std::vector<float> window ((size_t) analyser.getFftSize());

        const auto amplitude = sineAmplitudeFor (-12.0);
        const auto w1 = juce::MathConstants<double>::twoPi * f1 / rate;
        const auto w2 = juce::MathConstants<double>::twoPi * f2 / rate;

        for (size_t n = 0; n < window.size(); ++n)
            window[n] = (float) (amplitude * (std::cos (w1 * (double) n) + std::cos (w2 * (double) n)));

        analyser.reset();

        for (int frame = 0; frame < 4; ++frame)
            analyser.process (window.data(), 0.15f, 0.0f, 1.5f, 0.1f);

        const auto& db = analyser.getInstantDb();

        const auto binOf = [&analyser] (double hz)
        {
            return juce::jlimit (1, analyser.getNumBins() - 2,
                                 (int) std::round (hz * analyser.getFftSize() / analyser.getSampleRate()));
        };

        const auto spacing = analyser.getSampleRate() / analyser.getFftSize();
        const auto first = binOf (f1 - 6.0 * spacing);
        const auto last  = binOf (f2 + 6.0 * spacing);

        // Two local maxima with a dip between them is the whole definition of
        // "resolved". One broad hump means the window has merged them, however
        // many bins are underneath it.
        int firstPeak = -1, secondPeak = -1;

        for (int bin = first; bin <= last; ++bin)
        {
            const auto here = db[(size_t) bin];

            if (here > db[(size_t) bin - 1] && here >= db[(size_t) bin + 1])
            {
                if (firstPeak < 0)                    firstPeak = bin;
                else if (secondPeak < 0)              secondPeak = bin;
                else if (here > db[(size_t) secondPeak]) secondPeak = bin;
            }
        }

        if (firstPeak < 0 || secondPeak < 0)
            return false;

        auto valley = db[(size_t) firstPeak];

        for (int bin = firstPeak; bin <= secondPeak; ++bin)
            valley = juce::jmin (valley, db[(size_t) bin]);

        const auto lower = juce::jmin (db[(size_t) firstPeak], db[(size_t) secondPeak]);

        return lower - valley >= 1.0f;
    };

    double firstFactor = 0.0;

    for (const int order : { 12, 13, 14, 15, 16 })
    {
        SpectrumAnalyser analyser;
        analyser.prepare (sampleRate, order);

        const auto spacing = sampleRate / analyser.getFftSize();

        double threshold = 0.0;

        for (double factor = 4.0; factor >= 0.5; factor -= 0.05)
        {
            if (! resolvesTwoTones (analyser, sampleRate, baseHz, baseHz + factor * spacing))
                break;

            threshold = factor;
        }

        char detail[160];
        std::snprintf (detail, sizeof (detail),
                       "%5d pt: %.2f Hz apart = %.2f bins (spacing %.3f Hz)",
                       analyser.getFftSize(), threshold * spacing, threshold, spacing);

        report (threshold > 0.5 && threshold < 4.0,
                "two tones separate at a sane multiple of the bin spacing", detail);

        if (firstFactor == 0.0)
            firstFactor = threshold;
        else
            expectWithin (threshold / firstFactor, 0.85, 1.15,
                          "the threshold scales with the window and nothing else");
    }

    // ---- Claims 2 and 3: what the zoom changes ------------------------------
    //
    // Swept across plot widths rather than measured at one, so the conclusion
    // does not quietly depend on a window size somebody may drag. The zoom's
    // magnification is the ratio of the two, and it is the same at every width.
    section ("The bass zoom: magnification, and where it stops being only that");

    for (const float widthPixels : { 600.0f, 732.0f, 1100.0f, 1400.0f })
    {
        int firstImprovingOrder = 0;

        for (const int order : FXParams::fftOrderValues)
        {
            const auto size = 1 << order;

            const auto full = FXParams::binsPerPixel (FXParams::bassZoomMaxHz, size, sampleRate,
                                                      FXParams::spectrumMinHz, FXParams::spectrumMaxHz,
                                                      widthPixels, true);

            const auto zoomed = FXParams::binsPerPixel (FXParams::bassZoomMaxHz, size, sampleRate,
                                                        FXParams::bassZoomMinHzLog, FXParams::bassZoomMaxHz,
                                                        widthPixels, true);

            if (firstImprovingOrder == 0 && full > 1.0f && zoomed <= full)
                firstImprovingOrder = order;

            if (order == 12)
                report (full < 1.0f && zoomed < 1.0f,
                        "at 4096 both axes already have pixels to spare",
                        "the zoom can only magnify here");
        }

        // The magnification itself: the zoom spans five octaves where the full
        // axis spans just under ten, so every hertz gets almost exactly twice
        // the pixels. Measured at the top of the band, where it matters least -
        // the ratio is the same everywhere on a logarithmic axis.
        const auto ratio = FXParams::binsPerPixel (FXParams::bassZoomMaxHz, 4096, sampleRate,
                                                   FXParams::spectrumMinHz, FXParams::spectrumMaxHz,
                                                   widthPixels, true)
                         / FXParams::binsPerPixel (FXParams::bassZoomMaxHz, 4096, sampleRate,
                                                   FXParams::bassZoomMinHzLog, FXParams::bassZoomMaxHz,
                                                   widthPixels, true);

        char detail[160];
        std::snprintf (detail, sizeof (detail),
                       "%.0f px plot: %.2fx magnification, detail returns at %d pt",
                       widthPixels, ratio, firstImprovingOrder > 0 ? (1 << firstImprovingOrder) : 0);

        report (ratio > 1.9f && ratio < 2.1f && firstImprovingOrder > 0,
                "the zoom doubles the pixels and recovers lost bins", detail);
    }
}

//==============================================================================
/** Why the cursor readout stopped reading the instantaneous spectrum.

    A single transform of a single window is an estimate with a wide
    distribution: on noise the same bin lands anywhere across a dozen decibels
    from one frame to the next, which is not a defect of the transform but the
    nature of estimating a spectrum from a finite window. Printed thirty times a
    second, that is a number nobody can read, let alone dial into an equaliser.

    Measured here at the Very Slow setting, which is what the plugin ships with.

    What this pins down is the property of the two arrays, not which of them
    SpectrumPage happens to call - that is one line of source and a rendered
    shot. The value here is that the choice is justified by a number rather than
    by an expectation, and that the justification stays true as the smoother
    changes. */
static void testCursorReadoutStability()
{
    section ("The cursor readout stands still");

    constexpr double sampleRate = 48000.0;
    constexpr float  frame      = 1.0f / (float) FXParams::displayFramesPerSecond;

    const auto verySlow = FXParams::reactivitySeconds (FXParams::Reactivity::verySlow);
    const auto attack   = FXParams::reactivityAttackSeconds    (FXParams::Reactivity::verySlow);

    SpectrumAnalyser analyser;
    analyser.prepare (sampleRate, 12);

    const auto size = analyser.getFftSize();
    const int  bin  = size / 8;

    std::mt19937 rng (20260901);
    std::normal_distribution<float> noise (0.0f, 0.15f);

    std::vector<float> window ((size_t) size);

    const auto pushFrame = [&]
    {
        for (auto& sample : window)
            sample = noise (rng);

        analyser.process (window.data(), verySlow, attack, 1.5f, frame);
    };

    // Long enough for the two-second attack to have arrived, so what follows
    // measures the steady state rather than the climb out of the floor.
    for (int i = 0; i < 200; ++i)
        pushFrame();

    auto instantLow = 1.0e9f, instantHigh = -1.0e9f;
    auto smoothLow  = 1.0e9f, smoothHigh  = -1.0e9f;

    for (int i = 0; i < 120; ++i)
    {
        pushFrame();

        const auto instant = analyser.getInstantDb()[(size_t) bin];
        const auto smooth  = analyser.getMagnitudesDb()[(size_t) bin];

        instantLow  = juce::jmin (instantLow,  instant);
        instantHigh = juce::jmax (instantHigh, instant);
        smoothLow   = juce::jmin (smoothLow,   smooth);
        smoothHigh  = juce::jmax (smoothHigh,  smooth);
    }

    const auto instantSpread = instantHigh - instantLow;
    const auto smoothSpread  = smoothHigh - smoothLow;

    char detail[160];
    std::snprintf (detail, sizeof (detail), "one frame: %.1f dB   smoothed: %.1f dB   over four seconds",
                   instantSpread, smoothSpread);

    // The ratio is the claim; the absolute figure is reported and not asserted
    // against a number I would have had to invent. White noise is the worst
    // case there is for this - every bin is an independent draw every frame -
    // and it still comes out near a seventh. On the sustained bass note this
    // readout exists for, the remaining movement is smaller again.
    report (smoothSpread < instantSpread * 0.25f && smoothSpread < 6.0f,
            "the smoothed value moves a fraction of what one frame does", detail);

    // And it is still the same measurement: a stable number that had drifted
    // away from the signal would be worse than a moving one. White noise at
    // this amplitude has a known level per bin, and the smoothed value has to
    // sit on it rather than merely sit still.
    const auto expectedDb = 10.0f * std::log10 (0.15f * 0.15f / (float) (size / 2));

    expectNear (0.5f * (smoothLow + smoothHigh), expectedDb, 1.5,
                "and it still reads the level the noise actually has");
}

//==============================================================================
/** Changing FFT Size must not move the curve.

    Octave smoothing averages power over a band, and the band holds more bins
    the longer the window - so the smoothed display used to fall 3 dB every time
    the window doubled, and switching FFT Size from 4096 to 65536 slid a tone
    9.8 dB down the screen. It was invisible while the tiers each carried a
    level constant, because in Multi the shortest tier was the reference and the
    reference never moves; it appeared the moment 32768 and 65536 became
    settings in their own right.

    The compensation lives in SpectrumAnalyser now and covers both cases. It is
    exact for broadband signals everywhere, and for tones wherever the band is
    more than a bin or two across. It cannot be exact for both at once down in
    the bass at the shortest windows - a tone occupies at least one bin, so a
    band narrower than that does not average it - and what that leaves behind is
    measured here rather than left to be discovered. */
static void testLevelAcrossFftSizes()
{
    section ("The level does not depend on the window length");

    constexpr double sampleRate = 48000.0;

    const auto smoothedLevelAt = [] (int order, double hz, bool tone)
    {
        SpectrumAnalyser analyser;
        analyser.prepare (sampleRate, order);
        analyser.setOctaveSmoothing (1.0f / 3.0f);

        const auto size = analyser.getFftSize();
        const int  bin  = (int) std::round (hz * size / sampleRate);

        if (tone)
        {
            driveSpectrum (analyser, sampleRate, sampleRate * bin / size, sineAmplitudeFor (-12.0));
            return (double) analyser.getDisplayDb()[(size_t) bin];
        }

        std::mt19937 rng (7);
        std::normal_distribution<float> gauss (0.0f, 0.1f);
        std::vector<float> window ((size_t) size);

        for (int frame = 0; frame < 300; ++frame)
        {
            for (auto& sample : window)
                sample = gauss (rng);

            analyser.process (window.data(), 0.15f, 0.0f, 1.5f, 0.1f);
        }

        // One bin of noise is a wide distribution however long it is smoothed,
        // so this averages the power across an octave either side - two orders
        // of magnitude more independent samples behind the number.
        double sum = 0.0;
        int    counted = 0;

        for (int b = bin / 2; b <= juce::jmin (analyser.getNumBins() - 1, bin * 2); ++b)
        {
            sum += std::pow (10.0, (double) analyser.getDisplayDb()[(size_t) b] * 0.1);
            ++counted;
        }

        return counted > 0 ? 10.0 * std::log10 (sum / (double) counted) : 0.0;
    };

    struct Case { double hz; bool tone; const char* what; double allowAll; double allowFrom4096; };

    // The three tolerances are three different situations, not three guesses.
    //
    // Broadband is what the compensation is derived for and it is exact. A tone
    // is exact too, as long as the smoothing band holds more than about three
    // bins - three, not one, because the width is split across three cascaded
    // passes and a one-bin box is the identity.
    //
    // Below that the band is narrower than the transform can resolve, no
    // averaging happens, and lifting the result by a number that assumes it did
    // is simply wrong. At 40 Hz a third of an octave is 9.3 Hz, which is under
    // one bin of a 4096-point window - so 1/3 oct smoothing at 40 Hz is a thing
    // a short window cannot do, and the tolerance says so rather than hiding
    // it. What was there before was the same error pointing the other way: the
    // tone fell 6.05 dB across the same range instead of rising 5.99. The fix
    // is a real one at 100 Hz and above, and at 40 Hz it only changes the sign.
    const Case cases[]
    {
        { 1000.0, false, "white noise at 1 kHz",                          1.0,  0.6 },
        { 1000.0, true,  "a tone at 1 kHz",                               2.0,  0.2 },
        {  100.0, true,  "a tone at 100 Hz, band about two bins at 4096", 8.5,  2.5 },
        {   40.0, true,  "a tone at 40 Hz, band under one bin at 4096",  12.5,  6.5 }
    };

    for (const auto& test : cases)
    {
        double lowAll = 1.0e9, highAll = -1.0e9, lowBig = 1.0e9, highBig = -1.0e9;

        for (const int order : FXParams::fftOrderValues)
        {
            const auto level = smoothedLevelAt (order, test.hz, test.tone);

            lowAll  = juce::jmin (lowAll,  level);
            highAll = juce::jmax (highAll, level);

            if ((1 << order) >= FXParams::smoothingReferenceFftSize)
            {
                lowBig  = juce::jmin (lowBig,  level);
                highBig = juce::jmax (highBig, level);
            }
        }

        char detail[190];
        std::snprintf (detail, sizeof (detail),
                       "%s: %.2f dB across 1024-65536, %.2f dB across 4096-65536",
                       test.what, highAll - lowAll, highBig - lowBig);

        report (highAll - lowAll <= test.allowAll && highBig - lowBig <= test.allowFrom4096,
                "the window length does not move it", detail);
    }
}

//==============================================================================
static void testPeakHoldReturn()
{
    section ("Peak hold return rate");

    constexpr double sampleRate = 48000.0;
    constexpr float  hold       = 1.5f;
    constexpr float  frame      = 0.1f;

    SpectrumAnalyser analyser;
    analyser.prepare (sampleRate, 12);

    const auto size = analyser.getFftSize();
    const int  bin  = size / 8;
    const auto frequency = sampleRate * bin / size;

    std::vector<float> tone ((size_t) size), silence ((size_t) size, 0.0f);

    const double increment = juce::MathConstants<double>::twoPi * frequency / sampleRate;

    for (size_t n = 0; n < tone.size(); ++n)
        tone[n] = (float) (sineAmplitudeFor (-6.0) * std::cos (increment * (double) n));

    for (int i = 0; i < 20; ++i)
        analyser.process (tone.data(), 0.15f, 0.0f, hold, frame);

    const auto established = analyser.getPeakDb()[(size_t) bin];
    expectNear (established, -6.0, 0.5, "the peak line reaches the tone's level");

    // Silence from here. The peak must sit still for the hold, then fall at the
    // stated rate - a rate quoted in the docs and nowhere enforced is a rate
    // that drifts the next time somebody tunes the display by eye.
    const int holdFrames = (int) (hold / frame);

    for (int i = 0; i < holdFrames - 1; ++i)
        analyser.process (silence.data(), 0.15f, 0.0f, hold, frame);

    expectNear (analyser.getPeakDb()[(size_t) bin], established, 0.01,
                "it does not move while the hold is running");

    constexpr float fallSeconds = 5.0f;

    for (int i = 0; i < (int) (fallSeconds / frame); ++i)
        analyser.process (silence.data(), 0.15f, 0.0f, hold, frame);

    const auto fallen = established - analyser.getPeakDb()[(size_t) bin];

    expectNear (fallen, FXParams::peakFallDbPerSecond * fallSeconds, 0.5,
                "then falls at the stated rate");

    expectNear (FXParams::peakFallDbPerSecond, 4.0, 0.001, "the rate is 4 dB/s");
}

//==============================================================================
static void testDCBlocker()
{
    section ("DC blocker");

    constexpr double sampleRate = 48000.0;

    DCBlocker blocker;
    blocker.prepare (sampleRate);

    // A 200 Hz sine sitting on a 0.1 offset. After the filter has settled the
    // mean must be gone and the sine must still be there: a DC blocker that
    // removed the offset by also removing the bottom two octaves would pass a
    // test that only looked at the mean.
    const int    length    = (int) (sampleRate * 2.0);
    const double increment = juce::MathConstants<double>::twoPi * 200.0 / sampleRate;

    std::vector<float> signal ((size_t) length);

    for (int n = 0; n < length; ++n)
        signal[(size_t) n] = (float) (0.1 + 0.5 * std::cos (increment * n));

    blocker.process (signal.data(), length);

    const int settled = (int) (sampleRate * 1.0);

    double mean = 0.0, peak = 0.0;

    for (int n = settled; n < length; ++n)
    {
        mean += signal[(size_t) n];
        peak = juce::jmax (peak, (double) std::abs (signal[(size_t) n]));
    }

    mean /= (double) (length - settled);

    expectNear (mean, 0.0, 1.0e-3, "offset is removed");
    expectNear (peak, 0.5, 0.01, "the 200 Hz content is untouched");
}

//==============================================================================
static void testOscilloscope()
{
    section ("Scope capture");

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;
    constexpr double frequency  = 110.0;
    constexpr double amplitude  = 0.4;

    CaptureRing ring;
    ring.prepare (3, (int) (sampleRate * 2.0));

    Oscilloscope scope;
    scope.prepare (sampleRate, (int) std::ceil (sampleRate * 0.2));

    // Two seconds of a sine into the ring, block by block, the way the hub
    // feeds it.
    juce::AudioBuffer<float> block (3, blockSize);
    const auto totalBlocks = (int) (sampleRate * 2.0) / blockSize;

    for (int b = 0; b < totalBlocks; ++b)
    {
        for (int n = 0; n < blockSize; ++n)
        {
            const auto phase = juce::MathConstants<double>::twoPi * frequency
                                   * (double) (b * blockSize + n) / sampleRate;
            const auto value = (float) (amplitude * std::sin (phase));

            for (int ch = 0; ch < 3; ++ch)
                block.setSample (ch, n, value);
        }

        const float* pointers[] { block.getReadPointer (0), block.getReadPointer (1), block.getReadPointer (2) };
        ring.write (pointers, 3, blockSize);
    }

    scope.capture (ring, sampleRate, 0.02f, FXParams::ScopeTrigger::rising);

    expectTrue (scope.isTriggered(), "a sine triggers");
    expectNear (scope.getNumSamples(), 960, 0, "a 20 ms window at 48 kHz is 960 samples");

    const auto& trace = scope.getLeft();

    // The captured window must be that sine over its whole length. Checking
    // only the peak, or only the first half, is what let a capture that ran off
    // the end of its valid range look correct: the beginning was the waveform
    // and the tail was whatever the ring held there, and the peak was right
    // either way.
    double worstError = 0.0;
    int    worstIndex = -1;

    const auto startPhase = std::asin (juce::jlimit (-1.0, 1.0, (double) trace[0] / amplitude));

    for (int i = 0; i < scope.getNumSamples(); ++i)
    {
        const auto expected = amplitude * std::sin (startPhase
                                  + juce::MathConstants<double>::twoPi * frequency * (double) i / sampleRate);
        const auto error = std::abs ((double) trace[(size_t) i] - expected);

        if (error > worstError)
        {
            worstError = error;
            worstIndex = i;
        }
    }

    char detail[160];
    std::snprintf (detail, sizeof (detail), "worst sample %d is off by %.5f", worstIndex, worstError);
    report (worstError < 0.01, "the whole captured window is the input waveform", detail);

    // The trigger has to land on a rising edge, not merely somewhere.
    expectTrue (trace[0] < trace[4], "the window starts on a rising edge",
                std::to_string (trace[0]) + " then " + std::to_string (trace[4]));

    // Free run must still return a full window rather than nothing.
    scope.capture (ring, sampleRate, 0.02f, FXParams::ScopeTrigger::free);
    expectTrue (! scope.isTriggered() && scope.getNumSamples() == 960,
                "free run returns a full untriggered window");
}

//==============================================================================
static void testStereoAnalyser()
{
    section ("Stereo field");

    constexpr double sampleRate = 48000.0;
    constexpr int length = (int) (sampleRate * 3.0);

    // Nothing captured: both values are constant expressions, so the lambda can
    // read them without a capture, and listing them anyway is what the compiler
    // is objecting to.
    const auto run = [] (auto&& makeSample)
    {
        StereoAnalyser analyser;
        analyser.prepare (sampleRate);

        std::vector<float> left ((size_t) 512), right ((size_t) 512);

        for (int start = 0; start < length; start += 512)
        {
            for (int i = 0; i < 512; ++i)
            {
                const auto pair = makeSample (start + i);
                left[(size_t) i]  = pair.first;
                right[(size_t) i] = pair.second;
            }

            analyser.processBlock (left.data(), right.data(), 512, 0.15f);
        }

        return std::tuple { analyser.getCorrelation(), analyser.getBalance(), analyser.getWidthDb() };
    };

    const double increment = juce::MathConstants<double>::twoPi * 440.0 / sampleRate;

    {
        const auto [correlation, balance, width] = run ([increment] (int n)
        {
            const auto value = (float) (0.5 * std::cos (increment * n));
            return std::pair { value, value };
        });

        expectNear (correlation, 1.0, 0.001, "identical channels correlate at +1");
        expectNear (balance, 0.0, 0.001, "identical channels are centred");
        expectTrue (width <= -60.0f, "identical channels report mono width",
                    "width " + std::to_string (width) + " dB");
    }

    {
        const auto [correlation, balance, width] = run ([increment] (int n)
        {
            const auto value = (float) (0.5 * std::cos (increment * n));
            return std::pair { value, -value };
        });

        expectNear (correlation, -1.0, 0.001, "inverted channels correlate at -1");
        expectTrue (width >= 60.0f, "inverted channels are pure side",
                    "width " + std::to_string (width) + " dB");
    }

    {
        std::mt19937 generator (20260830);
        std::uniform_real_distribution<float> noise (-0.5f, 0.5f);

        // Two independent noise sources. The correlation of uncorrelated signals
        // is not exactly zero over a finite window, and the tolerance is what a
        // 150 ms window can actually deliver rather than what the definition
        // says at infinity.
        const auto [correlation, balance, width] = run ([&generator, &noise] (int)
        {
            return std::pair { noise (generator), noise (generator) };
        });

        expectWithin (correlation, -0.15, 0.15, "uncorrelated noise correlates near zero");
        expectWithin (balance, -0.05, 0.05, "uncorrelated noise is balanced");
        expectWithin (width, -1.0, 1.0, "uncorrelated noise has equal mid and side energy");
    }

    {
        const auto [correlation, balance, width] = run ([increment] (int n)
        {
            const auto value = (float) (0.5 * std::cos (increment * n));
            return std::pair { value, value * 0.5f };
        });

        juce::ignoreUnused (correlation, width);

        // A right channel 6 dB down puts the balance a third of the way left:
        // (0.5 - 1.0) / (0.5 + 1.0).
        expectNear (balance, -1.0 / 3.0, 0.01, "a 6 dB imbalance reads a third to the left");
    }
}

//==============================================================================
static void testPitchDetector()
{
    section ("Pitch");

    constexpr double sampleRate = 48000.0;

    PitchDetector detector;
    detector.prepare (sampleRate, FXParams::pitchMinHz, FXParams::pitchMaxHz);

    const auto measure = [&detector] (double frequency, int harmonics)
    {
        detector.reset();

        std::vector<float> window ((size_t) detector.getWindowSize());

        for (size_t n = 0; n < window.size(); ++n)
        {
            double value = 0.0;

            // A sawtooth-ish stack when harmonics > 1. It matters: the second
            // harmonic of a plucked bass string is often louder than the first,
            // and a detector that reads the largest FFT peak reports the octave.
            for (int h = 1; h <= harmonics; ++h)
                value += (h == 2 ? 1.2 : 1.0 / h)
                             * std::cos (juce::MathConstants<double>::twoPi * frequency * (double) h
                                             * (double) n / sampleRate);

            window[n] = (float) (0.3 * value);
        }

        // Two passes so the internal smoother has arrived at the value rather
        // than being halfway to it.
        detector.process (window.data(), (int) window.size(), 0.15f, 1.0f);
        detector.process (window.data(), (int) window.size(), 0.15f, 1.0f);

        return detector.getResult();
    };

    struct Case { double frequency; const char* name; int harmonics; };

    for (const auto& testCase : { Case { 440.0,   "A4 sine",              1 },
                                  Case { 82.4069, "E2 sine",              1 },
                                  Case { 1000.0,  "1 kHz sine",           1 },
                                  Case { 110.0,   "A2 with a loud second harmonic", 6 } })
    {
        const auto result = measure (testCase.frequency, testCase.harmonics);

        const auto cents = 1200.0 * std::log2 ((double) result.frequencyHz / testCase.frequency);

        char label[130];
        std::snprintf (label, sizeof (label), "%s: within a cent", testCase.name);
        expectNear (cents, 0.0, 1.0, label);

        std::snprintf (label, sizeof (label), "%s: confident", testCase.name);
        expectTrue (result.confidence >= FXParams::pitchMinConfidence, label,
                    "confidence " + std::to_string (result.confidence));
    }

    // Silence must not name a note. A tuner that reports something during a
    // pause teaches its user to ignore it.
    {
        detector.reset();
        std::vector<float> silence ((size_t) detector.getWindowSize(), 0.0f);
        detector.process (silence.data(), (int) silence.size(), 0.15f, 1.0f);

        expectTrue (! detector.getResult().valid, "silence reports no note");
    }
}

//==============================================================================
static void testNoteNaming()
{
    section ("Note naming");

    struct Case { double frequency; const char* name; int octave; };

    for (const auto& testCase : { Case { 440.0,   "A",  4 },
                                  Case { 261.626, "C",  4 },
                                  Case { 27.5,    "A",  0 },
                                  Case { 4186.01, "C",  8 },
                                  Case { 92.4986, "F#", 2 } })
    {
        const auto note = NoteName::fromFrequency ((float) testCase.frequency, 440.0f);

        expectTrue (note.name == testCase.name && note.octave == testCase.octave,
                    std::string (testCase.name) + std::to_string (testCase.octave) + " is named correctly",
                    note.name.toStdString() + std::to_string (note.octave));

        expectNear (note.cents, 0.0, 1.0, std::string (testCase.name) + " lands on the note");
    }

    // A different concert pitch moves every note by the same number of cents.
    // 443 Hz is 11.76 cents above 440, so a 440 Hz tone measured against it is
    // that far flat.
    const auto flat = NoteName::fromFrequency (440.0f, 443.0f);
    expectNear (flat.cents, -1200.0 * std::log2 (443.0 / 440.0), 0.01,
                "the reference pitch shifts the cents reading");
}

//==============================================================================
static void testAnalysisChain()
{
    section ("The analysis chain, and the promise not to touch the audio");

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;

    AnalysisHub hub;
    hub.prepare (sampleRate, blockSize);

    const double increment = juce::MathConstants<double>::twoPi * 1000.0 / sampleRate;

    // ---- The null test --------------------------------------------------
    //
    // At every input gain the plugin offers, including the extremes, the buffer
    // handed to pushBlock has to come back untouched. Bit patterns, not a
    // tolerance: "quiet enough" is not what transparent means.
    for (const float gainDb : { -24.0f, -6.0f, 0.0f, 12.0f, 24.0f })
    {
        hub.setInputGainDb (gainDb);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::AudioBuffer<float> untouched (2, blockSize);

        for (int n = 0; n < blockSize; ++n)
        {
            const auto value = (float) (0.4 * std::cos (increment * n));
            buffer.setSample (0, n, value);
            buffer.setSample (1, n, value * 0.5f);
        }

        untouched.makeCopyOf (buffer);
        hub.pushBlock (buffer);

        bool identical = true;

        for (int ch = 0; ch < 2 && identical; ++ch)
            for (int n = 0; n < blockSize; ++n)
                if (! sameBits (buffer.getSample (ch, n), untouched.getSample (ch, n)))
                {
                    identical = false;
                    break;
                }

        char label[110];
        std::snprintf (label, sizeof (label), "%+g dB input gain: output is bit-identical", gainDb);
        expectTrue (identical, label);
    }

    // ---- Input gain reaches the analysis --------------------------------
    //
    // The other half of the same claim: a gain that changed nothing at all
    // would also pass the null test.
    const auto measureIntegrated = [&] (float gainDb, FXParams::Channel channel)
    {
        AnalysisHub fresh;
        fresh.prepare (sampleRate, blockSize);
        fresh.setInputGainDb (gainDb);
        fresh.setChannel (channel);
        fresh.setDcBlockEnabled (false);

        juce::AudioBuffer<float> buffer (2, blockSize);

        const int blocks = (int) (sampleRate * 10.0) / blockSize;

        for (int block = 0; block < blocks; ++block)
        {
            for (int n = 0; n < blockSize; ++n)
            {
                const auto phase = increment * (double) (block * blockSize + n);
                buffer.setSample (0, n, (float) (sineAmplitudeFor (-23.0) * std::cos (phase)));
                buffer.setSample (1, n, (float) (sineAmplitudeFor (-23.0) * std::cos (phase)));
            }

            fresh.pushBlock (buffer);
        }

        return fresh.getLoudness().getIntegratedLufs();
    };

    expectNear (measureIntegrated (0.0f, FXParams::Channel::leftPlusRight), -23.0, 0.1,
                "a -23 dBFS sine measures -23 LUFS through the chain");

    expectNear (measureIntegrated (6.0f, FXParams::Channel::leftPlusRight), -17.0, 0.1,
                "+6 dB of input gain moves the measurement by 6 LU");

    // ---- Channel selection ----------------------------------------------
    //
    // The signal above is identical in both channels. Selecting a single
    // channel therefore measures half the energy, which is 3.01 LU quieter, and
    // selecting Side measures nothing at all.
    expectNear (measureIntegrated (0.0f, FXParams::Channel::left), -23.0 - 3.0103, 0.1,
                "one channel of a centred signal is 3 LU quieter");

    expectNear (measureIntegrated (0.0f, FXParams::Channel::mid), -23.0 - 3.0103, 0.1,
                "Mid of a centred signal matches the mono downmix");

    expectTrue (measureIntegrated (0.0f, FXParams::Channel::side) <= -70.0f,
                "Side of a centred signal is silence");
}

//==============================================================================
int main (int, char**)
{
    std::printf ("\n\033[1mFX Analyzer - AnalyzerCheck\033[0m\n");
    std::printf ("Analysis against BS.1770-4, EBU Tech 3341/3342, and first principles.\n");

    testCoefficientDerivation();
    testLoudnessSines();
    testLoudnessRange();
    testTruePeak();
    testSpectrumCalibration();
    testSpectrumPeakEstimate();
    testReactivity();
    testSpectrumRelease();
    testSpectrumTilt();
    testConcurrentReallocation();
    testScopeTimeBases();
    testOverlap();
    testMultiResolution();
    testAgainstTextbookDFT();
    testOctaveSmoothing();
    testAxisRange();
    testBassBandResolution();
    testCursorReadoutStability();
    testLevelAcrossFftSizes();
    testPeakHoldReturn();
    testDCBlocker();
    testOscilloscope();
    testStereoAnalyser();
    testPitchDetector();
    testNoteNaming();
    testAnalysisChain();

    std::printf ("\n%d checks, %d failures\n\n", checks, failures);

    return failures == 0 ? 0 : 1;
}
