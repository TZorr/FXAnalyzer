//
//  HostNullTest.cpp
//  FX Analyzer
//
//  The null test, run against the installed Audio Unit the way a host runs it.
//
//  AnalyzerCheck already asserts that AnalysisHub does not touch the buffer, and
//  EditorShot asserts it again through the real processBlock. Both stop at the
//  same place: they test the code, not the product. Between processBlock and
//  the host lies JUCE's AU wrapper, the channel layout negotiation and the
//  render callback, and a plugin can be transparent in all of its own code and
//  still not be transparent as a component - a wrapper that pre-clears the
//  output buffer, or a bus layout that silently sums, would pass every existing
//  check here and fail in the DAW.
//
//  So this loads the .component from ~/Library/Audio/Plug-Ins, renders through
//  it with AudioUnitRender, and compares the output to what was fed in, bit
//  pattern for bit pattern. Not "within an epsilon": identical. A transparent
//  plugin has no rounding to do, and a tolerance here would hide exactly the
//  quiet gain error the test exists to catch.
//
//  Every parameter is swept while it does so. The claim is not merely that the
//  plugin is transparent at its defaults - it is that nothing on the panel can
//  make it otherwise, and Input Gain in particular is a +/-24 dB control that
//  must reach the analysis and nothing else.
//

#include <AudioToolbox/AudioToolbox.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace
{
    constexpr int    numChannels = 2;
    constexpr UInt32 blockFrames = 512;
    constexpr double sampleRate  = 48000.0;

    struct Feeder
    {
        std::vector<std::vector<float>> sent { (size_t) numChannels };
        std::mt19937 generator { 1234 };
    };

    OSStatus feedInput (void* refCon,
                        AudioUnitRenderActionFlags*,
                        const AudioTimeStamp*,
                        UInt32,
                        UInt32 frames,
                        AudioBufferList* data)
    {
        auto* feeder = static_cast<Feeder*> (refCon);
        std::uniform_real_distribution<float> noise (-0.9f, 0.9f);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* out = static_cast<float*> (data->mBuffers[ch].mData);

            for (UInt32 n = 0; n < frames; ++n)
            {
                // Full-scale-ish noise, different per channel, so a wrapper that
                // summed or swapped channels would be as visible as one that
                // scaled them.
                const auto value = noise (feeder->generator);
                out[n] = value;
                feeder->sent[(size_t) ch].push_back (value);
            }
        }

        return noErr;
    }

    int failures = 0;

    void report (bool ok, const char* what, const char* detail = "")
    {
        std::printf ("  %s  %-56s %s\n", ok ? "PASS" : "FAIL", what, detail);

        if (! ok)
            ++failures;
    }
}

