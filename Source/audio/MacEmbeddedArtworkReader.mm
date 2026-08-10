#include "MacEmbeddedArtworkReader.h"

#import <AVFoundation/AVFoundation.h>

juce::MemoryBlock readMacEmbeddedArtwork (const juce::File& file)
{
    @autoreleasepool
    {
        auto* path = [NSString stringWithUTF8String:
            file.getFullPathName().toRawUTF8()];
        if (path == nil)
            return {};

        auto* asset = [AVURLAsset URLAssetWithURL:
            [NSURL fileURLWithPath:path] options:nil];
        for (AVMetadataItem* item in asset.commonMetadata)
        {
            if (! [item.commonKey isEqualToString:AVMetadataCommonKeyArtwork])
                continue;

            auto* data = item.dataValue;
            if (data == nil || data.length == 0
                || data.length > 20u * 1024u * 1024u)
                continue;
            return { data.bytes, data.length };
        }
        return {};
    }
}
