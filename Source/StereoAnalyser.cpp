//
//  StereoAnalyser.cpp
//  FX Analyzer
//

#include "StereoAnalyser.h"

#include <cmath>

//==============================================================================
void StereoAnalyser::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
}

void StereoAnalyser::reset() noexcept
{
    sumLL = sumRR = sumLR = 0.0;

    correlation.store (0.0f, std::memory_order_relaxed);
    balance.store     (0.0f, std::memory_order_relaxed);
    widthDb.store     (kSilent, std::memory_order_relaxed);
    rmsLeftDb.store   (kSilent, std::memory_order_relaxed);
    rmsRightDb.store  (kSilent, std::memory_order_relaxed);
}

void StereoAnalyser::processBlock (const float* leftData, const float* rightData, int numSamples,
                                   float smoothingSeconds) noexcept
{
    if (leftData == nullptr || rightData == nullptr || numSamples <= 0)
        return;

    double blockLL = 0.0, blockRR = 0.0, blockLR = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const double l = (double) leftData[i];
        const double r = (double) rightData[i];

        blockLL += l * l;
        blockRR += r * r;
        blockLR += l * r;
    }

    // The window is the panel's own time constant, expressed as the fraction of
    // the old sums that survives this block. Reactivity therefore moves the
    // correlation meter's window along with everything else, which is the
    // behaviour anybody expects from a control called Reactivity.
    const auto tau     = juce::jmax (1.0e-3, (double) smoothingSeconds);
    const auto seconds = (double) numSamples / rate;
    const auto decay   = std::exp (-seconds / tau);

    sumLL = sumLL * decay + blockLL;
    sumRR = sumRR * decay + blockRR;
    sumLR = sumLR * decay + blockLR;

    // Effective window length, so the sums can be turned back into an RMS.
    const auto effectiveSamples = rate * tau;

    const auto denominator = std::sqrt (sumLL * sumRR);

    // Silence has no correlation. Reporting +1 for two digital zeros - which is
    // what 0/0 clamped upwards would give - has this meter reading "perfectly
    // mono" during a pause, and a meter that is confident about nothing is
    // worse than one that admits it.
    const auto energy = sumLL + sumRR;
    const auto haveSignal = energy > 1.0e-12;

    if (denominator > 1.0e-18 && haveSignal)
        correlation.store ((float) juce::jlimit (-1.0, 1.0, sumLR / denominator), std::memory_order_relaxed);
    else
        correlation.store (0.0f, std::memory_order_relaxed);

    const auto rmsL = std::sqrt (sumLL / juce::jmax (1.0, effectiveSamples));
    const auto rmsR = std::sqrt (sumRR / juce::jmax (1.0, effectiveSamples));

    rmsLeftDb.store  (rmsL > 0.0 ? (float) (20.0 * std::log10 (rmsL)) : kSilent, std::memory_order_relaxed);
    rmsRightDb.store (rmsR > 0.0 ? (float) (20.0 * std::log10 (rmsR)) : kSilent, std::memory_order_relaxed);

    if (haveSignal && (rmsL + rmsR) > 0.0)
        balance.store ((float) ((rmsR - rmsL) / (rmsR + rmsL)), std::memory_order_relaxed);
    else
        balance.store (0.0f, std::memory_order_relaxed);

    // Mid and side energy follow from the same three sums, which is the reason
    // to keep sumLR at all beyond correlation:
    //     4*|M|^2 = LL + 2*LR + RR      4*|S|^2 = LL - 2*LR + RR
    const auto midEnergy  = sumLL + 2.0 * sumLR + sumRR;
    const auto sideEnergy = sumLL - 2.0 * sumLR + sumRR;

    // Both energies get a floor at -70 dB of the total before the ratio is
    // taken, which bounds the answer to +/- 70 dB instead of letting either
    // limit produce an infinity. Special-casing them instead was the first
    // attempt and it was wrong in the more damaging direction: a signal against
    // its own inversion has *no* mid energy, so the zero test fired and the
    // meter reported "mono" for the one input that is the furthest thing from
    // it - and reported it confidently, on the page whose job is to catch
    // exactly that.
    if (haveSignal)
    {
        const auto floorEnergy = energy * 1.0e-7;

        widthDb.store ((float) (10.0 * std::log10 ((sideEnergy + floorEnergy) / (midEnergy + floorEnergy))),
                       std::memory_order_relaxed);
    }
    else
    {
        widthDb.store (kSilent, std::memory_order_relaxed);
    }
}
