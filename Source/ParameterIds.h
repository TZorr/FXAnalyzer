//
//  ParameterIds.h
//  FX Analyzer
//
//  Every parameter id, range, choice list and default in one place, because a
//  parameter id is a contract with the host: once a session has been saved
//  against it, it can never be changed without silently breaking that session.
//  Scattering the strings across the processor and six pages is how a rename
//  gets applied in five of the six places.
//
//  The split between "parameter" and "view state" is deliberate and is the one
//  design decision in this file worth arguing about. Only the five things that
//  change what is *measured* are parameters: input gain, channel, reactivity,
//  DC block, freeze. Everything else - which page is open, log or linear, the
//  scope time base - is view state living in the same ValueTree but
//  outside the parameter set. The alternative, making them all parameters, was
//  rejected because a host would then offer "Spectrum Mode" in an automation
//  lane and write a page change into every preset morph. Nobody automates which
//  tab they are looking at, and the ones who try are told by the DAW that they
//  can, which is worse.
//

#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

namespace FXParams
{
    // ---- Parameter ids. Hosts store these; treat them as immutable. --------
    inline constexpr const char* inputGainDb = "inputGainDb";
    inline constexpr const char* channel     = "channel";
    inline constexpr const char* reactivity  = "reactivity";
    inline constexpr const char* dcBlock     = "dcBlock";
    inline constexpr const char* freeze      = "freeze";

    /** Moves only when the *shape* of the saved state changes, which lets a
        later build read an earlier session as long as nothing structural has
        moved. It is not the plugin version and must not follow it. */
    inline constexpr int stateVersion = 1;

    inline constexpr const char* stateTreeType    = "FXAnalyzerState";
    inline constexpr const char* propStateVersion = "stateVersion";

    // ---- Analysis source ---------------------------------------------------

    /** What the analyzer listens to. Mid and Side are here because half the
        questions asked of a stereo bus - "what is only in the sides?", "is the
        bass mono?" - cannot be answered from L, R or their sum. */
    enum class Channel { leftPlusRight = 0, left, right, mid, side };

    inline const juce::StringArray channelNames { "L + R", "L", "R", "Mid", "Side" };

    // ---- Reactivity --------------------------------------------------------

    /** One control for every time constant on the panel: spectrum smoothing,
        peak-hold fall, goniometer persistence, pitch averaging.
        Named settings rather than a millisecond field, because the useful
        answer is never a number - it is "hold still so I can read it" or
        "follow the transient".

        Ordered slowest first, so that stepping up is always stepping calmer. */
    enum class Reactivity { verySlow = 0, slow, medium, fast };

    inline const juce::StringArray reactivityNames { "Very Slow", "Slow", "Medium", "Fast" };

    /** Seconds for one time constant of the display smoother, per setting. */
    inline constexpr float reactivitySeconds (Reactivity r) noexcept
    {
        switch (r)
        {
            case Reactivity::verySlow: return 2.000f;
            case Reactivity::slow:     return 0.500f;
            case Reactivity::medium:   return 0.150f;
            case Reactivity::fast:     return 0.035f;
        }
        return 0.150f;
    }

    /** Seconds for one time constant of the *rise*, per setting.

        Zero means instant, and that is what the three fast settings use: a
        spectrum that eases up into a transient draws the transient smaller than
        it was, and catching the peak is the whole reason to watch a spectrum
        while mixing.

        Very Slow is the one setting where that is wrong, and the reason it
        needs its own number rather than just a longer release. With an instant
        rise, a two second release does not produce an average - it produces a
        two second peak envelope, a curve that leaps to every transient and then
        creeps down, and comparing two masters through it compares their loudest
        moments rather than their tonal balance. Smoothing both directions
        equally turns it into a running average of the programme, which is the
        thing a master is actually judged on. AnalyzerCheck asserts the
        difference: the same alternating signal settles 10 dB apart under the
        two behaviours. */
    inline constexpr float reactivityAttackSeconds (Reactivity r) noexcept
    {
        return r == Reactivity::verySlow ? 2.000f : 0.0f;
    }

    /** Multi-resolution analysis: several window lengths at once, spliced by
        frequency, each band getting the shortest window that resolves it.

        One window length cannot serve the whole range. At 4096 points and
        48 kHz a bin is 11.72 Hz wide while a semitone at 41 Hz is 2.45 Hz, so
        two neighbouring bass notes land in the same bin and cannot be told
        apart. Lengthening the window fixes the bass and ruins the top, where a
        1.4 second window smears every transient.

        The tiers are not arbitrary. Taking "a bin no wider than a semitone" as
        the criterion, each length is useful down to binWidth / (2^(1/12) - 1):

            4096   11.72 Hz/bin     85 ms window    down to ~200 Hz
            16384   2.93 Hz/bin    341 ms window    down to  ~50 Hz
            65536   0.73 Hz/bin   1365 ms window    down to  ~13 Hz

        The middle tier does not save CPU - it saves sluggishness. Without it,
        50-200 Hz would inherit the 1365 ms window instead of a 341 ms one. */
    enum class SpectrumResolution { single = 0, multi };

