#include "LyricsComponent.h"

#include "JuceSkinAdapter.h"

#include <cmath>

LyricsComponent::LyricsComponent()
{
    setWantsKeyboardFocus (true);
    setTitle ("Lyrics");
    setDescription ("Use arrow keys to browse lyrics, Enter to seek, and Home to follow playback.");
}

LyricsComponent::~LyricsComponent() { stopTimer(); }

bool LyricsComponent::compact() const
{
    return getProperties().getWithDefault (JuceSkinAdapter::presentationProperty, "document").toString() == "line";
}

void LyricsComponent::setDocument (std::shared_ptr<const LyricsDocument> next)
{
    if (document == next) return;
    document = std::move (next);
    activeLine = document ? document->activeLineAt (playbackPosition) : -1;
    keyboardLine = hoveredLine = -1;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    scrollPosition = targetScroll = 0;
    setFollowing (true);
    rebuildRows();
    updateFollowPosition (false);
    repaint();
}

void LyricsComponent::setPlaybackPosition (double seconds)
{
    playbackPosition = seconds;
    const auto next = document ? document->activeLineAt (seconds) : -1;
    if (activeLine == next) return;
    activeLine = next;
    updateFollowPosition (true);
    repaint();
}

void LyricsComponent::setStatus (juce::String title, juce::String detail)
{
    if (statusTitle == title && statusDetail == detail) return;
    statusTitle = std::move (title);
    statusDetail = std::move (detail);
    repaint();
}

void LyricsComponent::setFollowing (bool next)
{
    if (following == next) return;
    following = next;
    if (following) keyboardLine = -1;
    else stopTimer();
    updateFollowPosition (true);
    repaint();
    if (onFollowingChanged) onFollowingChanged (following);
}

SkinRenderContext LyricsComponent::contextFor (int index, juce::Rectangle<float> bounds) const
{
    SkinRenderContext context;
    context.bounds = context.contentBounds = bounds;
    context.state.selected = index >= 0 && index == activeLine;
    context.state.hover = index >= 0 && index == hoveredLine;
    const auto focusedLine = keyboardLine >= 0 ? keyboardLine : juce::jmax (0, activeLine);
    context.state.focused = JuceSkinAdapter::hasVisibleKeyboardFocus (*this)
        && ((index >= 0 && index == focusedLine) || (index < 0 && rows.empty()));
    context.state.enabled = isEnabled();
    if (index >= 0 && index < static_cast<int> (rows.size()))
        context.primaryText = rows[static_cast<size_t> (index)].text;
    context.extraLookup = [this, index] (std::string_view path) -> std::optional<SkinExpressionValue>
    {
        if (path == "lyrics.synced") return document && document->isSynced();
        if (path == "lyrics.following") return following;
        if (path == "lyrics.hasText") return ! rows.empty();
        if (path == "lyrics.presentation") return juce::String (compact() ? "line" : "document");
        if (path == "line.index") return static_cast<double> (index);
        if (path == "line.active") return index >= 0 && index == activeLine;
        if (path == "line.past") return index >= 0 && index < activeLine;
        if (path == "line.timeSeconds") return document && document->isSynced() && index >= 0
            && index < static_cast<int> (document->lines.size()) ? document->lines[static_cast<size_t> (index)].timeSeconds : 0.0;
        return extraContext ? extraContext (path) : std::nullopt;
    };
    return context;
}

void LyricsComponent::rebuildRows()
{
    rows.clear();
    documentHeight = 0;
    auto* adapter = dynamic_cast<JuceSkinAdapter*> (&getLookAndFeel());
    if (adapter == nullptr || getWidth() <= 0) return;
    const auto controls = adapter->controlStyleFor (*this, SkinRenderTarget::lyrics, contextFor (-1, getLocalBounds().toFloat()));
    if (! controls || ! controls->padding || ! controls->lineGap || ! controls->followAnchor || ! controls->followDurationMs) return;
    padding = *controls->padding;
    lineGap = *controls->lineGap;
    followAnchor = *controls->followAnchor;
    followDurationMs = *controls->followDurationMs;
    if (! document) return;
    if (document->isSynced())
        for (const auto& line : document->lines) rows.push_back ({ line.text, 0, 0 });
    else if (document->plainText.isNotEmpty())
        for (const auto& text : juce::StringArray::fromLines (document->plainText)) rows.push_back ({ text, 0, 0 });

    const auto width = juce::jmax (1.0f, static_cast<float> (getWidth()) - padding * 2);
    auto y = document->isSynced() && ! compact() ? static_cast<float> (getHeight()) * followAnchor : padding;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        auto& row = rows[i];
        row.y = y;
        for (const auto selected : { false, true })
        {
            auto context = contextFor (static_cast<int> (i), { padding, 0, width, static_cast<float> (getHeight()) });
            context.state.selected = selected;
            const auto lookup = context.extraLookup;
            context.extraLookup = [lookup, selected] (std::string_view path) -> std::optional<SkinExpressionValue>
            {
                if (path == "line.active") return selected;
                return lookup (path);
            };
            if (const auto style = adapter->slotTextStyleFor (*this, SkinRenderTarget::lyrics, "line.text", context))
            {
                const juce::detail::ShapedText shaped (row.text, juce::detail::ShapedText::Options {}
                    .withFont (style->font).withWordWrapWidth (juce::jmax (1.0f, style->bounds.getWidth()))
                    .withTrailingWhitespacesShouldFit (false));
                const auto lines = juce::jmax (size_t (1), shaped.getLineTextRanges().getRanges().size());
                row.height = juce::jmax (row.height, std::ceil (style->font.getHeight() * static_cast<float> (lines)));
            }
        }
        y += row.height + lineGap;
    }
    documentHeight = y + (document->isSynced() && ! compact()
        ? static_cast<float> (getHeight()) * (1.0f - followAnchor) : padding);
    scrollPosition = juce::jlimit (0.0f, maximumScroll(), scrollPosition);
    updateFollowPosition (false);
}

