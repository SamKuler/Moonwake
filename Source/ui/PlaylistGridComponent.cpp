#include "PlaylistGridComponent.h"

#include "AlbumArtworkComponent.h"
#include "JuceSkinAdapter.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace
{
juce::String summaryText (const LibraryCollectionSummary& collection)
{
    auto text = juce::String (collection.trackCount) + (collection.trackCount == 1 ? " song" : " songs");
    if (std::isfinite (collection.durationSeconds) && collection.durationSeconds > 0.0)
    {
        const auto minutes = std::floor (collection.durationSeconds / 60.0);
        text += juce::String::fromUTF8 (" · ") + (minutes < 1 ? juce::String ("Under a minute")
            : (minutes >= 60 ? juce::String (std::floor (minutes / 60.0), 0) + " hr " : juce::String {})
                + juce::String (std::fmod (minutes, 60.0), 0) + " min");
    }
    return text;
}
}

class PlaylistGridComponent::GridViewport final : public juce::Viewport
{
public:
    explicit GridViewport (PlaylistGridComponent& grid) : owner (grid) {}
    void visibleAreaChanged (const juce::Rectangle<int>&) override
    {
        if (owner.layingOut) return;
        owner.updateVisibleCards();
        owner.scheduleVisibleArtwork();
    }
private:
    PlaylistGridComponent& owner;
};

class PlaylistGridComponent::Card final : public juce::Button
{
public:
    Card (PlaylistGridComponent& grid, LibraryCollectionSummary item, int index)
        : juce::Button (item.name), owner (grid)
    {
        setWantsKeyboardFocus (true);
        setMouseClickGrabsKeyboardFocus (true);
        cover.setInterceptsMouseClicks (false, false);
        cover.setAccessible (false);
        cover.getProperties().set (JuceSkinAdapter::presentationProperty, "cover");
        cover.extraContext = [this] (std::string_view path) { return contextValue (path); };
        addAndMakeVisible (cover);
        setCollection (item, index);
        onClick = [this]
        {
            if (owner.onOpenCollection) owner.onOpenCollection (collection.id);
        };
    }
    ~Card() override
    {
        if (artworkRequest) artworkRequest->store (true);
    }

    void setCollection (const LibraryCollectionSummary& item, int index)
    {
        matchIndex = index;
        setExplicitFocusOrder (index + 1);
        if (collection == item) return;
        releaseCover();
        collection = item;
        setName (collection.name);
        setComponentID (collection.id);
        setTitle (collection.name);
        setTooltip (collection.name + "\n" + collection.subtitle + "\n" + summaryText (collection));
        cover.setImage (collection.id, collection.name, {});
        repaint();
    }

    void resized() override
    {
        if (auto* adapter = dynamic_cast<JuceSkinAdapter*> (&owner.getLookAndFeel()))
            adapter->applySlotStyle (cover, owner, SkinRenderTarget::playlistGrid, "card.cover");
        cover.setBounds (slotBounds ("card.cover").getSmallestIntegerContainer());
    }