    inline const juce::StringArray spectrumResolutionNames { "Single", "Multi" };
    inline constexpr int defaultSpectrumResolutionIndex = 1;

    /** The fixed lengths above whatever "FFT Size" is set to. The tier list is
        filtered to strictly increasing sizes, so setting FFT Size to 16384
        yields two tiers rather than a degenerate third. */
    inline constexpr int multiResolutionOrders[] { 14, 16 };   // 16384, 65536

    inline constexpr int maxSpectrumOrder = 16;

    /** The window length the smoothed display is normalised to.

        Fractional-octave smoothing averages power over a band, and the number
        of bins in that band grows with the window - so the smoothed curve of
        the very same signal sits lower the longer the window, by 3 dB per
        doubling once the band is more than a bin or two wide. That is why the
        tiers needed a level constant to line up, and it is also why changing
        FFT Size used to slide the whole curve down the screen.

        SpectrumAnalyser now compensates per bin against this size, so every
        window length reads what a 4096-point transform would have read, and the
        tiers line up with no constant at all. 4096 is chosen because it is the
        default: the shipped display is then unchanged, and it is the other
        settings that move onto it. */
    inline constexpr int smoothingReferenceFftSize = 4096;

    /** What the smoothed display adds so that the window length does not change
        its level. Exact for broadband signals at every frequency, and for tones
        wherever the smoothing band is more than a bin or two wide - see the
        note above rebuildBands for why nothing can be exact for both. */
    inline float smoothingLevelOffsetDb (int fftSize) noexcept
    {
        return 10.0f * std::log10 ((float) juce::jmax (1, fftSize)
                                       / (float) smoothingReferenceFftSize);
    }

    /** Where one tier hands over to the next: the frequency at which the
        shorter window's bins become wider than a semitone. Derived rather than
        written down, so it follows the sample rate by itself - at 96 kHz every
        crossover doubles, which is exactly right because so does every bin. */
    inline float tierCrossoverHz (double sampleRate, int fftSize) noexcept
    {
        constexpr auto semitone = 0.0594630944f;   // 2^(1/12) - 1
        return (float) (sampleRate / (double) fftSize) / semitone;
    }

    /** Half-width of the blend between two tiers, in octaves. The splice is
        continuous by construction, so this only has to hide the residue of
        window leakage - a third of an octave either side is plenty. */
    inline constexpr float tierBlendOctaves = 0.333f;

    /** Why the tiers agree, and why there is no longer a constant here.

        A bin of the 65536-point transform is sixteen times narrower than one of
        the 4096-point transform and so holds 12 dB less of any broadband
        signal. Left alone, the noise floor would step at every crossover.

        With octave smoothing on, the shape of the fix is arithmetic: for a band
        of fixed *relative* width the bin count is proportional to N, and the
        smoothed display is the mean, sum / count. The sum - the band's power -
        does not depend on N at all, so between two tiers the displayed values
        differ by 10*log10(N2/N1). One constant per tier cancels that, for tones
        and for noise alike, because both are averaged over the same bin count.

        That constant lived here and was wrong in two places at once. It assumed
        the band always holds more than a bin or two, and in the bass it holds
        less than one: at 100 Hz a third of an octave is 23 Hz, which is under a
        single bin of anything shorter than 8192 points. There the smoother does
        nothing, so there is nothing to cancel, and the constant shifted a curve
        that had never been divided. Measured, that put 1024 and 2048 six and
        three decibels out. The same assumption also made the level depend on
        FFT Size: a tone slid 9.8 dB down the screen going from 4096 to 65536.

        The compensation is now per bin, inside SpectrumAnalyser, as the ratio
        of the band width the smoother actually used to the width the same
        frequency would have had on smoothingReferenceFftSize. That is the same
        10*log10(N/Nref) wherever the band is wide, and correctly nothing at all
        where it is narrower than a bin. Measured across 1024 to 65536, a tone
        now holds its level to within 1.5 dB at 40 Hz, 100 Hz and 1 kHz, where
        it used to move by 9.8 to 16.5 dB.

        Multi-resolution still needs smoothing switched on. With no bands there
        is no fixed relative width, nothing is averaged, and the tiers' noise
        floors go back to differing by 10*log10(N2/N1) with nothing to cancel
        it - see multiResolutionAvailable. */

