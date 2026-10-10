#include "PlayerWindow.h"

PlayerWindow::PlayerWindow (juce::String name)
    : DocumentWindow (std::move (name), juce::Desktop::getInstance().getDefaultLookAndFeel()
          .findColour (juce::ResizableWindow::backgroundColourId), allButtons)
{
    setUsingNativeTitleBar (true);
    content = new MainComponent();
    content->onPresentationRequested = [this] (auto mode) { present (mode); };
    content->onPinRequested = [this] (bool pinned) { setMiniPinned (pinned); };
    setContentOwned (content, true);
    setResizable (true, true);
    setResizeLimits (1040, 700, 1800, 1200);
    centreWithSize (1240, 800);
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
    DocumentWindow::resized();
    if (! content || changing || fullScreenTransitioning || enteringFullScreen || pendingWindowed) return;
    if (isFullScreen() && presentation == PlayerPresentation::normal
        && content->supportsPresentation (PlayerPresentation::immersive))
    {
        const juce::ScopedValueSetter<bool> guard (changing, true);
        presentation = PlayerPresentation::immersive;
        content->setPlayerPresentation (presentation);
    }
    else if (! isFullScreen() && presentation == PlayerPresentation::immersive)
        applyWindowedPresentation (PlayerPresentation::normal);
    rememberNormalBounds();
}

void PlayerWindow::present (PlayerPresentation mode)
{
    if (! content || ! content->supportsPresentation (mode)) return;
    if (mode == PlayerPresentation::immersive)
    {
        if (presentation == mode && ! pendingWindowed) return;
        rememberNormalBounds();
        pendingWindowed.reset();
        exitRequested = false;
        enteringFullScreen = true;
        const juce::ScopedValueSetter<bool> guard (changing, true);
        presentation = mode;
        setAlwaysOnTop (false);
        setTitleBarButtonsRequired (allButtons, false);
        setResizeLimits (1040, 700, 16384, 16384);
        content->setPlayerPresentation (mode);
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
    setResizeLimits (sizing.minimumWidth, sizing.minimumHeight, sizing.maximumWidth, sizing.maximumHeight);
    setTitleBarButtonsRequired (mode == PlayerPresentation::mini ? closeButton | minimiseButton : allButtons, false);
    setAlwaysOnTop (mode == PlayerPresentation::mini && miniPinned);
    content->setPlayerPresentation (mode);
    if (mode == PlayerPresentation::mini)
        setContentComponentSize (sizing.width, sizing.height);
    else if (! normalBounds.isEmpty())
        setBounds (normalBounds);
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
