#include "ChannelRouter.h"

#include <array>
#include <cmath>

namespace
{
constexpr auto equalPower = 0.7071067811865476f;
constexpr auto heightFold = 0.5f;
constexpr auto lfeFold = 0.5f;

struct RouteTargets
{
    void add (int channel, float gain)
    {
        if (channel < 0 || count >= static_cast<int> (channels.size()))
            return;

        channels[static_cast<size_t> (count)] = channel;
        gains[static_cast<size_t> (count)] = gain;
        ++count;
    }

    std::array<int, 2> channels { -1, -1 };
    std::array<float, 2> gains {};
    int count = 0;
};

RouteTargets getRouteTargets (juce::AudioChannelSet::ChannelType type,
                              int sourceChannel,
                              const juce::AudioChannelSet& sourceLayout,
                              const juce::AudioChannelSet& destinationLayout,
                              int destinationChannels)
{
    RouteTargets result;
    const auto destinationFor = [&destinationLayout, destinationChannels] (auto channelType)
    {
        const auto index = destinationLayout.getChannelIndexForType (channelType);
        return juce::isPositiveAndBelow (index, destinationChannels) ? index : -1;
    };
    const auto addStereoPair = [&] (float gain)
    {
        result.add (destinationFor (juce::AudioChannelSet::left), gain);
        result.add (destinationFor (juce::AudioChannelSet::right), gain);
    };

    if (const auto exact = destinationFor (type); exact >= 0)
    {
        result.add (exact, 1.0f);
        return result;
    }

    if (destinationChannels == 1 && destinationLayout.getTypeOfChannel (0) == juce::AudioChannelSet::centre)
    {
        result.add (0, type == juce::AudioChannelSet::LFE ? lfeFold : equalPower);
        return result;
    }

    const auto addFirstAvailable = [&] (std::initializer_list<juce::AudioChannelSet::ChannelType> candidates,
                                        float gain)
    {
        for (const auto candidate : candidates)
            if (const auto destination = destinationFor (candidate); destination >= 0)
            {
                result.add (destination, gain);
                break;
            }
    };

    if (type == juce::AudioChannelSet::centre)
        addStereoPair (equalPower);
    else if (type == juce::AudioChannelSet::LFE)
        addStereoPair (lfeFold);
    else if (type == juce::AudioChannelSet::wideLeft)
        addFirstAvailable ({ juce::AudioChannelSet::left }, equalPower);
    else if (type == juce::AudioChannelSet::wideRight)
        addFirstAvailable ({ juce::AudioChannelSet::right }, equalPower);
    else if (type == juce::AudioChannelSet::centreSurround)
    {
        addFirstAvailable ({ juce::AudioChannelSet::leftSurroundRear, juce::AudioChannelSet::leftSurround,
                             juce::AudioChannelSet::leftSurroundSide, juce::AudioChannelSet::left }, equalPower);
        addFirstAvailable ({ juce::AudioChannelSet::rightSurroundRear, juce::AudioChannelSet::rightSurround,
                             juce::AudioChannelSet::rightSurroundSide, juce::AudioChannelSet::right }, equalPower);
    }
    else if (type == juce::AudioChannelSet::topFrontLeft)
        addFirstAvailable ({ juce::AudioChannelSet::topSideLeft, juce::AudioChannelSet::left }, heightFold);
    else if (type == juce::AudioChannelSet::topFrontRight)
        addFirstAvailable ({ juce::AudioChannelSet::topSideRight, juce::AudioChannelSet::right }, heightFold);
    else if (type == juce::AudioChannelSet::topRearLeft)
        addFirstAvailable ({ juce::AudioChannelSet::topSideLeft, juce::AudioChannelSet::leftSurroundRear,
                             juce::AudioChannelSet::leftSurround, juce::AudioChannelSet::leftSurroundSide,
                             juce::AudioChannelSet::left }, heightFold);
    else if (type == juce::AudioChannelSet::topRearRight)
        addFirstAvailable ({ juce::AudioChannelSet::topSideRight, juce::AudioChannelSet::rightSurroundRear,
                             juce::AudioChannelSet::rightSurround, juce::AudioChannelSet::rightSurroundSide,
                             juce::AudioChannelSet::right }, heightFold);
    else if (type == juce::AudioChannelSet::leftSurround)
    {
        const auto surround = destinationFor (juce::AudioChannelSet::leftSurroundSide);
        result.add (surround >= 0 ? surround : destinationFor (juce::AudioChannelSet::left),
                    surround >= 0 ? 1.0f : equalPower);
    }
    else if (type == juce::AudioChannelSet::rightSurround)
    {
        const auto surround = destinationFor (juce::AudioChannelSet::rightSurroundSide);
        result.add (surround >= 0 ? surround : destinationFor (juce::AudioChannelSet::right),
                    surround >= 0 ? 1.0f : equalPower);
    }
    else if (type == juce::AudioChannelSet::leftSurroundSide
             || type == juce::AudioChannelSet::leftSurroundRear)
    {
        const auto surround = destinationFor (juce::AudioChannelSet::leftSurround);
        result.add (surround >= 0 ? surround : destinationFor (juce::AudioChannelSet::left), equalPower);
    }
    else if (type == juce::AudioChannelSet::rightSurroundSide
             || type == juce::AudioChannelSet::rightSurroundRear)
    {
        const auto surround = destinationFor (juce::AudioChannelSet::rightSurround);
        result.add (surround >= 0 ? surround : destinationFor (juce::AudioChannelSet::right), equalPower);
    }
    else if (type == juce::AudioChannelSet::topSideLeft)
    {
        auto destination = destinationFor (juce::AudioChannelSet::leftSurround);
        if (destination < 0)
            destination = destinationFor (juce::AudioChannelSet::leftSurroundSide);
        if (destination < 0)
            destination = destinationFor (juce::AudioChannelSet::left);
        result.add (destination, heightFold);
    }
    else if (type == juce::AudioChannelSet::topSideRight)
    {
        auto destination = destinationFor (juce::AudioChannelSet::rightSurround);
        if (destination < 0)
            destination = destinationFor (juce::AudioChannelSet::rightSurroundSide);
        if (destination < 0)
            destination = destinationFor (juce::AudioChannelSet::right);
        result.add (destination, heightFold);
    }

    if (result.count == 0
        && (sourceLayout.isDiscreteLayout() || destinationLayout.isDiscreteLayout())
        && juce::isPositiveAndBelow (sourceChannel, destinationChannels))
        result.add (sourceChannel, 1.0f);

    return result;
}
} // namespace