    void paintButton (juce::Graphics& graphics, bool hovered, bool pressed) override
    {
        auto* adapter = dynamic_cast<JuceSkinAdapter*> (&owner.getLookAndFeel());
        if (adapter == nullptr) return;
        const auto draw = [&] (juce::StringRef slot, SkinRenderTarget target, const juce::String& text)
        {
            SkinRenderContext context;
            context.bounds = context.contentBounds = slotBounds (juce::String (slot));
            if (context.bounds.isEmpty()) return;
            context.primaryText = text;
            context.secondaryText = collection.subtitle;
            context.state.enabled = isEnabled();
            context.state.hover = hovered;
            context.state.pressed = pressed;
            context.state.focused = juce::String (slot) == "card.surface" && JuceSkinAdapter::hasVisibleKeyboardFocus (*this);
            context.extraLookup = [this] (std::string_view path) -> std::optional<SkinExpressionValue>
            {
                return contextValue (path);
            };
            adapter->drawCustomSlot (graphics, owner, SkinRenderTarget::playlistGrid, slot, target,
                                    std::move (context), "collection:" + collection.id + ":" + juce::String (slot));
        };
        draw ("card.surface", SkinRenderTarget::surface, {});
        draw ("card.title", SkinRenderTarget::label, collection.name);
        draw ("card.summary", SkinRenderTarget::label, summaryText (collection));
        auto description = collection.subtitle;
        if (collection.createdAtMs > 0)
            description += (description.isEmpty() ? juce::String ("Created ") : juce::String::fromUTF8 (" · "))
                + juce::Time (collection.createdAtMs).formatted ("%b %d, %Y");
        draw ("card.date", SkinRenderTarget::label, description);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
        {
            grabKeyboardFocus();
            if (owner.onContextMenu) owner.onContextMenu (collection.id);
        }
        else juce::Button::mouseDown (event);
    }
    void mouseUp (const juce::MouseEvent& event) override
    {
        if (! event.mods.isPopupMenu()) juce::Button::mouseUp (event);
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
        {
            triggerClick();
            return true;
        }
        if (key == juce::KeyPress::leftKey) return owner.focusRelative (*this, -1);
        if (key == juce::KeyPress::rightKey) return owner.focusRelative (*this, 1);
        if (key == juce::KeyPress::upKey) return owner.focusRelative (*this, -owner.columns);
        if (key == juce::KeyPress::downKey) return owner.focusRelative (*this, owner.columns);
        if (key == juce::KeyPress::homeKey) return owner.focusRelative (*this, -matchIndex);
        if (key == juce::KeyPress::endKey) return owner.focusRelative (*this, owner.visibleCollectionCount() - 1 - matchIndex);
        if (key.getKeyCode() == juce::KeyPress::tabKey)
        {
            const auto offset = key.getModifiers().isShiftDown() ? -1 : 1;
            if (juce::isPositiveAndBelow (matchIndex + offset, owner.visibleCollectionCount()))
                return owner.focusRelative (*this, offset);
        }
        return juce::Button::keyPressed (key);
    }
    void focusGained (FocusChangeType cause) override
    {
        juce::Button::focusGained (cause);
        owner.reveal (matchIndex);
        repaint();
    }
    void focusLost (FocusChangeType cause) override
    {
        juce::Button::focusLost (cause);
        owner.triggerAsyncUpdate();
    }
    void setCover (juce::Image image)
    {
        hasArtwork = image.isValid();
        cover.setImage (collection.id, collection.name, std::move (image));
        repaint();
    }
    void releaseCover()
    {
        if (artworkRequest)
        {
            artworkRequest->store (true);
            artworkRequest.reset();
        }
        requestedArtwork = false;
        if (hasArtwork) setCover ({});
    }

    LibraryCollectionSummary collection;
    int matchIndex = -1;
    bool requestedArtwork = false;
    std::shared_ptr<std::atomic_bool> artworkRequest;

private:
    std::optional<SkinExpressionValue> contextValue (std::string_view path) const
    {
        if (path == "collection.id") return collection.id;
        if (path == "collection.name") return collection.name;
        if (path == "collection.trackCount") return static_cast<double> (collection.trackCount);
        if (path == "collection.durationSeconds") return collection.durationSeconds;
        if (path == "collection.createdAtMs") return static_cast<double> (collection.createdAtMs);
        if (path == "collection.hasArtwork") return hasArtwork;
        return std::nullopt;
    }
    juce::Rectangle<float> slotBounds (const juce::String& slot) const
    {
        const auto found = owner.cardLayout.slots.find (slot);
        return found != owner.cardLayout.slots.end() ? found->second : juce::Rectangle<float> {};
    }
    PlaylistGridComponent& owner;
    AlbumArtworkComponent cover;
    bool hasArtwork = false;
};

