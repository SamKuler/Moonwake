#include "ProductPaths.h"

#include "ProductIdentity.h"

namespace ProductPaths
{
juce::File applicationDataRoot()
{
#if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile ("Library")
        .getChildFile ("Application Support")
        .getChildFile (MoonwakeIdentity::applicationSupportLeaf);
#else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile (MoonwakeIdentity::applicationSupportLeaf);
#endif
}

juce::File installedSkinsRoot()
{
    return applicationDataRoot().getChildFile ("Skins");
}

juce::File artworkCacheRoot()
{
    return applicationDataRoot().getChildFile ("Artwork");
}
}