    /** The spectrum's rise time, as an override of what Reactivity would pick.

        Auto is the default and is not a fudge: it keeps the coupling the panel
        was designed around - instant at the three fast settings, matched to the
        release at Very Slow - so a user who never opens this control gets the
        behaviour that was there before it existed.

        The rest is a genuine trade and the numbers are worth having in front of
        you. Measured on a signal alternating between -6 and -26 dBFS, whose
        true mean is -16 dB:

            attack   reaches within 1 dB   reads
            2.00 s              9.8 s      -16.4 dB   the honest average
            1.00 s              4.9 s      -12.8 dB
            0.50 s              2.5 s      -10.1 dB
            0.25 s              1.2 s       -8.4 dB
            instant             0.03 s      -6.3 dB   a peak envelope

        Faster is not better and slower is not better; the left column is how
        long you wait to see a change and the right one is how much the reading
        drifts towards the loud moments while you do. Which matters depends on
        whether you are working or comparing. */
    inline const juce::StringArray spectrumAttackNames
        { "Auto", "2 s", "1 s", "0.5 s", "0.25 s", "Instant" };

    /** Negative means Auto - follow Reactivity. A sentinel rather than a second
        boolean, so there is one value to store, serialise and reason about. */
    inline constexpr float spectrumAttackValues[] { -1.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.0f };

    inline constexpr int defaultSpectrumAttackIndex = 0;

    /** The spectrum's fall time, as an override of what Reactivity would pick.

        The other half of Attack, added on 2026-09-24 so that the two ballistics
        can be set independently while listening rather than only as one of four
        pairs. Auto keeps Reactivity's number, so a session that never touched
        this reads exactly as before.

        Only the spectrum follows it. Reactivity still sets the stereo window,
        the pitch averaging and the peak hold, because those answer different
        questions - see stereoWindowSeconds - and a spectrum release is not a
        correlation window.

        Ordered slowest first, like Attack and Reactivity, so that stepping up
        is always stepping calmer. */
    inline const juce::StringArray spectrumReleaseNames
        { "Auto", "4 s", "2 s", "1 s", "0.5 s", "0.25 s", "0.15 s", "50 ms" };

    /** Negative means Auto, as for Attack. */
    inline constexpr float spectrumReleaseValues[] { -1.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.15f, 0.05f };

    inline constexpr int defaultSpectrumReleaseIndex = 0;

    /** The stereo field's averaging window.

        Its own row rather than the spectrum's, because the two are answering
        different questions. The spectrum is averaging many FFT frames into a
        shape and wants to be slow; correlation is a number somebody watches for
        the moment it dips below zero, and a window as long as the spectrum's
        would hide exactly that. Half a second at Medium is the classic
        correlation-meter window.

        Loudness is deliberately not in this table and must never be: BS.1770
        fixes its windows at 400 ms momentary and 3 s short-term, with a gating
        block of its own. Those are the measurement, not a display preference,
        and a LUFS meter with an adjustable window is not a LUFS meter. */
    inline constexpr float stereoWindowSeconds (Reactivity r) noexcept
    {
        switch (r)
        {
            case Reactivity::verySlow: return 2.00f;
            case Reactivity::slow:     return 1.00f;
            case Reactivity::medium:   return 0.50f;
            case Reactivity::fast:     return 0.10f;
        }
        return 0.50f;
    }

    /** Pitch averaging. Faster than the stereo window at every setting: a tuner
        that lags is a tuner nobody can tune against, and the detector already
        refuses to glide between notes on its own. */
    inline constexpr float pitchSmoothingSeconds (Reactivity r) noexcept
    {
        switch (r)
        {
            case Reactivity::verySlow: return 1.00f;
            case Reactivity::slow:     return 0.50f;
            case Reactivity::medium:   return 0.25f;
            case Reactivity::fast:     return 0.05f;
        }
        return 0.25f;
    }

    /** How fast a peak-hold marker falls once its hold has expired, in dB per
        second. One rate for all four reactivity settings, because the peak line
        answers the same question at every one of them: how far below the
        loudest recent moment is the curve now.

        4 dB/s is a mastering rate. It means a peak that has fallen off the top
        of the scale takes about a quarter of a minute to clear, which is slow
        enough to still be there when you switch back from another page and
        compare - and slow enough to be a nuisance while mixing, where the line
        will still be showing the last chorus during the verse. That is the
        trade this number makes, and it is made in favour of the slow reading. */
    inline constexpr float peakFallDbPerSecond = 4.0f;

