//
//  EditorShot.cpp
//  FX Analyzer
//
//  Builds the plugin headlessly, opens its editor, and paints each of the six
//  pages into a PNG.
//
//  Two reasons this is a build target rather than a script somebody runs once.
//  The first is plain smoke testing: constructing the processor, preparing it,
//  running audio through it and constructing every page exercises a great deal
//  of code that auval never reaches, and it fails loudly instead of hanging a
//  window somewhere nobody is looking.
//
//  The second is that it renders. A panel is laid out with arithmetic on
//  rectangles, and arithmetic on rectangles is exactly the kind of thing that is
//  obviously right while reading it and obviously wrong once drawn. Six images
//  mean the whole plugin can be looked at without a DAW.
//
//  --demo pushes a signal through first. An empty analyzer is six boxes of grid
//  lines, and a layout checked only in that state is a layout nobody has seen
//  do its job: the curve, the goniometer cloud and the note name all have to be
//  in the picture before anything can be said about the spacing around them.
//
//  There is one design since 0.2 (the Kitbox look), so there is no --theme any
//  more; the hit map holds that one palette to its contrast floors instead.
//
//  --scale <factor> renders at that multiple of the default size (the window is
//  resizable from 0.75 to 2), with the factor in each file name. The graphs
//  grow and the controls do not, so the two ends of the range are where a
//  layout runs out of room or leaves too much of it.
//
//  Usage:  EditorShot <output-directory> [--demo] [--scale <factor>]
//

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginEditor.h"
#include "UI/StepperControl.h"
#include "PluginProcessor.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <atomic>
#include <chrono>
#include <ctime>
#include <random>
#include <thread>

namespace
{
    /** CIE76 delta E between two opaque colours, in Lab under D65: about 2.3
        is a just-noticeable difference, 40 and more is a different colour
        rather than a different shade. Here for the tab strip's check, where the
        selection is carried by hue and a luminance ratio cannot see it. */
    float colourDistance (juce::Colour a, juce::Colour b)
    {
        const auto toLab = [] (juce::Colour c)
        {
            const auto linear = [] (float v)
            {
                return v <= 0.04045f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f);
            };

            const auto red = linear (c.getFloatRed()), green = linear (c.getFloatGreen()), blue = linear (c.getFloatBlue());

            const auto x = (0.4124f * red + 0.3576f * green + 0.1805f * blue) / 0.95047f;
            const auto y = (0.2126f * red + 0.7152f * green + 0.0722f * blue);
            const auto z = (0.0193f * red + 0.1192f * green + 0.9505f * blue) / 1.08883f;

            const auto f = [] (float t)
            {
                return t > 0.008856f ? std::cbrt (t) : 7.787f * t + 16.0f / 116.0f;
            };

            return std::array<float, 3> { 116.0f * f (y) - 16.0f, 500.0f * (f (x) - f (y)), 200.0f * (f (y) - f (z)) };
        };

