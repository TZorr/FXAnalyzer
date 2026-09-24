//
//  RingBuffer.h
//  FX Analyzer
//
//  The one place where the audio thread and the drawing thread meet.
//
//  The audio thread writes every sample it is given. The UI wakes up thirty
//  times a second and reads back whatever the most recent window happens to be.
//  Samples written and never read are dropped, and that is the correct
//  behaviour, not a compromise: a display has no use for the frames it was too
//  late to draw, and a FIFO that refused to drop them would either block the
//  audio thread or grow without bound. juce::AbstractFifo was the obvious
//  reach and is the wrong shape for this - it models a consumer that must see
//  everything.
//
//  Correctness rests on one thing: the buffer is far larger than any window a
//  reader asks for. Capacity is two seconds; the longest read is the pitch
//  detector's 3500 samples and the scope's history search, both well under a
//  tenth of that. A reader can therefore be pre-empted for tens of
//  milliseconds mid-copy and still finish before the writer laps it. If the UI
//  thread ever stalled for two full seconds the read would tear - and a panel
//  that has not painted for two seconds has a bigger problem than a torn
//  waveform.
//

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

class CaptureRing
{
public:
    CaptureRing() = default;

    /** Allocates. Message thread only. Capacity is rounded up to a power of two
        so the wrap is a mask rather than a modulo - this is read three times per
        sample in the goniometer's decimation loop. */
    void prepare (int numChannels, int minimumCapacity)
    {
        capacity = juce::nextPowerOfTwo (juce::jmax (1024, minimumCapacity));
        mask     = capacity - 1;

        storage.setSize (numChannels, capacity);
        storage.clear();

        writePosition.store (0, std::memory_order_release);
    }

    /** Real-time safe. Channels beyond the prepared count are ignored; a
        request for more samples than the whole ring keeps only the tail, which
        is what a ring can honestly promise. */
    void write (const float* const* channelData, int numChannels, int numSamples) noexcept
    {
        if (capacity == 0 || numSamples <= 0)
            return;

        const auto channels = juce::jmin (numChannels, storage.getNumChannels());
        auto position = writePosition.load (std::memory_order_relaxed);

        // A block longer than the whole ring can only leave its tail behind.
        // The skipped samples still advance the write position, because that
        // position is a clock and a clock that stops lying about how much audio
        // has passed is the only way the scope's history stays aligned.
        int sourceOffset = 0;

        if (numSamples > capacity)
        {
            sourceOffset = numSamples - capacity;
            position    += sourceOffset;
            numSamples   = capacity;
        }

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* source = channelData[ch] + sourceOffset;
            auto* destination   = storage.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
                destination[(int) ((position + (int64_t) i) & (int64_t) mask)] = source[i];
        }

        writePosition.store (position + numSamples, std::memory_order_release);
    }

    /** Total samples ever written. The reader uses this as a clock: two reads
        that quote the same position describe the same audio. */
    int64_t getWritePosition() const noexcept { return writePosition.load (std::memory_order_acquire); }

    int getCapacity() const noexcept { return capacity; }

    /** Copies numSamples ending at (and excluding) endPosition into dest.
        Pads with zeros at the front if the ring has not filled that far yet,
        which is what makes the first quarter second after a load draw a flat
        line rather than whatever was in the allocation. */
    void read (int channel, int64_t endPosition, float* destination, int numSamples) const noexcept
    {
        if (capacity == 0 || channel >= storage.getNumChannels() || numSamples <= 0)
        {
            juce::FloatVectorOperations::clear (destination, juce::jmax (0, numSamples));
            return;
        }

        const auto* source = storage.getReadPointer (channel);
        const auto start   = endPosition - numSamples;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto position = start + (int64_t) i;
            destination[i] = position < 0 ? 0.0f
                                          : source[(int) (position & (int64_t) mask)];
        }
    }

    /** The most recent numSamples. */
    void readLatest (int channel, float* destination, int numSamples) const noexcept
    {
        read (channel, getWritePosition(), destination, numSamples);
    }

private:
    juce::AudioBuffer<float> storage;
    int capacity = 0;
    int mask     = 0;

    std::atomic<int64_t> writePosition { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureRing)
};