float LyricsComponent::maximumScroll() const
{
    return juce::jmax (0.0f, documentHeight - static_cast<float> (getHeight()));
}

void LyricsComponent::updateFollowPosition (bool animate)
{
    if (! following || compact() || rows.empty() || ! document || ! document->isSynced()) return;
    targetScroll = activeLine >= 0 && activeLine < static_cast<int> (rows.size())
        ? juce::jlimit (0.0f, maximumScroll(), rows[static_cast<size_t> (activeLine)].y
            - static_cast<float> (getHeight()) * followAnchor)
        : 0.0f;
    const auto* adapter = dynamic_cast<JuceSkinAdapter*> (&getLookAndFeel());
    if (! animate || followDurationMs <= 0 || (adapter && adapter->reduceMotionFor (*this, SkinRenderTarget::lyrics)))
    {
        stopTimer();
        scrollPosition = targetScroll;
        return;
    }
    lastAnimationTime = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

void LyricsComponent::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto elapsed = juce::jmax (0.0, now - lastAnimationTime);
    lastAnimationTime = now;
    scrollPosition += (targetScroll - scrollPosition) * static_cast<float> (1.0 - std::exp (-5.0 * elapsed / juce::jmax (1.0f, followDurationMs)));
    if (std::abs (targetScroll - scrollPosition) < 0.25f)
    {
        scrollPosition = targetScroll;
        stopTimer();
    }
    repaint();
}

void LyricsComponent::resized() { rebuildRows(); }
void LyricsComponent::lookAndFeelChanged() { rebuildRows(); repaint(); }
void LyricsComponent::focusGained (FocusChangeType) { repaint(); }
void LyricsComponent::focusLost (FocusChangeType) { repaint(); }

void LyricsComponent::paint (juce::Graphics& graphics)
{
    auto* adapter = dynamic_cast<JuceSkinAdapter*> (&getLookAndFeel());
    if (! adapter) return;
    adapter->drawCustomComponent (graphics, *this, SkinRenderTarget::lyrics, contextFor (-1, getLocalBounds().toFloat()));
    if (rows.empty())
    {
        auto context = contextFor (-1, getLocalBounds().toFloat().reduced (padding));
        context.primaryText = statusTitle;
        context.secondaryText = statusDetail;
        adapter->drawCustomSlot (graphics, *this, SkinRenderTarget::lyrics, "empty.title", SkinRenderTarget::label, context);
        if (! compact())
            adapter->drawCustomSlot (graphics, *this, SkinRenderTarget::lyrics, "empty.detail", SkinRenderTarget::label, context);
        return;
    }
    const auto drawRow = [&] (int index, juce::Rectangle<float> bounds)
    {
        if (index < 0 || index >= static_cast<int> (rows.size())) return;
        auto context = contextFor (index, bounds);
        if (! compact())
            if (const auto textStyle = adapter->slotTextStyleFor (*this, SkinRenderTarget::lyrics, "line.text", context))
            {
                const juce::detail::ShapedText shaped (context.primaryText, juce::detail::ShapedText::Options {}
                    .withFont (textStyle->font).withWordWrapWidth (juce::jmax (1.0f, textStyle->bounds.getWidth()))
                    .withTrailingWhitespacesShouldFit (false));
                const auto& lines = shaped.getLineTextRanges().getRanges();
                const auto maximumLines = static_cast<size_t> (textStyle->maxLines);
                if (lines.size() > maximumLines)
                {
                    const auto fullText = context.primaryText;
                    for (size_t start = 0; start < lines.size(); start += maximumLines)
                    {
                        const auto end = juce::jmin (lines.size(), start + maximumLines);
                        context.primaryText = fullText.substring (static_cast<int> (lines[start].getStart()),
                            static_cast<int> (lines[end - 1].getEnd())).trimEnd();
                        context.bounds = context.contentBounds = bounds.withY (bounds.getY()
                            + static_cast<float> (start) * textStyle->font.getHeight())
                            .withHeight (std::ceil (static_cast<float> (end - start) * textStyle->font.getHeight()));
                        if (context.bounds.intersects (getLocalBounds().toFloat()))
                            adapter->drawCustomSlot (graphics, *this, SkinRenderTarget::lyrics, "line.text", SkinRenderTarget::label,
                                context, "line:" + juce::String (index) + ":" + juce::String (static_cast<int> (start)));
                    }
                    return;
                }
            }
        adapter->drawCustomSlot (graphics, *this, SkinRenderTarget::lyrics, "line.text", SkinRenderTarget::label,
                                std::move (context), "line:" + juce::String (index));
    };
    if (compact())
    {
        drawRow (document->isSynced() ? activeLine : 0, getLocalBounds().toFloat().reduced (padding));
        return;
    }
    juce::Graphics::ScopedSaveState saved (graphics);
    graphics.reduceClipRegion (getLocalBounds());
    const auto width = juce::jmax (0.0f, static_cast<float> (getWidth()) - 2 * padding);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& row = rows[i];
        const juce::Rectangle<float> bounds (padding, row.y - scrollPosition, width, row.height);
        if (bounds.getBottom() >= 0 && bounds.getY() < static_cast<float> (getHeight()))
            drawRow (static_cast<int> (i), bounds);
    }
}