void ChannelRouter::route (const juce::AudioBuffer<float>& source,
                           const juce::AudioChannelSet& sourceLayout,
                           juce::AudioBuffer<float>& destination,
                           const juce::AudioChannelSet& destinationLayout,
                           int destinationStartSample,
                           int numSamples)
{
    const auto availableSamples = juce::jmin (source.getNumSamples(),
                                              destination.getNumSamples() - destinationStartSample);
    if (destinationStartSample < 0 || availableSamples <= 0 || numSamples <= 0)
        return;
    const auto samplesToRoute = juce::jmin (availableSamples, numSamples);

    for (int channel = 0; channel < destination.getNumChannels(); ++channel)
        destination.clear (channel, destinationStartSample, samplesToRoute);

    const auto sourceChannels = juce::jmin (source.getNumChannels(), sourceLayout.size());
    const auto destinationChannels = juce::jmin (destination.getNumChannels(), destinationLayout.size());
    if (sourceChannels <= 0 || destinationChannels <= 0)
        return;

    if (sourceChannels == 1)
    {
        const auto centre = destinationLayout.getChannelIndexForType (juce::AudioChannelSet::centre);
        if (juce::isPositiveAndBelow (centre, destinationChannels))
        {
            destination.copyFrom (centre,
                                  destinationStartSample,
                                  source,
                                  0,
                                  0,
                                  samplesToRoute);
            return;
        }

        const auto left = destinationLayout.getChannelIndexForType (juce::AudioChannelSet::left);
        const auto right = destinationLayout.getChannelIndexForType (juce::AudioChannelSet::right);
        if (juce::isPositiveAndBelow (left, destinationChannels))
            destination.addFrom (left,
                                 destinationStartSample,
                                 source,
                                 0,
                                 0,
                                 samplesToRoute,
                                 equalPower);
        if (juce::isPositiveAndBelow (right, destinationChannels))
            destination.addFrom (right,
                                 destinationStartSample,
                                 source,
                                 0,
                                 0,
                                 samplesToRoute,
                                 equalPower);
        return;
    }

    constexpr auto maximumRoutedChannels = 64;
    const auto routedSourceChannels = juce::jmin (sourceChannels, maximumRoutedChannels);
    const auto routedDestinationChannels = juce::jmin (destinationChannels, maximumRoutedChannels);
    std::array<RouteTargets, maximumRoutedChannels> routes;
    std::array<float, maximumRoutedChannels> destinationWeights {};

    for (int sourceChannel = 0; sourceChannel < routedSourceChannels; ++sourceChannel)
    {
        const auto type = sourceLayout.getTypeOfChannel (sourceChannel);
        routes[static_cast<size_t> (sourceChannel)] = getRouteTargets (type,
                                                                       sourceChannel,
                                                                       sourceLayout,
                                                                       destinationLayout,
                                                                       routedDestinationChannels);
        const auto& route = routes[static_cast<size_t> (sourceChannel)];
        for (int target = 0; target < route.count; ++target)
            destinationWeights[static_cast<size_t> (route.channels[static_cast<size_t> (target)])]
                += std::abs (route.gains[static_cast<size_t> (target)]);
    }

    for (int sourceChannel = 0; sourceChannel < routedSourceChannels; ++sourceChannel)
    {
        const auto& route = routes[static_cast<size_t> (sourceChannel)];
        for (int target = 0; target < route.count; ++target)
        {
            const auto destinationChannel = route.channels[static_cast<size_t> (target)];
            const auto normalisation = juce::jmax (1.0f,
                                                   destinationWeights[static_cast<size_t> (destinationChannel)]);
            destination.addFrom (destinationChannel,
                                 destinationStartSample,
                                 source,
                                 sourceChannel,
                                 0,
                                 samplesToRoute,
                                 route.gains[static_cast<size_t> (target)] / normalisation);
        }
    }
}