    /** How long a peak-hold marker sits before it starts to fall, per setting.
        Very Slow holds longest: at that setting the live curve is an average,
        so the peak line is the only thing on the graph still reporting what the
        loudest moment did. */
    inline constexpr float peakHoldSeconds (Reactivity r) noexcept
    {
        switch (r)
        {
            case Reactivity::verySlow: return 6.0f;
            case Reactivity::slow:     return 3.0f;
            case Reactivity::medium:   return 1.5f;
            case Reactivity::fast:     return 0.5f;
        }
        return 1.5f;
    }

    // ---- Input gain --------------------------------------------------------

    // +/- 24 dB. Wide enough to bring a quiet stem up into the readable part of
    // the scale, narrow enough that the number on the panel stays a trim rather
    // than becoming a fader. It never reaches the output; see PluginProcessor.
    inline constexpr float inputGainMinDb = -24.0f;
    inline constexpr float inputGainMaxDb =  24.0f;

    // ---- View state: property names on the state tree ----------------------
    //
    // These are ValueTree properties, not parameters. They are saved with the
    // session and restored with it, but the host never sees them as automatable
    // values.

    inline constexpr const char* propPage          = "page";
    inline constexpr const char* propFftOrder      = "fftOrder";
    inline constexpr const char* propSpectrumMode  = "spectrumMode";
    inline constexpr const char* propSpectrumScale = "spectrumScale";
    inline constexpr const char* propSpectrumSlope = "spectrumSlope";
    inline constexpr const char* propSpectrumSmoothing = "spectrumSmoothing";
    inline constexpr const char* propSpectrumTop   = "spectrumTop";
    inline constexpr const char* propSpectrumRange = "spectrumRange";
    inline constexpr const char* propSpectrumAttack = "spectrumAttack";
    inline constexpr const char* propSpectrumRelease = "spectrumRelease";
    inline constexpr const char* propSpectrumTiltDb    = "spectrumTiltDb";
    inline constexpr const char* propSpectrumTiltPivot = "spectrumTiltPivot";
    inline constexpr const char* propSpectrumResolution = "spectrumResolution";
    inline constexpr const char* propSpectrumBassZoom   = "spectrumBassZoom";
    inline constexpr const char* propPeakHold      = "peakHold";
    inline constexpr const char* propScopeTimeMs   = "scopeTimeMs";
    inline constexpr const char* propScopeTrigger  = "scopeTrigger";
    inline constexpr const char* propScopeGainDb   = "scopeGainDb";
    inline constexpr const char* propLoudnessTarget = "loudnessTarget";
    inline constexpr const char* propStereoMode    = "stereoMode";
    inline constexpr const char* propStereoZoomDb  = "stereoZoomDb";
    inline constexpr const char* propPitchRefHz    = "pitchRefHz";

    // "themeName" and a <Theme> child node were view state until 0.2, when the
    // user themes went and the design became fixed. Sessions and presets saved
    // before still carry them; nothing reads them, and replaceState keeps them
    // as inert data rather than failing on them.

    /** Set once the standalone build has lifted JUCE's default input mute, so
        that a user who deliberately mutes the input again does not have it
        silently unmuted for them on the next launch. It means "we have had our
        say about this", not "the input is on". */
    inline constexpr const char* propStandaloneInputChosen = "standaloneInputChosen";

    // ---- Pages -------------------------------------------------------------

    enum class Page { spectrum = 0, scope, loudness, stereo, pitch, settings };

    /** How often the panel repaints, and therefore how often the analysis is
        asked to advance. One constant, because the two must agree: the hop cap
        in AnalysisHub exists precisely to keep every tier level with the
        display, and two copies of "30" in two files is a pair that drifts. */
    inline constexpr int displayFramesPerSecond = 30;

    inline constexpr int numPages = 6;

    inline const juce::StringArray pageNames
        { "SPECTRUM", "SCOPE", "LOUDNESS", "STEREO", "PITCH", "SETTINGS" };

    // ---- Spectrum ----------------------------------------------------------

    /** How the spectrum is drawn. Bars 63 was added on 2026-09-24 and is
        appended rather than inserted, because propSpectrumMode stores this
        enum's index: putting it after Bars 31 would have turned every session
        saved on Sonogram (index 2) into a bar graph. */
    enum class SpectrumMode { curve = 0, bars, sonogram, bars63 };

    /** The order the View stepper shows them in, which is not the enum's.
        Stepping from Bars 31 should land on the finer bars, not skip past them
        to the sonogram and come back round. */
    inline constexpr SpectrumMode spectrumModeOrder[]
        { SpectrumMode::curve, SpectrumMode::bars, SpectrumMode::bars63, SpectrumMode::sonogram };

