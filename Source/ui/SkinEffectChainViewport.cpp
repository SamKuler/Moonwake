#include "SkinEffectChainViewport.h"

#include "JuceSkinAdapter.h"

void SkinEffectChainViewport::setSlotCount (int count)
{
    count = juce::jmax (0, count);
    if (slotCount == count) return;
    slotCount = count;
    repaint();
}

void SkinEffectChainViewport::paint (juce::Graphics& graphics)
{
    auto* adapter = dynamic_cast<JuceSkinAdapter*> (&getLookAndFeel());
    if (adapter == nullptr)
        return;

    SkinRenderContext context;
    context.repeatItemCount = slotCount;
    context.extraLookup = [count = slotCount] (std::string_view path)
        -> std::optional<SkinExpressionValue>
    {
        if (path == "chain.slotCount")
            return SkinExpressionValue (static_cast<double> (count));
        return std::nullopt;
    };
    static_cast<void> (adapter->drawCustomComponent (
        graphics, *this, SkinRenderTarget::effectChain,
        std::move (context), "chain"));
}
