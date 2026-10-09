#include "TrackFileIdentity.h"

#if ! JUCE_WINDOWS
 #include <sys/stat.h>
#endif

juce::String trackFileIdentity (const juce::File& file)
{
    const auto fullPath = file.getFullPathName();
#if JUCE_WINDOWS
    if (const auto identifier = file.getFileIdentifier(); identifier != 0)
        if (const auto volume = file.getVolumeSerialNumber(); volume != 0)
            return "windows:" + juce::String::toHexString (volume)
                 + ":" + juce::String::toHexString (static_cast<juce::int64> (identifier));
#else
    struct stat information {};
    if (::stat (fullPath.toRawUTF8(), &information) == 0)
        return "posix:"
             + juce::String::toHexString (
                   static_cast<juce::int64> (information.st_dev))
             + ":"
             + juce::String::toHexString (
                   static_cast<juce::int64> (information.st_ino));
#endif
    return "path:" + fullPath;
}
