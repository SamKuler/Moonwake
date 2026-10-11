#pragma once

#include "MainComponent.h"

class PlayerWindow final : public juce::DocumentWindow, private juce::Timer
{
public:
    explicit PlayerWindow (juce::String name);
    ~PlayerWindow() override;
    void closeButtonPressed() override;
    void resized() override;
    void moved() override;
    void importCommandLine (const juce::String&);
    void present (PlayerPresentation);
    void setMiniPinned (bool);

private:
    void applyWindowedPresentation (PlayerPresentation);
    void timerCallback() override;
    void rememberNormalBounds();
    void setContentResizeLimits (const PlayerWindowSizing&);
    void updateContentPresentation();
   #if JUCE_MAC
    void installFullScreenObserver();
    std::function<void()> removeFullScreenObserver;
   #endif
    MainComponent* content = nullptr;
    PlayerPresentation presentation = PlayerPresentation::normal;
    juce::Rectangle<int> normalBounds;
    std::optional<PlayerPresentation> pendingWindowed;
    bool changing = false, enteringFullScreen = false, exitRequested = false, miniPinned = false;
    bool fullScreenTransitioning = false;
};
