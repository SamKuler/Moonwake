#include "PlayerWindow.h"

#import <AppKit/AppKit.h>

void PlayerWindow::installFullScreenObserver()
{
    auto* tokens = [NSMutableArray array];
    for (NSNotificationName name in @[ NSWindowWillEnterFullScreenNotification,
                                      NSWindowDidEnterFullScreenNotification,
                                      NSWindowWillExitFullScreenNotification,
                                      NSWindowDidExitFullScreenNotification ])
    {
        id token = [[NSNotificationCenter defaultCenter] addObserverForName:name object:nil queue:nil
            usingBlock:^(NSNotification* notification)
            {
                // JUCE may replace the native peer when the title-bar buttons change.
                auto* peer = getPeer();
                if (peer == nullptr || notification.object != [(__bridge NSView*) peer->getNativeHandle() window])
                    return;

                const auto starting = [notification.name isEqualToString:NSWindowWillEnterFullScreenNotification]
                                   || [notification.name isEqualToString:NSWindowWillExitFullScreenNotification];
                if (starting) rememberNormalBounds();
                fullScreenTransitioning = starting;
                if (! starting)
                {
                    resized();
                    if (enteringFullScreen || pendingWindowed) startTimer (30);
                }
            }];
        [tokens addObject:token];
    }
    removeFullScreenObserver = [tokens]
    {
        for (id token in tokens) [[NSNotificationCenter defaultCenter] removeObserver:token];
    };
}