    /** Names in display order - index them with a step index, never with a
        stored mode. */
    inline const juce::StringArray spectrumModeNames { "2D", "Bars 31", "Bars 63", "Sonogram" };

    inline constexpr int lastSpectrumMode = (int) SpectrumMode::bars63;

    inline int spectrumModeToStepIndex (int storedMode) noexcept
    {
        for (int i = 0; i < (int) std::size (spectrumModeOrder); ++i)
            if ((int) spectrumModeOrder[i] == storedMode)
                return i;

        return 0;
    }

    inline int spectrumModeFromStepIndex (int step) noexcept
    {
        return (int) spectrumModeOrder[juce::jlimit (0, (int) std::size (spectrumModeOrder) - 1, step)];
    }

    /** Bars per view, in one place. 31 is a bar per third of an octave from
        20 Hz to 20 kHz - the spacing of a graphic equaliser, and so the one
        people already read. 63 is about a sixth of an octave: two bass notes
        four semitones apart share a bar at 31 and get one each at 63. */
    inline constexpr int spectrumBarCount (SpectrumMode m) noexcept
    {
        return m == SpectrumMode::bars ? 31 : m == SpectrumMode::bars63 ? 63 : 0;
    }

    enum class FrequencyScale { logarithmic = 0, linear };
    inline const juce::StringArray frequencyScaleNames { "Log", "Lin" };

    /** A tilt applied to the drawn curve, in dB per octave, to make broadband
        material sit flat on the screen. 0 is the honest physical spectrum;
        4.5 dB/oct is the one that makes a full mix look like a line rather than
        a ski slope, which is what people are actually comparing against. */
    inline const juce::StringArray spectrumSlopeNames { "0 dB/oct", "3 dB/oct", "4.5 dB/oct", "6 dB/oct" };
    inline constexpr float spectrumSlopeValues[] { 0.0f, 3.0f, 4.5f, 6.0f };

    /** 4.5 dB/oct by default: the tilt that makes a finished master sit flat on
        the screen instead of as a diagonal, and comparing two flat lines by eye
        is far easier than comparing two diagonals. */
    inline constexpr int defaultSpectrumSlopeIndex = 2;

    /** The tilt as a number rather than one of four, since 2026-09-24.

        Four fixed slopes were enough to make a master sit flat and not enough
        to find the slope a particular record sits flat at, which is the thing
        somebody tuning the display by ear is actually doing - a dense mix wants
        4.5, a sparse acoustic one nearer 3, and the answer is often between.
        Half a decibel per octave is the step: finer than anyone can see on the
        curve, coarse enough that a click visibly moves it.

        Stored under a new property as the value itself, not as an index. The
        old `spectrumSlope` index is still read when the new one is absent, so a
        session saved before this change opens on the slope it was saved with -
        see spectrumTiltFromLegacyIndex. Growing the old list instead would have
        changed what every saved index meant. */
    inline constexpr float spectrumTiltMinDb     = 0.0f;
    inline constexpr float spectrumTiltMaxDb     = 6.0f;
    inline constexpr float spectrumTiltStepDb    = 0.5f;

    inline float spectrumTiltFromLegacyIndex (int index) noexcept
    {
        return spectrumSlopeValues[juce::jlimit (0, (int) std::size (spectrumSlopeValues) - 1, index)];
    }

    inline float defaultSpectrumTiltDb() noexcept
    {
        return spectrumSlopeValues[defaultSpectrumSlopeIndex];
    }

    /** The frequency the tilt turns about - the one place on the axis where the
        drawn level is the measured level.

        It moves the whole curve up or down and nothing else: a tilt of s dB/oct
        about p adds s * log2(f / p), and changing p changes only the constant.
        That sounds like nothing and is not. It decides *which* part of the
        spectrum keeps its true height on the dB scale, and so which part the
        Top and Range settings are framing - pivot at 100 Hz and the bass reads
        its real level with the top end lifted over it, pivot at 5 kHz and it is
        the other way round. 1 kHz is where the tilt has always turned. */
    inline const juce::StringArray spectrumTiltPivotNames
        { "100 Hz", "200 Hz", "500 Hz", "1 kHz", "2 kHz", "5 kHz" };

    inline constexpr float spectrumTiltPivotValues[] { 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f };
    inline constexpr int   defaultSpectrumTiltPivotIndex = 3;   // 1 kHz

    /** What the tilt adds at a frequency. One function, so that the curve, the
        bars, the sonogram and AnalyzerCheck agree on it. */
    inline float spectrumTiltAt (float hz, float dbPerOctave, float pivotHz) noexcept
    {
        if (dbPerOctave == 0.0f)
            return 0.0f;

        return dbPerOctave * std::log2 (juce::jmax (1.0f, hz) / juce::jmax (1.0f, pivotHz));
    }

