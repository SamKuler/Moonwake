#include "PlayerWindow.h"

PlayerWindow::PlayerWindow (juce::String name)
    : DocumentWindow (std::move (name), juce::Desktop::getInstance().getDefaultLookAndFeel()
          .findColour (juce::ResizableWindow::backgroundColourId), allButtons)
{
    setUsingNativeTitleBar (true);
    content = new MainComponent();
    content->onPresentationRequested = [this] (auto mode) { present (mode); };
    content->presentationIntent = [this]
    {
        return pendingWindowed.value_or (enteringFullScreen ? PlayerPresentation::immersive : presentation);
    };
    content->onPinRequested = [this] (bool pinned) { setMiniPinned (pinned); };
    {
        const juce::ScopedValueSetter<bool> guard (changing, true);
        setContentOwned (content, false);
        setResizable (true, true);
        setContentResizeLimits (content->windowSizing (PlayerPresentation::normal));
        centreWithSize (1240, 800);
    }
    updateContentPresentation();
    normalBounds = getBounds();
    setVisible (true);
   #if JUCE_MAC
    installFullScreenObserver();
   #endif
}

PlayerWindow::~PlayerWindow()
{
   #if JUCE_MAC
    if (removeFullScreenObserver) removeFullScreenObserver();
   #endif
    stopTimer();
    if (content)
    {
        content->onPresentationRequested = {};
        content->presentationIntent = {};
        content->onPinRequested = {};
    }
    clearContentComponent();
    content = nullptr;
}

void PlayerWindow::closeButtonPressed()
{
    if (auto* application = juce::JUCEApplication::getInstance()) application->systemRequestedQuit();
}

void PlayerWindow::rememberNormalBounds()
{
    if (content && ! changing && ! fullScreenTransitioning && ! enteringFullScreen && ! pendingWindowed
        && presentation == PlayerPresentation::normal && ! isFullScreen() && ! isMinimised())
        normalBounds = getBounds();
}

void PlayerWindow::moved() { rememberNormalBounds(); }

void PlayerWindow::resized()
{
    if (! content) { DocumentWindow::resized(); return; }
    if (changing || fullScreenTransitioning || enteringFullScreen || pendingWindowed) return;
    if (isFullScreen() && presentation == PlayerPresentation::normal
        && content->supportsPresentation (PlayerPresentation::immersive))
        presentation = PlayerPresentation::immersive;
    else if (! isFullScreen() && presentation == PlayerPresentation::immersive)
    {
        applyWindowedPresentation (PlayerPresentation::normal);
        return;
    }
    updateContentPresentation();
    rememberNormalBounds();
}

void PlayerWindow::setContentResizeLimits (const PlayerWindowSizing& sizing)
{
    auto border = getContentComponentBorder();
    if (auto* peer = getPeer())
        if (const auto frame = peer->getFrameSizeIfPresent())
            border = border.addedTo (*frame);
    setResizeLimits (sizing.minimumWidth + border.getLeftAndRight(),
                     sizing.minimumHeight + border.getTopAndBottom(),
                     sizing.maximumWidth + border.getLeftAndRight(),
                     sizing.maximumHeight + border.getTopAndBottom());
}

void PlayerWindow::updateContentPresentation()
{
    const juce::ScopedValueSetter<bool> guard (changing, true);
    content->setPlayerPresentation (presentation, getContentComponentBorder().subtractedFrom (getLocalBounds()));
    DocumentWindow::resized();
}

void PlayerWindow::present (PlayerPresentation mode)
{
    if (! content || ! content->canChangePresentation() || ! content->supportsPresentation (mode)) return;
    if (mode == PlayerPresentation::immersive)
    {
        if (presentation == mode && ! pendingWindowed) return;
        rememberNormalBounds();
        pendingWindowed.reset();
        exitRequested = false;
        enteringFullScreen = true;
        const juce::ScopedValueSetter<bool> guard (changing, true);
        setAlwaysOnTop (false);
        setTitleBarButtonsRequired (allButtons, false);
        auto sizing = content->windowSizing (mode);
        sizing.maximumWidth = sizing.maximumHeight = 16384;
        setContentResizeLimits (sizing);
        setFullScreen (true);
        startTimer (30);
        return;
    }
    if (isFullScreen() || enteringFullScreen || fullScreenTransitioning)
    {
        pendingWindowed = mode;
        if (! fullScreenTransitioning && isFullScreen() && ! exitRequested)
        {
            exitRequested = true;
            enteringFullScreen = false;
            setFullScreen (false);
        }
        startTimer (30);
        return;
    }
    if (presentation != mode) applyWindowedPresentation (mode);
}

void PlayerWindow::timerCallback()
{
    if (fullScreenTransitioning) return;
    if (enteringFullScreen && isFullScreen())
    {
        enteringFullScreen = false;
        if (pendingWindowed)
        {
            exitRequested = true;
            setFullScreen (false);
        }
        else
        {
            presentation = PlayerPresentation::immersive;
            updateContentPresentation();
        }
    }
    if (! fullScreenTransitioning && pendingWindowed && ! isFullScreen() && ! enteringFullScreen)
    {
        const auto mode = *pendingWindowed;
        pendingWindowed.reset();
        exitRequested = false;
        applyWindowedPresentation (mode);
    }
    if (! pendingWindowed && ! enteringFullScreen) stopTimer();
}

void PlayerWindow::applyWindowedPresentation (PlayerPresentation mode)
{
    rememberNormalBounds();
    const juce::ScopedValueSetter<bool> guard (changing, true);
    presentation = mode;
    const auto sizing = content->windowSizing (mode);
    setTitleBarButtonsRequired (mode == PlayerPresentation::mini ? closeButton | minimiseButton : allButtons, false);
    setContentResizeLimits (sizing);
    setAlwaysOnTop (mode == PlayerPresentation::mini && miniPinned);
    if (mode == PlayerPresentation::mini)
        setContentComponentSize (sizing.width, sizing.height);
    else if (! normalBounds.isEmpty())
        setBounds (normalBounds);
    updateContentPresentation();
    content->setMiniPinned (miniPinned);
}

void PlayerWindow::setMiniPinned (bool pinned)
{
    miniPinned = pinned;
    setAlwaysOnTop (presentation == PlayerPresentation::mini && pinned);
    if (content) content->setMiniPinned (pinned);
}

void PlayerWindow::importCommandLine (const juce::String& commandLine)
{
    if (! content || commandLine.isEmpty()) return;
    juce::Array<juce::File> files;
    for (const auto& token : juce::StringArray::fromTokens (commandLine, true))
    {
        const juce::File file (token.trim().unquoted());
        if (file.exists()) files.addIfNotAlreadyThere (file);
    }
    if (! files.isEmpty()) content->importFilesAndFolders (files);
}
