#pragma once

#include "library/LibraryProjection.h"
#include "skin/SkinRowLayout.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class PlaylistGridComponent final : public juce::Component,
                                    private juce::AsyncUpdater
{
public:
    using ArtworkResolver = std::function<juce::File (const juce::String&)>;
    using CoverLoader = std::function<juce::Image (const std::vector<juce::File>&)>;

    explicit PlaylistGridComponent (CoverLoader = {});
    ~PlaylistGridComponent() override;
    void setCollections (std::vector<LibraryCollectionSummary>, ArtworkResolver);
    void setQuery (juce::String);
    [[nodiscard]] int visibleCollectionCount() const noexcept;
    // Performs file I/O and image decoding; call from a worker thread.
    [[nodiscard]] static juce::Image buildCollectionCover (
        const std::vector<juce::File>& files, int maximumDimension = 512);

    void resized() override;
    void paint (juce::Graphics&) override;
    void lookAndFeelChanged() override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

    std::function<void (juce::String)> onOpenCollection;
    std::function<void (juce::String)> onContextMenu;

private:
    class Card;
    class GridViewport;
    std::unique_ptr<GridViewport> viewport;
    juce::Component content;
    std::vector<LibraryCollectionSummary> collections;
    std::vector<size_t> matchingCollections;
    std::vector<std::unique_ptr<Card>> cards;
    ArtworkResolver resolveArtwork;
    CoverLoader loadCollectionCover;
    juce::String query;
    SkinResolvedRowLayout cardLayout;
    int columns = 1, cardWidth = 0, cardHeight = 0, cardGap = 0;
    bool layingOut = false, needsArtwork = false;
    std::shared_ptr<std::atomic<uint64_t>> artworkGeneration {
        std::make_shared<std::atomic<uint64_t>> (0)
    };
    juce::ThreadPool coverPool { 1 };

    void rebuildMatches();
    void updateVisibleCards();
    [[nodiscard]] juce::Rectangle<int> cardBounds (int index) const;
    void scheduleVisibleArtwork();
    [[nodiscard]] bool artworkIsVisible (const Card&) const;
    void handleAsyncUpdate() override;
    void reveal (int index);
    bool focusRelative (const Card&, int offset);
};