        const auto la = toLab (a), lb = toLab (b);
        return std::sqrt ((la[0] - lb[0]) * (la[0] - lb[0]) + (la[1] - lb[1]) * (la[1] - lb[1])
                            + (la[2] - lb[2]) * (la[2] - lb[2]));
    }

    /** A signal with something to say on every page: a pitched note with
        harmonics for Pitch and Spectrum, broadband noise so the top of the
        spectrum is not empty, a slow tremolo so Loudness has a range to report,
        and a decorrelated right channel so the goniometer draws a cloud rather
        than a line. Synthesised rather than shipped, because the one thing this
        target must not need is an asset somebody has to remember to keep. */
    void fillDemoBlock (juce::AudioBuffer<float>& buffer, int64_t startSample, double sampleRate,
                        std::mt19937& generator)
    {
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

        constexpr double fundamental = 110.0;   // A2

        for (int n = 0; n < buffer.getNumSamples(); ++n)
        {
            const double t = (double) (startSample + n) / sampleRate;

            double tone = 0.0;

            // A sawtooth-ish stack, six harmonics deep. The second harmonic is
            // deliberately the loudest: it is what a plucked string does, and it
            // is the case a pitch detector reading the biggest FFT peak gets
            // wrong.
            for (int harmonic = 1; harmonic <= 6; ++harmonic)
                tone += (harmonic == 2 ? 1.1 : 1.0 / harmonic)
                            * std::sin (juce::MathConstants<double>::twoPi * fundamental * harmonic * t);

            const double tremolo = 0.72 + 0.28 * std::sin (juce::MathConstants<double>::twoPi * 0.4 * t);

            // The hiss is loud enough to sit inside the display's 60 dB window
            // on purpose. A demo with an inaudible noise floor renders a
            // spectrum that is six spikes over an empty box, and an empty box
            // is exactly the region of the layout that most needs looking at.
            const auto left  = (float) (0.10 * tremolo * tone) + 0.05f * noise (generator);
            const auto right = (float) (0.10 * tremolo * tone * 0.85) + 0.05f * noise (generator);

            buffer.setSample (0, n, left);
            buffer.setSample (1, n, right);
        }
    }
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const juce::File outputDirectory = argc > 1
        ? juce::File (juce::String (argv[1]))
        : juce::File::getCurrentWorkingDirectory();

    bool         demo = false;
    bool         lifecycle = false;
    bool         stateRoundTrip = false;
    bool         profile = false;
    bool         resolutionCheck = false;
    bool         hitMap = false;
    float        scale = 1.0f;

    for (int argument = 2; argument < argc; ++argument)
    {
        const juce::String option (argv[argument]);

        if (option == "--demo")
            demo = true;
        else if (option == "--lifecycle")
            lifecycle = true;
        else if (option == "--state")
            stateRoundTrip = true;
        else if (option == "--profile")
            profile = true;
        else if (option == "--resolution")
            resolutionCheck = true;
        else if (option == "--hitmap")
            hitMap = true;
        else if (option == "--scale" && argument + 1 < argc)
            scale = juce::jlimit (0.75f, 2.0f, (float) juce::String (argv[++argument]).getDoubleValue());
    }

    if (! outputDirectory.createDirectory())
    {
        std::printf ("FAIL: could not create %s\n", outputDirectory.getFullPathName().toRawUTF8());
        return 1;
    }

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;

    FXAnalyzerProcessor processor;

    processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    // ------------------------------------------------------------------
    // --hitmap: does every pixel of the tab strip belong to the tab drawn
    // on it?
    //
    // The report was that a click on the strip sometimes does nothing and has
    // to be repeated a little further along. The strip's own arithmetic is
    // sound - the tabs tile without a gap - so the interesting question is not
    // where TabBar thinks the tabs are, it is where a click actually goes.
    // getComponentAt answers that one: it walks the children topmost first and
    // honours setAlwaysOnTop and setInterceptsMouseClicks exactly as the mouse
    // does, so anything lying over the strip shows up here and nowhere else.
    //
    // Written out as a picture as well as a count, because an overlay added
    // later is a shape, and a shape is something a person recognises at a
    // glance where a number is just a number.
    // ------------------------------------------------------------------
    if (hitMap)
    {
        std::unique_ptr<juce::AudioProcessorEditor> base (processor.createEditorAndMakeActive());
        auto* editor = dynamic_cast<FXAnalyzerEditor*> (base.get());

        if (editor == nullptr)
        {
            std::printf ("FAIL: no editor for the hit map\n");
            return 1;
        }

        // Visible, or getComponentAt stops at the first line of its own body
        // and reports that nothing is anywhere. An editor built by the
        // processor has never been added to anything, so it is invisible, and
        // hit testing an invisible component is correctly a no-op - the first
        // run of this sweep found only that.
        editor->setVisible (true);

        // Every size the panel can be, not only the one it opens at. The strip
        // is laid out from the window height and the resizer corner is a fixed
        // 18 points, so how much of the strip it covers changes with the size -
        // and the default is not the worst case.
        const std::pair<int, int> sizes[]
        {
            { Layout::defaultWidth * 3 / 4, Layout::defaultHeight * 3 / 4 },
            { Layout::defaultWidth,         Layout::defaultHeight },
            { Layout::defaultWidth * 3 / 2, Layout::defaultHeight * 3 / 2 },
            { Layout::defaultWidth * 2,     Layout::defaultHeight * 2 }
        };

        int problems = 0;

        for (const auto& [width, height] : sizes)
        {
            editor->setSize (width, height);

            const auto& strip  = editor->getTabStrip();
            const auto  bounds = strip.getBounds();

            juce::Image map (juce::Image::ARGB, bounds.getWidth(), bounds.getHeight(), true);

            // One hue per tab, and two that mean trouble: red for a pixel some
            // other component would take, black for one that belongs to no tab.
            const juce::Colour tabColours[]
            {
                juce::Colour (0xff4a90d9), juce::Colour (0xff5cb85c), juce::Colour (0xfff0ad4e),
                juce::Colour (0xff9b59b6), juce::Colour (0xff17a2b8), juce::Colour (0xffd88c3f)
            };

            // The resize grip is allowed to take pixels here and nothing else
            // is. It claims a triangle in the very corner of the editor, which
            // lands on this strip because the strip is shorter than the grip;
            // that is a visible, grabbable control doing its job, not a hole.
            // Anything *else* over the strip is the bug this map exists for.
            int byGrip = 0, byOther = 0, unowned = 0;
            juce::String thief;

            // Which tab owns each column, taken along the strip's middle row,
            // to check the six of them appear once each and in order. A tab
            // that ends up in two separate runs means the layout has folded
            // over itself somewhere, which no per-pixel count would notice.
            std::vector<int> byColumn ((size_t) bounds.getWidth(), -2);

            for (int y = 0; y < bounds.getHeight(); ++y)
            {
                for (int x = 0; x < bounds.getWidth(); ++x)
                {
                    const juce::Point<int> inEditor { bounds.getX() + x, bounds.getY() + y };
                    const auto* target = editor->getComponentAt (inEditor);

                    if (target != &strip)
                    {
                        const auto isGrip = dynamic_cast<const juce::ResizableCornerComponent*> (target) != nullptr;

                        if (isGrip)
                        {
                            ++byGrip;
                            map.setPixelAt (x, y, juce::Colour (0xff707070));
                        }
                        else
                        {
                            ++byOther;
                            map.setPixelAt (x, y, juce::Colour (0xffe03030));

                            if (thief.isEmpty())
                                thief = target != nullptr
                                            ? (target->getName().isNotEmpty() ? target->getName()
                                                                              : juce::String (typeid (*target).name()))
                                            : juce::String ("nothing");
                        }

                        continue;
                    }

                    const auto index = strip.indexAt ({ x, y });

                    if (index < 0)
                    {
                        ++unowned;
                        map.setPixelAt (x, y, juce::Colour (0xff000000));
                    }
                    else
                    {
                        map.setPixelAt (x, y, tabColours[(size_t) juce::jlimit (0, 5, index)]);
                    }

                    // Only rows the strip itself owns feed the run count -
                    // otherwise the grip's triangle reads as a seventh tab.
                    if (y == bounds.getHeight() / 2)
                        byColumn[(size_t) x] = index;
                }
            }

            int runs = 0;
            int previous = -2;

            for (auto owner : byColumn)
            {
                if (owner == -2)      // the grip, not a tab: skip rather than split a run
                    continue;

                if (owner != previous)
                    ++runs;

                previous = owner;
            }

            const auto file = outputDirectory.getChildFile ("hitmap-" + juce::String (width) + "x"
                                                                + juce::String (height) + ".png");
            file.deleteFile();

            juce::PNGImageFormat format;
            std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

            if (stream != nullptr)
                format.writeImageToStream (map, *stream);

            const auto total = bounds.getWidth() * bounds.getHeight();
            const auto ok = byOther == 0 && unowned == 0 && runs == FXParams::numPages
                                && byGrip * 100 < total * 3;

            problems += ok ? 0 : 1;

            char detail[250];
            std::snprintf (detail, sizeof (detail),
                           "%4d x %-4d  strip %3d px  %d runs of %d tabs  %d px own no tab  grip takes %d px (%.1f%%)  stolen by %s: %d",
                           width, height, bounds.getHeight(), runs, FXParams::numPages, unowned, byGrip,
                           100.0 * byGrip / (double) juce::jmax (1, total),
                           thief.isEmpty() ? "nothing else" : thief.toRawUTF8(), byOther);

            std::printf ("  %s  %s\n", ok ? "PASS" : "FAIL", detail);
        }

        // Where the label is, against where the click counts.
        //
        // The sweep above proves the strip has no holes; it says nothing about
        // whether the word you aim at belongs to the tab it names. A label
        // drawn a few pixels out of its own area would leave a clean map and a
        // panel where clicking the left of a word selects its neighbour.
        for (const auto& [width, height] : sizes)
        {
            editor->setSize (width, height);

            const auto& strip  = editor->getTabStrip();
            const auto  bounds = strip.getBounds();
            const auto  middle = bounds.getHeight() / 2;
            const auto  font   = editor->getPanelTheme().tabFont();

            for (int tab = 0; tab < FXParams::numPages; ++tab)
            {
                int firstX = -1, lastX = -1;

                for (int x = 0; x < bounds.getWidth(); ++x)
                    if (strip.indexAt ({ x, middle }) == tab)
                    {
                        if (firstX < 0)
                            firstX = x;

                        lastX = x;
                    }

                const auto textWidth = juce::GlyphArrangement::getStringWidth (font, FXParams::pageNames[tab]);
                const auto centre    = 0.5f * (float) (firstX + lastX);
                const auto textLeft  = centre - textWidth * 0.5f;
                const auto textRight = centre + textWidth * 0.5f;

                const auto leftMargin  = textLeft  - (float) firstX;
                const auto rightMargin = (float) lastX - textRight;

                // Six pixels either side: enough that a pointer aimed at the
                // first letter is not resting on the boundary. Below that the
                // label and the area it belongs to are effectively the same
                // shape, and aiming at a word becomes aiming at an edge.
                //
                // And the distance from the bottom of the window, which is the
                // measurement that came out of Logic: clicks that land in the
                // last few points of a plugin window are not delivered at all.
                // Twenty is the floor because the labels now sit about
                // twenty-seven above it and the reserve should be visible in
                // the number when someone changes the strip's height.
                // The bottom of the *word*, which is what a pointer is aimed at,
                // not the bottom of the row it is centred in.
                const auto labelBottom = (float) bounds.getY() + 0.5f * (float) strip.getContentBottom()
                                             + 0.5f * font.getHeight();

                const auto clearance = (float) editor->getHeight() - labelBottom;

                const auto ok = leftMargin >= 6.0f && rightMargin >= 6.0f && clearance >= 20.0f;

                problems += ok ? 0 : 1;

                char detail[220];
                std::snprintf (detail, sizeof (detail),
                               "%4d px  %-9s label %6.1f..%-6.1f  area %4d..%-4d  side margin %5.1f  clear of window bottom %5.1f",
                               width, FXParams::pageNames[tab].toRawUTF8(), textLeft, textRight,
                               firstX, lastX, juce::jmin (leftMargin, rightMargin), clearance);

                std::printf ("  %s  %s\n", ok ? "PASS" : "FAIL", detail);
            }
        }

        // Can you see which tab is selected, and read every label?
        //
        // With user themes this was a loop over every theme, measuring WCAG
        // luminance contrast, because one theme had once put the selected and
        // unselected labels at 1.00. The design is fixed now and the selection
        // is a fill - an orange button among grey ones - and orange and that
        // grey are close in luminance (about 1.2:1) while being nothing alike
        // in colour. A luminance ratio would call that invisible. So the fills
        // are compared by colour distance, CIE76 delta E in Lab, and the text on
        // them by WCAG contrast, which is the right measure for type.
        {
            const Theme t;

            const auto selectedFill = colourDistance (t.accent, t.button);
            const auto hoverFill    = colourDistance (t.buttonHover, t.button);
            const auto hoverApart   = colourDistance (t.accent, t.buttonHover);
            const auto litText      = Theme::contrastRatio (t.onAccent, t.accent);
            const auto restText     = Theme::contrastRatio (t.text, t.button);
            const auto hoverText    = Theme::contrastRatio (t.text, t.buttonHover);

            // 40 delta E for the selection: unmistakably a different colour, not
            // a shade. 3 for hover: visible, and nothing like the selection. 4.5
            // for type is WCAG AA for normal text.
            const auto ok = selectedFill >= 40.0f && hoverFill >= 3.0f && hoverApart >= 40.0f
                                && litText >= 4.5f && restText >= 4.5f && hoverText >= 4.5f;

            problems += ok ? 0 : 1;

            char detail[240];
            std::snprintf (detail, sizeof (detail),
                           "fills dE: selected:rest %.1f  hover:rest %.1f  selected:hover %.1f   "
                           "type: on lit %.2f  on rest %.2f  on hover %.2f",
                           selectedFill, hoverFill, hoverApart, litText, restText, hoverText);

            std::printf ("  %s  %s\n", ok ? "PASS" : "FAIL", detail);
        }

        // A map of where a click would land is not a demonstration that a click
        // works. Six real clicks, one per tab, at the middle of each run the
        // sweep found - and then the recorded history, which is the thing that
        // has to be readable when this comes back from Logic.
        editor->setSize (Layout::defaultWidth, Layout::defaultHeight);

        {
            const auto& strip  = editor->getTabStrip();
            const auto  bounds = strip.getBounds();
            const auto  middle = bounds.getHeight() / 2;

            for (int tab = 0; tab < FXParams::numPages; ++tab)
            {
                // The centre of this tab's run, found by sweeping rather than
                // by asking the layout - so the click lands where a person
                // aiming at the label would put it.
                int firstX = -1, lastX = -1;

                for (int x = 0; x < bounds.getWidth(); ++x)
                    if (strip.indexAt ({ x, middle }) == tab)
                    {
                        if (firstX < 0)
                            firstX = x;

                        lastX = x;
                    }

                if (firstX < 0)
                {
                    std::printf ("  FAIL  tab %d has no pixels at all\n", tab);
                    ++problems;
                    continue;
                }

                const juce::Point<float> where { (float) ((firstX + lastX) / 2), (float) middle };

                const juce::MouseEvent click (juce::Desktop::getInstance().getMainMouseSource(),
                                              where, juce::ModifierKeys(),
                                              1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                              const_cast<TabBar*> (&strip), const_cast<TabBar*> (&strip),
                                              juce::Time::getCurrentTime(), where,
                                              juce::Time::getCurrentTime(), 1, false);

                const_cast<TabBar&> (strip).mouseDown (click);

                // And the same event through the editor's own handler, which is
                // the body JUCE's nested mouse listener runs. Calling the strip
                // directly exercises the tab logic but skips delivery, so the
                // recording would otherwise never be tested at all.
                editor->mouseDown (click);

                const auto landed = editor->getVisiblePageIndex();
                const auto ok = landed == tab;

                problems += ok ? 0 : 1;

                std::printf ("  %s  clicking %-9s at x %3d shows page %d\n",
                             ok ? "PASS" : "FAIL",
                             FXParams::pageNames[tab].toRawUTF8(), (int) where.x, landed);
            }
        }

        const auto history = editor->getDiagnostics();

        std::printf ("\n  %s  the panel recorded what it was asked to do (%d entries)\n",
                     history.size() >= FXParams::numPages ? "PASS" : "FAIL", history.size());

        problems += history.size() >= FXParams::numPages ? 0 : 1;

        for (const auto& line : history)
            std::printf ("      %s\n", line.toRawUTF8());

        std::printf ("\n%s\n", problems == 0
                        ? "Every pixel of the tab strip reaches its tab, but for the resize grip's corner."
                        : "The tab strip has pixels a click cannot use - see the hit maps.");

        return problems == 0 ? 0 : 1;
    }

    // ------------------------------------------------------------------
    // --profile: what a repaint rate actually costs.
    //
    // Two costs scale with the rate and only one of them was known. The
    // analysis is arithmetic from a measured per-transform time; the drawing -
    // rebuilding a column per pixel, then painting curve, peak line, grid and
    // axes - was not measured at all, and "raise it to 60" is exactly the kind
    // of change that is cheap to say and expensive to find out about.
    //
    // Measured as CPU time over a wall-clock window with audio flowing, first
    // with no editor (the analysis alone) and then with one open at each rate,
    // so the drawing cost is the difference rather than an estimate.
    // ------------------------------------------------------------------
    if (profile)
    {
        std::atomic<bool> stop { false };

        juce::AudioBuffer<float> audioBuffer (2, blockSize);
        juce::MidiBuffer midi;
        std::mt19937 generator (23);
        std::uniform_real_distribution<float> noise (-0.3f, 0.3f);

        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < blockSize; ++n)
                audioBuffer.setSample (ch, n, noise (generator));

        // Real time, so a second of wall clock is a second of audio and the
        // percentages mean what they say.
        std::thread audio ([&]
        {
            juce::AudioBuffer<float> local (2, blockSize);

            while (! stop.load())
            {
                local.makeCopyOf (audioBuffer);
                processor.processBlock (local, midi);
                std::this_thread::sleep_for (std::chrono::microseconds (
                    (long long) (1.0e6 * blockSize / sampleRate)));
            }
        });

        std::unique_ptr<juce::AudioProcessorEditor> base (processor.createEditorAndMakeActive());
        auto* editor = dynamic_cast<FXAnalyzerEditor*> (base.get());

        if (editor == nullptr)
        {
            std::printf ("FAIL: no editor to profile\n");
            stop.store (true);
            audio.join();
            return 1;
        }

        editor->selectPage ((int) FXParams::Page::spectrum);

        juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);

        const auto timePerCall = [] (int runs, auto&& body)
        {
            for (int i = 0; i < 20; ++i)
                body();

            const auto start = juce::Time::getMillisecondCounterHiRes();

            for (int i = 0; i < runs; ++i)
                body();

            return (juce::Time::getMillisecondCounterHiRes() - start) / runs;
        };

        constexpr int runs = 300;

        const auto refreshMs = timePerCall (runs, [&] { editor->advanceOneFrame(); });

        const auto bothMs = timePerCall (runs, [&]
        {
            editor->advanceOneFrame();
            juce::Graphics g (image);
            editor->paintEntireComponent (g, true);
        });

        // The analysis figure below is NOT the analysis cost. This loop calls
        // advanceOneFrame as fast as it can, far faster than audio arrives, so
        // most calls find no new samples and transform nothing. What it does
        // measure is the per-frame refresh - rebuilding a column per pixel -
        // which is the part that scales with the repaint rate. The analysis
        // scales with it too, but through the hop cap, and its cost is the
        // per-transform figure times the rate: about 1% of a core at 30 Hz.
        std::printf ("\n  one display frame, Spectrum page, %d x %d\n\n",
                     editor->getWidth(), editor->getHeight());
        std::printf ("    page refresh (analysis idle here)  %6.3f ms\n", refreshMs);
        std::printf ("    drawing                           %6.3f ms\n", bothMs - refreshMs);
        std::printf ("    per frame                         %6.3f ms\n\n", bothMs);

        for (int hz : { 30, 60 })
            std::printf ("    at %3d Hz: %5.2f %% of one core drawing, plus about %4.1f %% analysis"
                         "   (%.2f ms of every %.1f ms)\n",
                         hz, bothMs * hz / 10.0, hz / 30.0, bothMs, 1000.0 / hz);

        std::printf ("\n");

        stop.store (true);
        audio.join();
        return 0;
    }

    // ------------------------------------------------------------------
    // --resolution: does the Resolution setting actually take effect?
    //
    // Reported from use: switching to Multi sometimes leaves the spectrum's
    // info line naming one tier. Every combination of Resolution and Smoothing
    // is set through the same path the Settings page uses, and the tier count
    // is compared against what the setting promises. The message loop runs
    // between steps, because "not *immediately*" points at a delay and a test
    // without elapsed time cannot see one.
    // ------------------------------------------------------------------
    if (resolutionCheck)
    {
        int problems = 0;

        const auto check = [&problems] (bool ok, const juce::String& what)
        {
            std::printf ("  %s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
            if (! ok) ++problems;
        };

        std::printf ("\nResolution switching\n\n");

        for (int smoothing = 0; smoothing < FXParams::spectrumSmoothingNames.size(); ++smoothing)
        {
            for (int resolution = 0; resolution < FXParams::spectrumResolutionNames.size(); ++resolution)
            {
                processor.setViewProperty (FXParams::propSpectrumSmoothing, smoothing);
                processor.setViewProperty (FXParams::propSpectrumResolution, resolution);
                processor.applyDisplaySettings();

                juce::MessageManager::getInstance()->runDispatchLoopUntil (40);

                const auto smoothingOff = FXParams::spectrumSmoothingFractions[smoothing] <= 0.0f;
                const auto wantMulti = resolution == (int) FXParams::SpectrumResolution::multi;
                const auto expectMulti = wantMulti && ! smoothingOff;

                const auto tiers = processor.getAnalysis().getNumTiers();

                const auto label = FXParams::spectrumSmoothingNames[smoothing]
                                 + " + " + FXParams::spectrumResolutionNames[resolution]
                                 + ": " + juce::String (tiers) + " tier(s)"
                                 + (expectMulti ? " (expected more than one)" : " (expected one)");

                check (expectMulti ? tiers > 1 : tiers == 1, label);
            }
        }

        std::printf ("\n%s\n", problems == 0 ? "Resolution follows the setting."
                                              : "RESOLUTION DOES NOT FOLLOW THE SETTING - see above.");
        return problems == 0 ? 0 : 1;
    }

    // ------------------------------------------------------------------
    // --state: does a saved session come back?
    //
    // Everything the panel remembers is a property on the same ValueTree the
    // parameters live on, which makes saving look obviously correct and is
    // exactly why it needs checking: the tree round-trips whether or not
    // anything reads it back out afterwards.
    // ------------------------------------------------------------------
    if (stateRoundTrip)
    {
        struct Expected { const char* id; int value; };

        const Expected properties[]
        {
            { FXParams::propPage,             (int) FXParams::Page::stereo },
            { FXParams::propSpectrumMode,     (int) FXParams::SpectrumMode::bars63 },   // the newest mode
            { FXParams::propSpectrumScale,    1 },
            { FXParams::propSpectrumRelease,  3 },   // deliberately not the default
            { FXParams::propSpectrumTiltPivot, 1 },  // deliberately not the default
            { FXParams::propSpectrumSmoothing, 3 },   // deliberately not the default
            { FXParams::propSpectrumTop,      0 },   // deliberately not the default
            { FXParams::propSpectrumRange,    5 },
            { FXParams::propSpectrumAttack,   2 },
            { FXParams::propSpectrumResolution, 0 },   // deliberately not the default
            { FXParams::propSpectrumBassZoom, 1 },   // deliberately not the default
            { FXParams::propFftOrder,         13 },
            { FXParams::propScopeTimeMs,      5 }
        };

        for (const auto& property : properties)
            processor.setViewProperty (property.id, property.value);

        // The tilt is a value now, stored beside the legacy index it replaced.
        // 2.5 is on neither the old list nor the default, so a restore that
        // fell back to the index would read 3 and one that lost it would read
        // 4.5 - both visible below.
        processor.setSpectrumTiltDb (2.5f);

        processor.setFloatParameter (FXParams::inputGainDb, 12.0f);
        processor.setChoiceParameter (FXParams::channel, (int) FXParams::Channel::side);

        juce::MemoryBlock saved;
        processor.getStateInformation (saved);

        FXAnalyzerProcessor restored;
        restored.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        restored.prepareToPlay (sampleRate, blockSize);
        restored.setStateInformation (saved.getData(), (int) saved.getSize());

        int problems = 0;

        const auto check = [&problems] (bool ok, const juce::String& what, const juce::String& detail)
        {
            std::printf ("  %s  %-34s %s\n", ok ? "PASS" : "FAIL",
                         what.toRawUTF8(), detail.toRawUTF8());
            if (! ok) ++problems;
        };

        std::printf ("\nState round trip\n\n");

        for (const auto& property : properties)
        {
            const auto got = (int) restored.getViewProperty (property.id, -999);
            check (got == property.value, property.id,
                   "expected " + juce::String (property.value) + ", got " + juce::String (got));
        }

        check (std::abs (restored.getSpectrumTiltDb() - 2.5f) < 0.001f, "spectrumTiltDb",
               "expected 2.5, got " + juce::String (restored.getSpectrumTiltDb(), 2));

        check ((int) restored.getViewProperty (FXParams::propSpectrumSlope, -1) == 1,
               "legacy slope index kept nearest",
               "expected 1 (3 dB/oct), got " + restored.getViewProperty (FXParams::propSpectrumSlope, -1).toString());

        // And a session from before the tilt was a value: only the index is
        // there. It has to open on the slope it was saved with.
        {
            FXAnalyzerProcessor legacy;
            legacy.setPlayConfigDetails (2, 2, sampleRate, blockSize);
            legacy.prepareToPlay (sampleRate, blockSize);

            juce::ValueTree old (FXParams::stateTreeType);
            old.setProperty (FXParams::propStateVersion, FXParams::stateVersion, nullptr);
            old.setProperty (FXParams::propSpectrumSlope, 3, nullptr);   // 6 dB/oct

            juce::MemoryBlock oldState;
            juce::AudioProcessor::copyXmlToBinary (*old.createXml(), oldState);
            legacy.setStateInformation (oldState.getData(), (int) oldState.getSize());

            check (std::abs (legacy.getSpectrumTiltDb() - 6.0f) < 0.001f, "a pre-tilt session keeps its slope",
                   "expected 6.0, got " + juce::String (legacy.getSpectrumTiltDb(), 2));
        }

        check (std::abs (restored.getFloatParameter (FXParams::inputGainDb) - 12.0f) < 0.01f,
               "inputGainDb", juce::String (restored.getFloatParameter (FXParams::inputGainDb), 2));
        check (restored.getChoiceParameter (FXParams::channel) == (int) FXParams::Channel::side,
               "channel", juce::String (restored.getChoiceParameter (FXParams::channel)));

        // And the half that matters to a user: does the panel *show* it? The
        // tree can round-trip perfectly while the open editor keeps displaying
        // whatever it cached when it was created.
        std::unique_ptr<juce::AudioProcessorEditor> base (restored.createEditorAndMakeActive());

        if (auto* editor = dynamic_cast<FXAnalyzerEditor*> (base.get()))
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

            check (editor->getVisiblePageIndex() == (int) FXParams::Page::stereo,
                   "editor opens on the saved page",
                   "page " + juce::String (editor->getVisiblePageIndex()));

            // Restore into an already-open editor, which is what a host does
            // when it loads a session with the window up.
            restored.setViewProperty (FXParams::propPage, (int) FXParams::Page::pitch);
            juce::MemoryBlock second;
            restored.getStateInformation (second);
            restored.setStateInformation (second.getData(), (int) second.getSize());
            juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

            check (editor->getVisiblePageIndex() == (int) FXParams::Page::pitch,
                   "an open editor follows a reload",
                   "page " + juce::String (editor->getVisiblePageIndex()));
        }

        // ---- the View stepper's order against the stored mode -------------
        //
        // Bars 63 is shown second but stored last, so that index 2 - Sonogram
        // in every session saved before it existed - still means Sonogram.
        {
            check (FXParams::spectrumModeToStepIndex (2) == 3
                       && FXParams::spectrumModeNames[FXParams::spectrumModeToStepIndex (2)] == "Sonogram",
                   "stored mode 2 is still Sonogram", FXParams::spectrumModeNames[FXParams::spectrumModeToStepIndex (2)]);

            bool roundTrips = true;

            for (int step = 0; step < FXParams::spectrumModeNames.size(); ++step)
                roundTrips = roundTrips && FXParams::spectrumModeToStepIndex (FXParams::spectrumModeFromStepIndex (step)) == step;

            check (roundTrips && FXParams::spectrumModeNames[1] == "Bars 31" && FXParams::spectrumModeNames[2] == "Bars 63",
                   "View steps 2D, Bars 31, Bars 63, Sonogram", FXParams::spectrumModeNames.joinIntoString (", "));
        }

        // ---- a session saved while there were themes ---------------------
        //
        // Until 0.2 the state carried a themeName property and a <Theme> child
        // with the colours. Nothing reads them now; what has to hold is that a
        // session carrying them still restores everything else.
        std::printf ("\nOld theme data\n\n");

        {
            auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
            check (xml != nullptr, "the saved state parses", {});

            if (xml != nullptr)
            {
                xml->setAttribute ("themeName", "Graphite");
                auto* themeNode = xml->createNewChildElement ("Theme");
                themeNode->setAttribute ("name", "Graphite");
                themeNode->setAttribute ("background", "FF2B2D31");
                themeNode->setAttribute ("curve", "FFE8A33D");

                juce::MemoryBlock old;
                juce::AudioProcessor::copyXmlToBinary (*xml, old);

                FXAnalyzerProcessor withTheme;
                withTheme.setPlayConfigDetails (2, 2, sampleRate, blockSize);
                withTheme.prepareToPlay (sampleRate, blockSize);
                withTheme.setStateInformation (old.getData(), (int) old.getSize());

                check (std::abs (withTheme.getFloatParameter (FXParams::inputGainDb) - 12.0f) < 0.01f
                           && withTheme.getChoiceParameter (FXParams::channel) == (int) FXParams::Channel::side
                           && std::abs (withTheme.getSpectrumTiltDb() - 2.5f) < 0.001f,
                       "a session with a theme in it restores the rest",
                       "gain " + juce::String (withTheme.getFloatParameter (FXParams::inputGainDb), 1)
                           + ", tilt " + juce::String (withTheme.getSpectrumTiltDb(), 1));
            }
        }

        // ---- the numeric stepper's value semantics ----------------------
        //
        // Input Gain looks like the other steppers and must not behave like
        // them: it is an automatable parameter, and quantising it to a list of
        // choices would snap every automation curve to those steps. The step is
        // how far a *click* moves, never a grid the value is forced onto - so
        // the assertion that matters is that an off-step value survives.
        std::printf ("\nNumeric stepper\n\n");

        {
            StepperControl stepper;
            stepper.setTheme (Theme{});
            stepper.setNumericRange (FXParams::inputGainMinDb, FXParams::inputGainMaxDb,
                                     1.0f, 0.0f,
                                     [] (float db) { return juce::String (db, 1) + " dB"; });

            stepper.setNumericValue (-7.3f, juce::dontSendNotification);
            check (std::abs (stepper.getNumericValue() + 7.3f) < 0.001f,
                   "an off-step value is not snapped",
                   juce::String (stepper.getNumericValue(), 3));

            stepper.setNumericValue (999.0f, juce::dontSendNotification);
            check (std::abs (stepper.getNumericValue() - FXParams::inputGainMaxDb) < 0.001f,
                   "the upper limit holds", juce::String (stepper.getNumericValue(), 2));

            stepper.setNumericValue (-999.0f, juce::dontSendNotification);
            check (std::abs (stepper.getNumericValue() - FXParams::inputGainMinDb) < 0.001f,
                   "the lower limit holds", juce::String (stepper.getNumericValue(), 2));

            stepper.setNumericValue (0.0f, juce::dontSendNotification);
            check (stepper.getSelectedText() == "0.0 dB", "the formatter is used",
                   stepper.getSelectedText());

            // Two quick clicks on the up arrow, exactly as JUCE delivers them:
            // mouseDown, then mouseDown *and* mouseDoubleClick. Reported from
            // use as "Input sometimes jumps back to 0" - and it is not
            // sometimes, it is whenever you click faster than the double-click
            // interval, which is what raising a gain by several decibels looks
            // like. Reset-on-double-click came over from the ring knob, where
            // it was harmless because nobody clicks a knob repeatedly.
            stepper.setSize (100, 90);
            stepper.setNumericValue (0.0f, juce::dontSendNotification);

            const auto clickAt = [&] (juce::Point<float> where, int clickCount)
            {
                return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                         where, juce::ModifierKeys(),
                                         1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                         &stepper, &stepper, juce::Time::getCurrentTime(),
                                         where, juce::Time::getCurrentTime(), clickCount, false);
            };

            const juce::Point<float> onTheUpArrow { 50.0f, 8.0f };

            stepper.mouseDown (clickAt (onTheUpArrow, 1));
            stepper.mouseDown (clickAt (onTheUpArrow, 2));
            stepper.mouseDoubleClick (clickAt (onTheUpArrow, 2));

            check (std::abs (stepper.getNumericValue() - 2.0f) < 0.001f,
                   "two quick clicks step twice and do not reset",
                   juce::String (stepper.getNumericValue(), 2) + " dB");

            // And the reset still exists, on the gesture that cannot happen by
            // accident.
            auto alt = juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                         onTheUpArrow,
                                         juce::ModifierKeys (juce::ModifierKeys::altModifier),
                                         1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                         &stepper, &stepper, juce::Time::getCurrentTime(),
                                         onTheUpArrow, juce::Time::getCurrentTime(), 1, false);

            stepper.mouseDown (alt);

            check (std::abs (stepper.getNumericValue()) < 0.001f,
                   "alt-click still resets",
                   juce::String (stepper.getNumericValue(), 2) + " dB");
        }

        std::printf ("\n%s\n", problems == 0 ? "State survives the round trip."
                                              : "STATE ROUND TRIP FAILED - see above.");
        return problems == 0 ? 0 : 1;
    }

    // ------------------------------------------------------------------
    // --lifecycle: what a host does when a plugin is dragged to another slot
    // while the transport is running.
    //
    // The editor is created and destroyed on the message thread, over and over,
    // while one background thread restores state - which is what the host does
    // on a move, and it does it on its own thread - and another pushes audio.
    // Every notification that crosses between those three is exercised.
    //
    // This is the harness the theme notification failed: it used to be a
    // std::function the editor assigned in its constructor and nulled in its
    // destructor, and a host restoring state could be calling it at the moment
    // it was being nulled.
    // ------------------------------------------------------------------
    if (lifecycle)
    {
        juce::MemoryBlock state;
        processor.getStateInformation (state);

        std::atomic<bool> stop { false };
        std::atomic<int>  restores { 0 }, blocks { 0 }, editors { 0 };

        juce::AudioBuffer<float> audioBuffer (2, blockSize);
        juce::MidiBuffer midi;
        std::mt19937 generator (9);
        std::uniform_real_distribution<float> noise (-0.2f, 0.2f);

        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < blockSize; ++n)
                audioBuffer.setSample (ch, n, noise (generator));

        std::thread restorer ([&]
        {
            while (! stop.load())
            {
                processor.setStateInformation (state.getData(), (int) state.getSize());
                ++restores;
                std::this_thread::sleep_for (std::chrono::microseconds (200));
            }
        });

        std::thread audio ([&]
        {
            juce::AudioBuffer<float> local (2, blockSize);

            while (! stop.load())
            {
                local.makeCopyOf (audioBuffer);
                processor.processBlock (local, midi);
                ++blocks;
                std::this_thread::yield();
            }
        });

        for (int i = 0; i < 120; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
            juce::MessageManager::getInstance()->runDispatchLoopUntil (8);
            editor.reset();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
            ++editors;
        }

        stop.store (true);
        restorer.join();
        audio.join();

        std::printf ("lifecycle: %d editors, %d state restores, %d audio blocks - survived\n",
                     editors.load(), restores.load(), blocks.load());
        return 0;
    }

    // The null test again, through the real processBlock this time.
    //
    // Before the demo audio, and that ordering is not cosmetic. This pushes a
    // block of full-scale noise through the plugin, which lands in the capture
    // ring like any other audio. Run after the demo, it is the *newest* thing
    // in the ring - so the scope, whose window is longer than one block, drew
    // ten milliseconds of waveform followed by ten of test noise, and the
    // rendered shot showed a fault that existed only in the tool that rendered
    // it. Eight seconds of demo audio afterwards flushes it back out.
    //
    // AnalyzerCheck asserts it against AnalysisHub, which is the whole of the
    // DSP but not the whole of the path: the processor could still write to the
    // buffer after handing it over, and since the addition of the standalone's
    // deliberate buffer.clear() there is now a branch in that function which
    // does exactly that. One wrong condition there and every host gets silence
    // out of an analyzer - a failure loud enough to notice, and one that no
    // test in AnalyzerCheck can see.
    {
        juce::AudioBuffer<float> buffer (2, blockSize), untouched (2, blockSize);
        juce::MidiBuffer midi;
        std::mt19937 generator (7);
        std::uniform_real_distribution<float> noise (-0.5f, 0.5f);

        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < blockSize; ++n)
                buffer.setSample (ch, n, noise (generator));

        untouched.makeCopyOf (buffer);
        processor.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < blockSize; ++n)
                if (std::bit_cast<std::uint32_t> (buffer.getSample (ch, n))
                        != std::bit_cast<std::uint32_t> (untouched.getSample (ch, n)))
                {
                    std::printf ("FAIL: processBlock altered the buffer at channel %d, sample %d\n", ch, n);
                    return 1;
                }
    }


    if (demo)
    {
        // Eight seconds, so the integrated loudness has passed its five second
        // settling test and the page shows a number rather than a dash. This is
        // the whole reason the demo runs for seconds rather than for a block:
        // the states worth photographing are the ones that take time to reach.
        std::mt19937 generator (20260830);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        const int blocks = (int) (sampleRate * 8.0) / blockSize;

        for (int block = 0; block < blocks; ++block)
        {
            fillDemoBlock (buffer, (int64_t) block * blockSize, sampleRate, generator);
            processor.processBlock (buffer, midi);
        }
    }

    std::unique_ptr<juce::AudioProcessorEditor> base (processor.createEditorAndMakeActive());
    auto* editor = dynamic_cast<FXAnalyzerEditor*> (base.get());

    if (editor == nullptr)
    {
        std::printf ("FAIL: the processor produced no FX Analyzer editor\n");
        return 1;
    }

    editor->setSize (juce::roundToInt ((float) Layout::defaultWidth * scale),
                     juce::roundToInt ((float) Layout::defaultHeight * scale));

    int written = 0;

    // The linear spectrum is rendered as a seventh shot rather than left to
    // whoever thinks to switch the control. It earns its place: the two scales
    // are not two settings of one layout, they are two different tick sets, and
    // the first version of this project shipped a linear axis whose labels sat
    // on top of each other in the leftmost tenth of the plot. Nothing caught it
    // because every rendered shot used the default, and the default is Log.
    // Two extra spectrum shots beyond the six pages: the linear axis, and the
    // unsmoothed curve. Both are states the default shot cannot show, and both
    // have had a bug that only a picture would have caught - the linear axis
    // stacked its labels on top of each other, and the octave smoothing moved a
    // tone by a semitone.
    // Two more for the bass zoom, and they only mean anything as a pair: the
    // one at 4096 shows a magnified curve with no new detail in it, the one at
    // 65536 shows the detail the full axis was throwing away. AnalyzerCheck
    // measures that difference; these are what let it be looked at.
    // And one for Bars 63, because a bar count that doubles is a layout
    // question - do the gaps swallow the bars at 900 points? - only a picture
    // answers.
    const auto totalShots = FXParams::numPages + 8;

    for (int shot = 0; shot < totalShots; ++shot)
    {
        const auto linearPass = shot == FXParams::numPages;
        const auto rawPass    = shot == FXParams::numPages + 1;
        const auto rangePass  = shot == FXParams::numPages + 2;
        const auto singlePass  = shot == FXParams::numPages + 3;
        const auto scopePass   = shot == FXParams::numPages + 4;
        const auto bassPass    = shot == FXParams::numPages + 5;
        const auto bassLongPass = shot == FXParams::numPages + 6;
        const auto bars63Pass   = shot == FXParams::numPages + 7;
        const auto page = scopePass ? (int) FXParams::Page::scope
                        : (linearPass || rawPass || rangePass || singlePass
                           || bassPass || bassLongPass || bars63Pass)
                              ? (int) FXParams::Page::spectrum : shot;

        // The bass zoom, twice. The long window is the point of the pair: at
        // 4096 there is already more than one pixel per bin across the whole
        // band, so the zoom can only magnify, while at 65536 the full axis puts
        // four bins under each pixel at the top of the band and keeps only the
        // loudest of them.
        processor.setViewProperty (FXParams::propSpectrumBassZoom,
                                   (bassPass || bassLongPass) ? 1 : 0);

        processor.setViewProperty (FXParams::propSpectrumMode,
                                   (int) (bars63Pass ? FXParams::SpectrumMode::bars63
                                                     : FXParams::SpectrumMode::curve));

        processor.setViewProperty (FXParams::propFftOrder,
                                   bassLongPass ? 16
                                                : FXParams::fftOrderValues[FXParams::defaultFftOrderIndex]);

        // The same signal at one resolution, for comparison against the default
        // shot. Three tiers against one is the whole feature, and a number in a
        // test does not show what it does to a curve.
        processor.setViewProperty (FXParams::propSpectrumResolution,
                                   singlePass ? 0 : FXParams::defaultSpectrumResolutionIndex);

        // A narrow window high up the scale: the axis and its grid step are
        // computed rather than fixed now, and how many lines a 30 dB range ends
        // up with is a question only a picture answers.
        processor.setViewProperty (FXParams::propSpectrumTop,
                                   rangePass ? 2 : FXParams::defaultSpectrumTopIndex);
        processor.setViewProperty (FXParams::propSpectrumRange,
                                   rangePass ? 0 : FXParams::defaultSpectrumRangeIndex);

        processor.setViewProperty (FXParams::propSpectrumScale,
                                   linearPass ? (int) FXParams::FrequencyScale::linear
                                              : (int) FXParams::FrequencyScale::logarithmic);

        processor.setViewProperty (FXParams::propSpectrumSmoothing,
                                   rawPass ? 0 : FXParams::defaultSpectrumSmoothingIndex);

        // The longest time base draws differently from the default: past two
        // pixels' worth of samples per column ScopePage switches to a min/max
        // bar per column, and that path has no shot of its own otherwise.
        // The property holds the time in milliseconds, not an index - writing an
        // index here produced a "2 s" shot that quietly rendered the default,
        // because ScopePage looks the value up and falls back when it does not
        // match.
        processor.setViewProperty (FXParams::propScopeTimeMs,
                                   FXParams::scopeTimeValues[scopePass ? FXParams::lastScopeTimeIndex
                                                                       : FXParams::defaultScopeTimeIndex]);
        processor.applyDisplaySettings();

        editor->selectPage (page);

        // Let the editor settle before painting - and keep feeding it while it
        // does, which is the part that was missing.
        //
        // Pushing all the demo audio before the editor exists was enough while
        // the display smoother ran on the display clock. It stopped being
        // enough twice over: the analysis now advances only as far as new audio
        // arrives, and the default reactivity is Very Slow, whose two second
        // rise needs several seconds of *analysis* time to climb out of the
        // floor. With the audio all delivered up front there was none left, so
        // the spectrum shot came out empty while every assertion stayed green -
        // the third time in this project that only a rendered image noticed.
        //
        // So audio keeps flowing during the settle. Ten seconds of it, in
        // chunks, with the message loop pumped between them.
        if (demo)
        {
            std::mt19937 settleGenerator (20260830);
            juce::AudioBuffer<float> settleBuffer (2, blockSize);
            juce::MidiBuffer settleMidi;

            const int chunkBlocks = (int) (sampleRate * 0.4) / blockSize;
            const int chunks = 25;

            for (int chunk = 0; chunk < chunks; ++chunk)
            {
                for (int block = 0; block < chunkBlocks; ++block)
                    fillDemoBlock (settleBuffer,
                                   (int64_t) (chunk * chunkBlocks + block) * blockSize,
                                   sampleRate, settleGenerator),
                    processor.processBlock (settleBuffer, settleMidi);

                juce::MessageManager::getInstance()->runDispatchLoopUntil (12);
            }
        }
        else
        {
            const auto deadline = juce::Time::getMillisecondCounter() + 400;

            while (juce::Time::getMillisecondCounter() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        }

        juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);

        {
            juce::Graphics g (image);
            editor->paintEntireComponent (g, true);
        }

        const auto name = FXParams::pageNames[page].toLowerCase()
                              + (linearPass ? "-linear" : "")
                              + (rawPass ? "-unsmoothed" : "")
                              + (rangePass ? "-narrow-range" : "")
                              + (singlePass ? "-single-resolution" : "")
                              + (bassPass ? "-bass-zoom" : "")
                              + (bassLongPass ? "-bass-zoom-65536" : "")
                              + (bars63Pass ? "-bars63" : "")
                              + (scopePass ? "-longest" : "");
        const auto scaleSuffix = std::abs (scale - 1.0f) < 0.001f ? juce::String()
                                                                  : "-x" + juce::String (scale, 2);
        const auto file = outputDirectory.getChildFile ("page-" + name + scaleSuffix + ".png");

        juce::PNGImageFormat format;

        // Deleted first, because createOutputStream opens an existing file at
        // its end. Without this, every render after the first is appended to the
        // last one - and since a decoder stops at the first image in the file,
        // the picture you look at is the one from the build before the change
        // you are checking. This target exists to make the panel checkable; a
        // stale image that still looks plausible defeats the point of it.
        file.deleteFile();

        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

        if (stream == nullptr || ! format.writeImageToStream (image, *stream))
        {
            std::printf ("FAIL: could not write %s\n", file.getFullPathName().toRawUTF8());
            return 1;
        }

        ++written;
    }

    const auto renderedSize = editor->getBounds().getBottomRight();

    processor.editorBeingDeleted (base.get());
    base.reset();

    std::printf ("%d shots rendered at %d x %d into %s\n",
                 written, renderedSize.x, renderedSize.y,
                 outputDirectory.getFullPathName().toRawUTF8());

    return 0;
}