    /** FFT sizes offered on the Settings page. 4096 at 48 kHz gives 11.7 Hz per
        bin - too coarse to separate two low notes, which is why 16384 is here,
        and too fine to follow a transient, which is why 1024 is.

        32768 and 65536 were added for the bass zoom, and they are the only
        thing on the panel that improves the resolution rather than the picture
        of it: frequency resolution is the reciprocal of the window length and
        nothing else, so 0.73 Hz per bin is available exactly once you are
        willing to spend 1.37 seconds observing. They cost little else - vDSP
        does a 65536-point transform in 0.262 ms on this machine, and the tiers
        have been running that length all along. */
    inline const juce::StringArray fftSizeNames
        { "1024", "2048", "4096", "8192", "16384", "32768", "65536" };

    inline constexpr int fftOrderValues[] { 10, 11, 12, 13, 14, 15, 16 };
    inline constexpr int defaultFftOrderIndex = 2;   // 4096

    /** Derived, never written out a second time. The two places that read this
        list used to carry their own copies of "4" and "5", which is the bug
        this project has now had four times: a list grows and its bounds do not.
        See longestScopeSeconds() for the same fix on the scope's time list. */
    inline constexpr int lastFftSizeIndex = (int) std::size (fftOrderValues) - 1;

    /** Fractional-octave smoothing of the spectrum, applied across frequency
        after the time smoothing has done its work across frames.

        The two are not alternatives and neither replaces the other. Time
        smoothing settles a single bin down; it cannot do anything about the
        comb of peaks and nulls that an FFT of real music produces across
        neighbouring bins, because those are all present in every frame. Octave
        smoothing averages a bin together with its neighbours inside a band of
        constant *relative* width, which is the only kind of band that means
        anything musically: a third of an octave is a third of an octave at
        60 Hz and at 6 kHz, while the FFT's own bins are a fixed number of hertz
        apart and so cover four octaves at the bottom of the display and a
        twentieth of one at the top.

        Averaging is done on power, not on decibels. Averaging decibels is
        averaging logarithms, which under-weights the loud bin in a band and
        pulls a resonance down towards its neighbours - the opposite of what
        somebody looking for that resonance wants. Power averaging is the RMS of
        the band, and it is what every third-octave analyser has always shown. */
    inline const juce::StringArray spectrumSmoothingNames
        { "Off", "1/24 oct", "1/12 oct", "1/6 oct", "1/3 oct", "1/1 oct" };

    inline constexpr float spectrumSmoothingFractions[]
        { 0.0f, 1.0f / 24.0f, 1.0f / 12.0f, 1.0f / 6.0f, 1.0f / 3.0f, 1.0f };

    /** A twenty-fourth of an octave by default.

        A third of an octave is the width reference curves are quoted in and it
        makes a mix read as a shape rather than as a hedge - it was the default
        for that reason. It is also 23% wide, which is **four semitones**, so it
        merges neighbouring bass notes no matter how fine the transform beneath
        it is. With multi-resolution analysis underneath, that throws away
        exactly what the 65536-point tier was added to provide.

        1/24 octave is 2.9%, half a semitone: fine enough that two notes a
        semitone apart stay two notes. The curve is busier than at 1/3 - that is
        the trade, and it is the right way round for an analyzer whose job is to
        show what is there. Anyone who wants the smooth shape back has four
        wider settings to choose from.

        Off is still there, and it is the honest one: it shows the measurement
        rather than a smoothed picture of it. Note that Off also drops
        multi-resolution back to a single tier - see spectrumResolutionNames. */
    inline constexpr int defaultSpectrumSmoothingIndex = 4;   // 1/3 oct

    /** Multi-resolution needs bands to splice across, and with smoothing off
        there are none - see the derivation at tierLevelOffsetDb. So `Off`
        silently forced a single tier, the Settings page went on showing
        `Multi`, and the panel said nothing about the disagreement. Reported
        from use as "Resolution sometimes doesn't switch"; it always switched,
        it just could not always take effect.

        The rule lives here so the processor and the panel read the same one.
        Two copies of it would drift, and the drift would look exactly like the
        bug it was written to explain. */
    inline constexpr bool multiResolutionAvailable (int smoothingIndex) noexcept
    {
        return spectrumSmoothingFractions[smoothingIndex] > 0.0f;
    }