PlaylistGridComponent::PlaylistGridComponent (CoverLoader loader)
    : viewport (std::make_unique<GridViewport> (*this)), loadCollectionCover (std::move (loader))
{
    if (! loadCollectionCover)
        loadCollectionCover = [] (const std::vector<juce::File>& files) { return buildCollectionCover (files); };
    setTitle ("Music collections");
    setWantsKeyboardFocus (false);
    viewport->setViewedComponent (&content, false);
    viewport->setScrollBarsShown (true, false);
    addAndMakeVisible (*viewport);
}

PlaylistGridComponent::~PlaylistGridComponent()
{
    cancelPendingUpdate();
    ++*artworkGeneration;
    coverPool.removeAllJobs (true, -1);
    viewport->setViewedComponent (nullptr, false);
}

void PlaylistGridComponent::setCollections (std::vector<LibraryCollectionSummary> items, ArtworkResolver resolver)
{
    resolveArtwork = std::move (resolver);
    if (collections == items) return;
    collections = std::move (items);
    rebuildMatches();
}

void PlaylistGridComponent::setQuery (juce::String value)
{
    value = value.trim();
    if (query == value) return;
    query = std::move (value);
    rebuildMatches();
}

int PlaylistGridComponent::visibleCollectionCount() const noexcept { return static_cast<int> (matchingCollections.size()); }

void PlaylistGridComponent::rebuildMatches()
{
    matchingCollections.clear();
    for (auto& card : cards) card->matchIndex = -1;
    for (size_t index = 0; index < collections.size(); ++index)
    {
        const auto& item = collections[index];
        if (query.isEmpty() || item.name.containsIgnoreCase (query) || item.subtitle.containsIgnoreCase (query))
        {
            const auto matchIndex = visibleCollectionCount();
            matchingCollections.push_back (index);
            for (auto& card : cards)
                if (card->collection.id == item.id) card->setCollection (item, matchIndex);
        }
    }
    resized();
    for (const auto& card : cards)
        if (card->hasKeyboardFocus (true)) { reveal (card->matchIndex); break; }
    repaint();
}

void PlaylistGridComponent::resized()
{
    if (layingOut) return;
    {
        const juce::ScopedValueSetter<bool> guard (layingOut, true);
        viewport->setBounds (getLocalBounds());
        auto* adapter = dynamic_cast<JuceSkinAdapter*> (&getLookAndFeel());
        if (adapter == nullptr || getLocalBounds().isEmpty()) return;
        viewport->setScrollBarThickness (adapter->getDefaultScrollbarWidth());
        const auto controls = adapter->controlStyleFor (*this, SkinRenderTarget::playlistGrid);
        if (! controls || ! controls->minItemWidth || ! controls->itemGap) return;
        const auto minimumWidth = *controls->minItemWidth;
        cardGap = juce::jmax (0, juce::roundToInt (*controls->itemGap));
        const auto layoutAtWidth = [&] (int width) -> int
        {
            const auto gapWidth = static_cast<float> (cardGap);
            columns = juce::jmax (1, static_cast<int> (std::floor ((static_cast<float> (width) + gapWidth) / (minimumWidth + gapWidth))));
            cardWidth = juce::jmax (1, (width - cardGap * (columns - 1)) / columns);
            SkinRenderContext context;
            context.bounds = context.contentBounds = { 0.0f, 0.0f, static_cast<float> (cardWidth), static_cast<float> (cardWidth) };
            context.tableRowCount = visibleCollectionCount();
            const auto layout = adapter->rowLayoutFor (*this, SkinRenderTarget::playlistGrid, context);
            if (! layout) return 0;
            cardLayout = *layout;
            cardHeight = juce::jmax (1, juce::roundToInt (cardLayout.rowHeight));
            const auto rows = (visibleCollectionCount() + columns - 1) / columns;
            return rows * (cardHeight + cardGap) - (rows == 0 ? 0 : cardGap);
        };
        auto width = getWidth();
        auto height = layoutAtWidth (width);
        if (height > getHeight())
        {
            width = juce::jmax (0, width - viewport->getScrollBarThickness());
            height = layoutAtWidth (width);
        }
        content.setSize (width, height);
    }
    updateVisibleCards();
    scheduleVisibleArtwork();
}

