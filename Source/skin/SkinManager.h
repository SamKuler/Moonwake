#pragma once

#include "SkinInstaller.h"
#include "SkinRegistry.h"
#include "SkinSession.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

struct SkinFailedSessionRecord
{
    SkinSelection selection;
    SkinSessionHealth health = SkinSessionHealth::fatal;
    SkinValidationReport runtimeReport;
};

struct InstalledSkinDescription
{
    SkinSelection selection;
    juce::String name;
    juce::String author;
    bool builtIn = false;
};

class SkinManager final : private juce::AsyncUpdater
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void skinChanged() = 0;
    };

    SkinManager();
    ~SkinManager() override;

    bool initialise (const juce::File& installRoot, SkinValidationReport&);
    SkinValidationReport importSkin (const juce::File&);
    bool preview (const SkinSelection&, SkinValidationReport&);
    bool applyPreview (SkinValidationReport&);
    void cancelPreview();
    bool remove (const SkinSelection&, SkinValidationReport&);
    bool setDeveloperDirectory (const juce::File&, SkinValidationReport&);
    bool restoreDeveloperDirectory (const juce::File&, SkinValidationReport&);
    bool reloadDeveloperSkin (SkinValidationReport&);
    bool activate (const SkinSelection&, bool persist, SkinValidationReport&);
    void restoreFoundation();

    [[nodiscard]] juce::File developerDirectory() const;
    [[nodiscard]] std::shared_ptr<SkinSession> activeSession() const;
    [[nodiscard]] SkinSelection activeSelection() const;
    [[nodiscard]] SkinSelection committedSelection() const;
    [[nodiscard]] bool isPreviewing() const noexcept;
    [[nodiscard]] bool isDeveloperPreviewing() const noexcept;
    [[nodiscard]] std::optional<SkinFailedSessionRecord> lastFailedSession() const;
    void clearLastFailedSession();
    [[nodiscard]] std::vector<SkinSelection> installedSkins() const;
    [[nodiscard]] std::vector<InstalledSkinDescription> installedSkinDescriptions() const;
    void setPersistenceCallback (std::function<void (const SkinSelection&)>);
    void addListener (Listener*);
    void removeListener (Listener*);

    void processPendingFallback();

private:
    struct ResolvedSkin
    {
        SkinSelection selection;
        std::shared_ptr<const CompiledSkin> compiled;
    };

    [[nodiscard]] std::optional<ResolvedSkin> compileSelection (
        const SkinSelection&, SkinValidationReport&);
    [[nodiscard]] std::shared_ptr<SkinSession> makeSession (
        std::shared_ptr<const CompiledSkin>, const SkinSelection&, bool preview);
    void enqueueFatalSession (const std::shared_ptr<SkinSession>&,
                              const SkinSelection&);
    void persistCommittedSelection();
    void handleAsyncUpdate() override;

    struct PendingFatalSession
    {
        std::shared_ptr<SkinSession> session;
        SkinSelection selection;
    };

    SkinRegistry registry;
    std::unique_ptr<SkinInstaller> installer;
    juce::File snapshotDirectory;
    juce::File developmentDirectory;
    std::shared_ptr<const CompiledSkin> foundationSkin;
    std::shared_ptr<const CompiledSkin> defaultSkin;
    std::shared_ptr<SkinSession> active;
    std::shared_ptr<SkinSession> beforePreview;
    SkinSelection activeSelectionValue;
    SkinSelection beforePreviewSelection;
    SkinSelection committedSelectionValue;
    bool previewActive = false;
    bool developerPreviewActive = false;
    std::optional<SkinFailedSessionRecord> failedSession;
    std::function<void (const SkinSelection&)> persistenceCallback;
    juce::ListenerList<Listener> listeners;
    juce::CriticalSection pendingFatalLock;
    std::vector<PendingFatalSession> pendingFatalSessions;
    std::shared_ptr<int> lifetimeToken = std::make_shared<int> (0);
};