    /** The dB axis, as a ceiling and a span rather than a top and a bottom.

        Two controls instead of two edges because that is how the axis is
        actually used. The ceiling is set once, from where the material sits -
        +6 for anything mastered, 0 for a bus with headroom, -12 for a quiet
        stem. The span is what gets changed while working: wide to see the noise
        floor, narrow to magnify the top two octaves of a master where
        everything interesting is inside twenty decibels. Offering "top" and
        "bottom" instead would make every change of ceiling also a change of
        span, which is never what was meant. */
    inline const juce::StringArray spectrumTopNames
        { "+12 dB", "+6 dB", "0 dB", "-6 dB", "-12 dB", "-20 dB" };

    inline constexpr float spectrumTopValues[] { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -20.0f };
    inline constexpr int   defaultSpectrumTopIndex = 3;   // -6 dB

    inline const juce::StringArray spectrumRangeNames
        { "30 dB", "40 dB", "60 dB", "66 dB", "80 dB", "100 dB", "120 dB" };

    inline constexpr float spectrumRangeValues[] { 30.0f, 40.0f, 60.0f, 66.0f, 80.0f, 100.0f, 120.0f };

    /** 66 dB under a +6 ceiling is the panel exactly as it was drawn, down to
        -60. The default has to reproduce the fixed axis it replaces, or every
        session saved before this existed opens looking different. */
    inline constexpr int defaultSpectrumRangeIndex = 4;   // 80 dB: the floor sits at -86

    /** Only what a SpectrumPage holds before it has read the view state - the
        axis itself comes from defaultSpectrumTopIndex and
        defaultSpectrumRangeIndex. These were the fixed axis once, and two
        assertions still checked the defaults against them long after the
        defaults had a reason of their own to differ. */
    inline constexpr float spectrumTopDb    =   6.0f;
    inline constexpr float spectrumBottomDb = -60.0f;
    /** A grid step that keeps roughly a dozen lines on the axis whatever the
        span is. A fixed 6 dB gives seven lines over a 40 dB range and
        twenty-one over 120, and twenty-one horizontal lines is a grid nobody
        can see a curve through. */
    inline constexpr float spectrumGridStepDb (float rangeDb) noexcept
    {
        return rangeDb <= 35.0f  ?  3.0f
             : rangeDb <= 70.0f  ?  6.0f
             : rangeDb <= 100.0f ? 10.0f
                                 : 20.0f;
    }

    inline constexpr float spectrumMinHz    =  20.0f;
    inline constexpr float spectrumMaxHz    = 20000.0f;

    /** The bass zoom, reached from the "B" button on the Spectrum page.

        0 Hz was asked for and 0 Hz is what the linear axis gets. The
        logarithmic one cannot have it - log(0) is not a number and no amount of
        clamping makes the left edge mean anything - so it starts an octave
        below the normal axis instead. Nothing is lost by that: the DC blocker
        corners at 5 Hz, so everything between 0 and 10 Hz has already been
        removed from the signal before the transform sees it. */
    inline constexpr float bassZoomMinHzLog    =  10.0f;
    inline constexpr float bassZoomMinHzLinear =   0.0f;
    inline constexpr float bassZoomMaxHz       = 320.0f;

    /** Where a frequency lands across a plot, as a fraction from 0 to 1.

        This lives here rather than in GraphAxes because it is not only drawing:
        the question "how many bins fall under one pixel" is asked by the
        measurement harness too, and answering it there with a second copy of
        this arithmetic is how the two would come to disagree. GraphAxes calls
        it and adds the rectangle. */
    inline float axisProportion (float hz, float minHz, float maxHz, bool logarithmic) noexcept
    {
        if (maxHz <= minHz)
            return 0.0f;

        if (! logarithmic)
            return (hz - minHz) / (maxHz - minHz);

        const auto safeMin = juce::jmax (1.0e-3f, minHz);
        return std::log (juce::jmax (1.0e-3f, hz) / safeMin) / std::log (maxHz / safeMin);
    }

    inline float axisFrequency (float proportion, float minHz, float maxHz, bool logarithmic) noexcept
    {
        if (! logarithmic)
            return minHz + proportion * (maxHz - minHz);

        const auto safeMin = juce::jmax (1.0e-3f, minHz);
        return safeMin * std::pow (maxHz / safeMin, proportion);
    }