juce::Rectangle<int> PlaylistGridComponent::cardBounds (int index) const
{
    return { (index % columns) * (cardWidth + cardGap), (index / columns) * (cardHeight + cardGap),
             cardWidth, cardHeight };
}

void PlaylistGridComponent::updateVisibleCards()
{
    if (layingOut || cardHeight == 0) return;
    const juce::ScopedValueSetter<bool> guard (layingOut, true);
    const auto view = viewport->getViewArea();
    const auto stride = cardHeight + cardGap;
    const auto first = juce::jmax (0, view.getY() / stride - 1) * columns;
    const auto last = juce::jmin (visibleCollectionCount(), ((view.getBottom() + stride - 1) / stride + 1) * columns);
    int focusedIndex = -1;
    for (const auto& card : cards)
        if (card->hasKeyboardFocus (true)) focusedIndex = card->matchIndex;

    std::vector<std::unique_ptr<Card>> next;
    const auto retain = [&] (int index)
    {
        const auto& item = collections[matchingCollections[static_cast<size_t> (index)]];
        const auto existing = std::find_if (cards.begin(), cards.end(), [&] (const auto& card)
            { return card != nullptr && card->collection.id == item.id; });
        auto card = existing != cards.end() ? std::move (*existing) : std::make_unique<Card> (*this, item, index);
        card->setCollection (item, index);
        card->setBounds (cardBounds (index));
        card->resized();
        content.addAndMakeVisible (*card);
        next.push_back (std::move (card));
    };
    if (focusedIndex >= 0 && focusedIndex < first) retain (focusedIndex);
    for (int index = first; index < last; ++index) retain (index);
    if (focusedIndex >= last && focusedIndex >= 0) retain (focusedIndex);
    cards = std::move (next);
}

void PlaylistGridComponent::paint (juce::Graphics& graphics)
{
    if (auto* adapter = dynamic_cast<JuceSkinAdapter*> (&getLookAndFeel()))
        adapter->drawCustomComponent (graphics, *this, SkinRenderTarget::playlistGrid, {}, "collections");
    if (needsArtwork) triggerAsyncUpdate();
}

void PlaylistGridComponent::lookAndFeelChanged() { resized(); }
void PlaylistGridComponent::visibilityChanged() { scheduleVisibleArtwork(); }
void PlaylistGridComponent::parentHierarchyChanged() { scheduleVisibleArtwork(); }

void PlaylistGridComponent::scheduleVisibleArtwork()
{
    for (const auto& card : cards)
        if (! artworkIsVisible (*card)) card->releaseCover();
    needsArtwork = true;
    triggerAsyncUpdate();
}

bool PlaylistGridComponent::artworkIsVisible (const Card& card) const
{
    for (auto* component = static_cast<const juce::Component*> (this); component != nullptr; component = component->getParentComponent())
        if (! component->isVisible()) return false;
    return card.getBounds().intersects (viewport->getViewArea());
}