int LyricsComponent::rowAt (juce::Point<float> position) const
{
    if (compact()) return activeLine;
    if (position.x < padding || position.x >= static_cast<float> (getWidth()) - padding) return -1;
    for (size_t i = 0; i < rows.size(); ++i)
        if (position.y + scrollPosition >= rows[i].y && position.y + scrollPosition < rows[i].y + rows[i].height)
            return static_cast<int> (i);
    return -1;
}

void LyricsComponent::scrollTo (float position)
{
    setFollowing (false);
    scrollPosition = targetScroll = juce::jlimit (0.0f, maximumScroll(), position);
    repaint();
}

void LyricsComponent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (compact() || rows.empty() || juce::approximatelyEqual (wheel.deltaY, 0.0f)) return;
    scrollTo (scrollPosition - wheel.deltaY * static_cast<float> (getHeight()));
}

void LyricsComponent::mouseDown (const juce::MouseEvent& event)
{
    if (! document || ! document->isSynced() || ! event.mods.isLeftButtonDown()) return;
    const auto index = rowAt (event.position);
    if (index < 0 || index >= static_cast<int> (document->lines.size())) return;
    if (onSeek) onSeek (document->lines[static_cast<size_t> (index)].timeSeconds);
    setFollowing (true);
}

void LyricsComponent::mouseMove (const juce::MouseEvent& event)
{
    const auto next = document && document->isSynced() ? rowAt (event.position) : -1;
    if (next == hoveredLine) return;
    hoveredLine = next;
    setMouseCursor (next >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void LyricsComponent::mouseExit (const juce::MouseEvent&)
{
    hoveredLine = -1;
    repaint();
}

bool LyricsComponent::keyPressed (const juce::KeyPress& key)
{
    if (rows.empty()) return false;
    if (key == juce::KeyPress::returnKey && document && document->isSynced())
    {
        const auto index = keyboardLine >= 0 ? keyboardLine : activeLine;
        if (index >= 0 && onSeek) onSeek (document->lines[static_cast<size_t> (index)].timeSeconds);
        setFollowing (true);
        return true;
    }
    if (compact()) return false;
    if (key == juce::KeyPress::homeKey)
    {
        if (document && document->isSynced()) { setFollowing (true); updateFollowPosition (true); }
        else scrollTo (0);
        return true;
    }
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        const auto start = keyboardLine >= 0 ? keyboardLine : juce::jmax (0, activeLine);
        keyboardLine = juce::jlimit (0, static_cast<int> (rows.size()) - 1, start + (key == juce::KeyPress::upKey ? -1 : 1));
        scrollTo (rows[static_cast<size_t> (keyboardLine)].y - static_cast<float> (getHeight()) * followAnchor);
        return true;
    }
    if (key == juce::KeyPress::pageUpKey || key == juce::KeyPress::pageDownKey)
    {
        scrollTo (scrollPosition + (key == juce::KeyPress::pageUpKey ? -1 : 1) * static_cast<float> (getHeight()));
        return true;
    }
    return false;
}

std::unique_ptr<juce::AccessibilityHandler> LyricsComponent::createAccessibilityHandler()
{
    class Value final : public juce::AccessibilityTextValueInterface
    {
    public:
        explicit Value (std::function<juce::String()> getter) : getText (std::move (getter)) {}
        bool isReadOnly() const override { return true; }
        juce::String getCurrentValueAsString() const override { return getText(); }
        void setValueAsString (const juce::String&) override {}
    private:
        std::function<juce::String()> getText;
    };
    auto value = std::make_unique<Value> ([this]
    {
        if (! document || rows.empty()) return statusTitle + ". " + statusDetail;
        if (document->isSynced())
        {
            const auto index = keyboardLine >= 0 ? keyboardLine : activeLine;
            return index >= 0 ? document->lines[static_cast<size_t> (index)].text : juce::String ("Lyrics have not started");
        }
        return document->plainText;
    });
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::staticText,
        juce::AccessibilityActions {}, juce::AccessibilityHandler::Interfaces (std::move (value)));
}