    /** How many transform bins fall under one horizontal pixel at a frequency.

        Below one, the display has more pixels than measurements and interpolates
        between neighbouring bins. Above one, several bins share a pixel and
        SpectrumPage keeps the loudest of them - measured detail that exists in
        the analysis and never reaches the screen. That crossing is the whole
        argument for the bass zoom, and it is a number, so it gets measured. */
    inline float binsPerPixel (float hz, int fftSize, double sampleRate,
                               float minHz, float maxHz, float widthPixels, bool logarithmic) noexcept
    {
        if (widthPixels <= 0.0f || fftSize <= 0)
            return 0.0f;

        const auto proportion = axisProportion (hz, minHz, maxHz, logarithmic);
        const auto neighbour  = axisFrequency (proportion + 1.0f / widthPixels, minHz, maxHz, logarithmic);
        const auto hzPerPixel = std::abs (neighbour - hz);

        const auto binWidthHz = (float) (sampleRate / (double) fftSize);

        return binWidthHz > 0.0f ? hzPerPixel / binWidthHz : 0.0f;
    }

    // ---- Scope -------------------------------------------------------------

    inline const juce::StringArray scopeTimeNames
        { "1 ms", "5 ms", "10 ms", "20 ms", "50 ms", "100 ms", "200 ms", "500 ms", "1 s", "2 s" };

    inline constexpr float scopeTimeValues[]
        { 1.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f };

    /** The longest window, in seconds. Every buffer that has to hold a scope
        window is sized from this rather than from a second copy of the number:
        the maximum used to be written once here and once as `rate * 0.2` where
        the oscilloscope was prepared, and Oscilloscope::capture clamps to its
        buffer *silently* - so adding an entry to the list above was enough to
        produce a display labelled "2 s" showing 200 ms. */
    inline constexpr float longestScopeSeconds()
    {
        float longest = 0.0f;

        for (auto ms : scopeTimeValues)
            longest = ms > longest ? ms : longest;

        return longest / 1000.0f;
    }
    inline constexpr int defaultScopeTimeIndex = 9;  // 2 s

    /** The last valid index, derived rather than counted.

        Adding three entries to the list above broke the Scope page in four
        separate places, every one of them a literal 6 or 7 written when the
        list ended at 200 ms - a clamp here, a loop bound there. None of them
        failed loudly: the page simply fell back to its default, so the new time
        bases existed in the menu and could never be selected. */
    inline constexpr int lastScopeTimeIndex = (int) std::size (scopeTimeValues) - 1;

    /** Free run is not the default. An untriggered scope on musical material is
        a smear, and the first thing anybody does on seeing one is look for the
        trigger. */
    enum class ScopeTrigger { rising = 0, falling, free };
    inline const juce::StringArray scopeTriggerNames { "Rising", "Falling", "Free" };

    /** Free by default, which reverses the argument above for a reason that
        argument could not have anticipated: the default time base is now two
        seconds, and at that length there is no waveform to lock onto - the
        screen shows an envelope. On the short time bases the trigger is still
        the first thing anybody reaches for. */
    inline constexpr int defaultScopeTriggerIndex = (int) ScopeTrigger::free;

    inline const juce::StringArray scopeGainNames { "Auto", "0 dB", "+6 dB", "+12 dB", "+24 dB" };
    inline constexpr int defaultScopeGainIndex = 0;

    // ---- Loudness ----------------------------------------------------------

    inline const juce::StringArray loudnessTargetNames { "-23 LUFS", "-16 LUFS", "-14 LUFS", "-9 LUFS" };
    inline constexpr float loudnessTargetValues[] { -23.0f, -16.0f, -14.0f, -9.0f };
    inline constexpr int defaultLoudnessTargetIndex = 2;   // streaming

    /** Below this many seconds of gated material the integrated readout is
        drawn dimmed. See LoudnessMeter::hasIntegratedFor. */
    inline constexpr double integratedSettleSeconds = 5.0;

    // ---- Stereo ------------------------------------------------------------

    enum class StereoMode { dots = 0, lines };
    inline const juce::StringArray stereoModeNames { "Dots", "Lines" };

    inline const juce::StringArray stereoZoomNames { "0 dB", "+6 dB", "+12 dB", "+18 dB" };
    inline constexpr float stereoZoomValues[] { 0.0f, 6.0f, 12.0f, 18.0f };

    // ---- Pitch -------------------------------------------------------------

    // Concert pitch. 415 Hz is baroque, 443 Hz is where several European
    // orchestras actually sit; a detector fixed at 440 tells them they are all
    // sharp, which is true and useless.
    inline constexpr float pitchRefMinHz     = 415.0f;
    inline constexpr float pitchRefMaxHz     = 466.0f;
    inline constexpr float pitchRefDefaultHz = 440.0f;

    /** The detector reports nothing below this confidence rather than reporting
        a note it does not believe. A tuner that names a note during a cymbal
        crash trains its user to ignore it. */
    inline constexpr float pitchMinConfidence = 0.55f;

    inline constexpr float pitchMinHz =  27.5f;    // A0
    inline constexpr float pitchMaxHz = 4186.0f;   // C8
}