void PlaylistGridComponent::handleAsyncUpdate()
{
    updateVisibleCards();
    for (auto* component = static_cast<juce::Component*> (this); component != nullptr; component = component->getParentComponent())
        if (! component->isVisible()) return;
    needsArtwork = false;
    const auto view = viewport->getViewArea();
    const auto generation = artworkGeneration;
    const auto version = generation->load();
    for (const auto& card : cards)
    {
        if (! card->getBounds().intersects (view))
        {
            card->releaseCover();
            continue;
        }
        if (card->requestedArtwork) continue;
        card->requestedArtwork = true;
        std::vector<juce::File> files;
        juce::StringArray keys;
        if (resolveArtwork)
            for (const auto& key : card->collection.artworkKeys)
                if (key.isNotEmpty() && ! keys.contains (key))
                {
                    keys.add (key);
                    files.push_back (resolveArtwork (key));
                    if (files.size() == 4) break;
                }
        if (files.empty()) continue;
        const auto cancellation = std::make_shared<std::atomic_bool> (false);
        card->artworkRequest = cancellation;
        const juce::Component::SafePointer<Card> safe (card.get());
        const juce::Component::SafePointer<PlaylistGridComponent> safeGrid (this);
        coverPool.addJob ([safe, safeGrid, generation, version, cancellation,
                          loadCover = loadCollectionCover, coverFiles = std::move (files)]
        {
            if (cancellation->load() || generation->load() != version) return;
            auto image = loadCover (coverFiles);
            if (cancellation->load() || generation->load() != version) return;
            juce::MessageManager::callAsync ([safe, safeGrid, generation, version, cancellation,
                                             coverImage = std::move (image)] () mutable
            {
                if (cancellation->load() || safe == nullptr || safeGrid == nullptr
                    || generation->load() != version || safe->artworkRequest != cancellation) return;
                safe->artworkRequest.reset();
                if (safeGrid->artworkIsVisible (*safe)) safe->setCover (std::move (coverImage));
                else safe->releaseCover();
            });
        });
    }
}

void PlaylistGridComponent::reveal (int index)
{
    const auto bounds = cardBounds (index);
    const auto view = viewport->getViewArea();
    if (bounds.getY() < view.getY()) viewport->setViewPosition (0, bounds.getY());
    else if (bounds.getBottom() > view.getBottom())
        viewport->setViewPosition (0, bounds.getHeight() >= view.getHeight()
            ? bounds.getY() : bounds.getBottom() - view.getHeight());
}

bool PlaylistGridComponent::focusRelative (const Card& current, int offset)
{
    const auto index = current.matchIndex + offset;
    if (juce::isPositiveAndBelow (index, visibleCollectionCount()))
    {
        reveal (index);
        const auto found = std::find_if (cards.begin(), cards.end(), [index] (const auto& card)
            { return card->matchIndex == index; });
        if (found != cards.end()) (*found)->grabKeyboardFocus();
    }
    return true;
}

juce::Image PlaylistGridComponent::buildCollectionCover (const std::vector<juce::File>& files, int maximumDimension)
{
    if (maximumDimension <= 0) return {};
    std::vector<juce::Image> images;
    std::set<juce::String> paths;
    for (const auto& file : files)
    {
        if (file == juce::File() || ! paths.insert (file.getFullPathName()).second) continue;
        auto image = juce::ImageFileFormat::loadFrom (file);
        if (image.isNull()) continue;
        const auto scale = juce::jmin (1.0, static_cast<double> (maximumDimension) / juce::jmax (image.getWidth(), image.getHeight()));
        images.push_back (image.rescaled (juce::jmax (1, juce::roundToInt (image.getWidth() * scale)),
                                         juce::jmax (1, juce::roundToInt (image.getHeight() * scale)),
                                         juce::Graphics::highResamplingQuality));
        if (images.size() == 4) break;
    }
    if (images.empty()) return {};
    if (images.size() == 1) return images.front();
    juce::Image result (juce::Image::ARGB, maximumDimension, maximumDimension, true);
    juce::Graphics graphics (result);
    for (int index = 0; index < 4; ++index)
    {
        const auto left = (index % 2) * maximumDimension / 2;
        const auto top = (index / 2) * maximumDimension / 2;
        const auto right = ((index % 2) + 1) * maximumDimension / 2;
        const auto bottom = ((index / 2) + 1) * maximumDimension / 2;
        const auto bounds = juce::Rectangle<int>::leftTopRightBottom (left, top, right, bottom).toFloat();
        const auto& image = images[static_cast<size_t> (index) % images.size()];
        juce::Graphics::ScopedSaveState save (graphics);
        graphics.reduceClipRegion (bounds.toNearestInt());
        graphics.drawImageTransformed (image, juce::RectanglePlacement (juce::RectanglePlacement::fillDestination)
            .getTransformToFit (image.getBounds().toFloat(), bounds), false);
    }
    return result;
}