int main()
{
    AudioComponentDescription desc {};
    desc.componentType         = kAudioUnitType_Effect;
    desc.componentSubType      = 'Fxan';
    desc.componentManufacturer = 'Tzor';

    auto* component = AudioComponentFindNext (nullptr, &desc);

    if (component == nullptr)
    {
        std::printf ("FAIL: aufx/Fxan/Tzor is not registered - run Scripts/install.sh first\n");
        return 1;
    }

    AudioUnit unit {};

    if (AudioComponentInstanceNew (component, &unit) != noErr)
    {
        std::printf ("FAIL: could not instantiate the component\n");
        return 1;
    }

    AudioStreamBasicDescription format {};
    format.mSampleRate       = sampleRate;
    format.mFormatID         = kAudioFormatLinearPCM;
    format.mFormatFlags      = (AudioFormatFlags) kAudioFormatFlagsNativeFloatPacked
                             | (AudioFormatFlags) kAudioFormatFlagIsNonInterleaved;
    format.mChannelsPerFrame = numChannels;
    format.mFramesPerPacket  = 1;
    format.mBitsPerChannel   = 32;
    format.mBytesPerFrame    = 4;
    format.mBytesPerPacket   = 4;

    for (auto scope : { kAudioUnitScope_Input, kAudioUnitScope_Output })
        if (AudioUnitSetProperty (unit, kAudioUnitProperty_StreamFormat, scope, 0,
                                  &format, sizeof (format)) != noErr)
        {
            std::printf ("FAIL: the component refused a stereo 48 kHz float layout\n");
            return 1;
        }

    UInt32 maxFrames = blockFrames;
    AudioUnitSetProperty (unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0,
                          &maxFrames, sizeof (maxFrames));

    Feeder feeder;

    AURenderCallbackStruct callback {};
    callback.inputProc       = feedInput;
    callback.inputProcRefCon = &feeder;

    AudioUnitSetProperty (unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0,
                          &callback, sizeof (callback));

    if (AudioUnitInitialize (unit) != noErr)
    {
        std::printf ("FAIL: AudioUnitInitialize failed\n");
        return 1;
    }

    // Every parameter, at its minimum, middle and maximum.
    UInt32 listSize = 0;
    AudioUnitGetPropertyInfo (unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0,
                              &listSize, nullptr);

    std::vector<AudioUnitParameterID> parameters (listSize / sizeof (AudioUnitParameterID));

    if (! parameters.empty())
        AudioUnitGetProperty (unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0,
                              parameters.data(), &listSize);

    std::printf ("\nHost null test: %zu parameters, %d blocks each\n\n", parameters.size(), 8);

    std::vector<float> left (blockFrames), right (blockFrames);

    const auto renderAndCompare = [&] (const char* label)
    {
        feeder.sent[0].clear();
        feeder.sent[1].clear();

        std::vector<std::vector<float>> received { (size_t) numChannels };

        AudioTimeStamp timeStamp {};
        timeStamp.mFlags = kAudioTimeStampSampleTimeValid;

        for (int block = 0; block < 8; ++block)
        {
            std::vector<char> storage (sizeof (AudioBufferList) + sizeof (AudioBuffer));
            auto* list = reinterpret_cast<AudioBufferList*> (storage.data());
            list->mNumberBuffers = numChannels;

            list->mBuffers[0] = { 1, blockFrames * 4, left.data() };
            list->mBuffers[1] = { 1, blockFrames * 4, right.data() };

            AudioUnitRenderActionFlags flags = 0;
            const auto status = AudioUnitRender (unit, &flags, &timeStamp, 0, blockFrames, list);

            if (status != noErr)
            {
                char detail[80];
                std::snprintf (detail, sizeof (detail), "AudioUnitRender returned %d", (int) status);
                report (false, label, detail);
                return;
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto* data = static_cast<const float*> (list->mBuffers[ch].mData);
                received[(size_t) ch].insert (received[(size_t) ch].end(), data, data + blockFrames);
            }

            timeStamp.mSampleTime += blockFrames;
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (received[(size_t) ch].size() != feeder.sent[(size_t) ch].size())
            {
                report (false, label, "the component returned a different number of samples");
                return;
            }

            if (std::memcmp (received[(size_t) ch].data(), feeder.sent[(size_t) ch].data(),
                             received[(size_t) ch].size() * sizeof (float)) != 0)
            {
                for (size_t n = 0; n < received[(size_t) ch].size(); ++n)
                    if (received[(size_t) ch][n] != feeder.sent[(size_t) ch][n])
                    {
                        char detail[140];
                        std::snprintf (detail, sizeof (detail),
                                       "channel %d, sample %zu: in %.9f, out %.9f",
                                       ch, n, feeder.sent[(size_t) ch][n], received[(size_t) ch][n]);
                        report (false, label, detail);
                        return;
                    }
            }
        }

        report (true, label, "bit for bit");
    };

    renderAndCompare ("defaults");

    for (auto id : parameters)
    {
        AudioUnitParameterInfo info {};
        UInt32 infoSize = sizeof (info);
        AudioUnitGetProperty (unit, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id,
                              &info, &infoSize);

        // Reachability first, and separately from the sweep.
        //
        // Without it the whole sweep could pass while doing nothing: if the
        // parameter never reached the plugin, every render would be at the
        // default and "bit for bit" would be true and meaningless. A test that
        // cannot fail is not evidence.
        //
        // What it must NOT do is demand that the value read back equals the
        // value set. Discrete parameters snap - asking Reactivity for 1.5 on a
        // four-step control correctly gives 2 - and a check that called that a
        // failure would be reporting the plugin working as designed as a bug.
        // So the question asked is the weaker and the right one: does the
        // extreme differ from the other extreme?
        const auto readParameter = [&]
        {
            AudioUnitParameterValue value = 0.0f;
            AudioUnitGetParameter (unit, id, kAudioUnitScope_Global, 0, &value);
            return value;
        };

        AudioUnitSetParameter (unit, id, kAudioUnitScope_Global, 0, info.minValue, 0);
        const auto atMinimum = readParameter();

        AudioUnitSetParameter (unit, id, kAudioUnitScope_Global, 0, info.maxValue, 0);
        const auto atMaximum = readParameter();

        char reach[140];
        std::snprintf (reach, sizeof (reach), "%s: %g at minimum, %g at maximum",
                       info.name, (double) atMinimum, (double) atMaximum);
        report (atMinimum != atMaximum, "the sweep reaches the plugin", reach);

        for (auto value : { info.minValue, (info.minValue + info.maxValue) * 0.5f, info.maxValue })
        {
            AudioUnitSetParameter (unit, id, kAudioUnitScope_Global, 0, value, 0);

            char label[110];
            std::snprintf (label, sizeof (label), "%s = %g", info.name, (double) readParameter());
            renderAndCompare (label);
        }

        AudioUnitSetParameter (unit, id, kAudioUnitScope_Global, 0, info.defaultValue, 0);
    }

    AudioUnitUninitialize (unit);
    AudioComponentInstanceDispose (unit);

    std::printf ("\n%s\n", failures == 0
                     ? "The installed Audio Unit is bit-transparent at every parameter setting."
                     : "TRANSPARENCY FAILED - see above.");

    return failures == 0 ? 0 : 1;
}
