//
//  TruePeakMeter.cpp
//  FX Analyzer
//

#include "TruePeakMeter.h"

#include <cmath>

void TruePeakMeter::prepare (int numChannels, int maximumBlockSize)
{
    preparedChannels  = juce::jmax (1, numChannels);
    preparedBlockSize = juce::jmax (1, maximumBlockSize);

    // Factor 2 means 2^2 = 4x, which is what BS.1770 specifies for material at
    // or below 48 kHz. An equiripple FIR rather than the polyphase IIR: the IIR
    // is cheaper but its phase response smears the very transients whose peak
    // is being measured, and this runs on two stereo streams, so cheap is not
    // a constraint worth trading accuracy for.
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) preparedChannels,
        2,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
        true,    // maximum quality
        false);  // integer latency is irrelevant: only the magnitude is read, never the timing

    oversampler->initProcessing ((size_t) preparedBlockSize);
    oversampler->reset();

    // The oversampler reports its latency in *output* samples, and the impulse
    // response is about twice that either side of the centre tap. Four times
    // the latency is a generous, cheap margin: it is a couple of milliseconds
    // of a signal that is silent at this point in every real session.
    samplesToPrime = 4 * (int) std::ceil (oversampler->getLatencyInSamples());

    reset();
}

void TruePeakMeter::reset() noexcept
{
    maxMagnitude.store (0.0f, std::memory_order_relaxed);
}

void TruePeakMeter::processBlock (const float* const* channelData, int numChannels, int numSamples) noexcept
{
    if (resetRequested.exchange (false, std::memory_order_relaxed))
        reset();

    if (oversampler == nullptr || numSamples <= 0 || numSamples > preparedBlockSize)
        return;

    const int channels = juce::jmin (numChannels, preparedChannels);

    if (channels <= 0)
        return;

    juce::dsp::AudioBlock<const float> inputBlock (channelData,
                                                  (size_t) channels,
                                                  (size_t) numSamples);

    auto upsampled = oversampler->processSamplesUp (inputBlock);

    float peak = 0.0f;

    for (size_t ch = 0; ch < upsampled.getNumChannels(); ++ch)
    {
        const auto* data = upsampled.getChannelPointer (ch);

        for (size_t n = 0; n < upsampled.getNumSamples(); ++n)
            peak = juce::jmax (peak, std::abs (data[n]));
    }

    // Compare-and-store rather than a plain store: the held value is a maximum,
    // and only the audio thread writes it, so a relaxed read-modify-write is
    // both correct and free here.
    if (samplesToPrime > 0)
    {
        // The block still had to go through the filters - that is what priming
        // means - but its peak is the filters settling, not the signal.
        samplesToPrime -= numSamples;
        return;
    }

    if (peak > maxMagnitude.load (std::memory_order_relaxed))
        maxMagnitude.store (peak, std::memory_order_relaxed);
}

void TruePeakMeter::processBuffer (const juce::AudioBuffer<float>& buffer) noexcept
{
    if (oversampler == nullptr)
        return;

    const int total = buffer.getNumSamples();
    const int channels = juce::jmin (buffer.getNumChannels(), preparedChannels);

    auto sweep = [&]
    {
        for (int start = 0; start < total; start += preparedBlockSize)
        {
            const int count = juce::jmin (preparedBlockSize, total - start);

            const float* pointers[2] {};

            for (int ch = 0; ch < channels; ++ch)
                pointers[ch] = buffer.getReadPointer (ch, start);

            processBlock (pointers, channels, count);
        }
    };

    // Twice, keeping the second answer. The first sweep exists only to leave
    // the filters holding the end of the file, so that when the second sweep
    // starts at sample 0 there is no cold-start overshoot to mistake for a
    // peak. See the header: this is worth 0.8 dB on the wrong file.
    sweep();
    reset();
    samplesToPrime = 0;
    sweep();
}

float TruePeakMeter::getMaxTruePeakDb() const noexcept
{
    const float magnitude = maxMagnitude.load (std::memory_order_relaxed);
    return magnitude > 0.0f ? juce::Decibels::gainToDecibels (magnitude, -200.0f) : -200.0f;
}
