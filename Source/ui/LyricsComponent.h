#pragma once

#include "lyrics/LyricsDocument.h"
#include "skin/SkinRenderer.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class LyricsComponent final : public juce::Component, private juce::Timer
{
public:
    LyricsComponent();
    ~LyricsComponent() override;

    void setDocument (std::shared_ptr<const LyricsDocument>);
    void setPlaybackPosition (double lyricTimeSeconds);
    void setStatus (juce::String title, juce::String detail);
    void setFollowing (bool);
    [[nodiscard]] bool isFollowing() const noexcept { return following; }

    std::function<void (double)> onSeek;
    std::function<void (bool)> onFollowingChanged;
    std::function<std::optional<SkinExpressionValue> (std::string_view)> extraContext;

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    friend class LyricsComponentTests;
    struct Row { juce::String text; float y = 0, height = 0; };
    [[nodiscard]] bool compact() const;
    [[nodiscard]] SkinRenderContext contextFor (int index, juce::Rectangle<float>) const;
    [[nodiscard]] int rowAt (juce::Point<float>) const;
    [[nodiscard]] float maximumScroll() const;
    void rebuildRows();
    void updateFollowPosition (bool animate);
    void scrollTo (float);
    void timerCallback() override;

    std::shared_ptr<const LyricsDocument> document;
    std::vector<Row> rows;
    juce::String statusTitle, statusDetail;
    double playbackPosition = 0;
    int activeLine = -1, keyboardLine = -1, hoveredLine = -1;
    float scrollPosition = 0, targetScroll = 0, documentHeight = 0;
    float padding = 0, lineGap = 0, followAnchor = 0, followDurationMs = 0;
    bool following = true;
    double lastAnimationTime = 0;
};
