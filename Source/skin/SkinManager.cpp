#include "SkinManager.h"

#include "SkinBuiltInPackages.h"
#include "SkinCompiler.h"

#include <algorithm>

SkinManager::SkinManager() = default;

SkinManager::~SkinManager()
{
    cancelPendingUpdate();
    lifetimeToken.reset();
    if (active) active->setFatalCallback ({});
    if (beforePreview) beforePreview->setFatalCallback ({});
    const juce::ScopedLock lock (pendingFatalLock);
    for (const auto& pending : pendingFatalSessions)
        if (pending.session) pending.session->setFatalCallback ({});
    pendingFatalSessions.clear();
}

bool SkinManager::initialise (const juce::File& installRoot,
                              SkinValidationReport& report)
{
    snapshotDirectory = installRoot.getChildFile (".snapshots");
    if (! snapshotDirectory.createDirectory()
        || ! registry.initialise (installRoot, report))
        return false;
    installer = std::make_unique<SkinInstaller> (installRoot);
    if (! installer->recover (report) || ! registry.refresh (report))
        return false;

    const auto foundationPackage = SkinBuiltInPackages::create (
        SkinBuiltInPackages::Package::foundation, report);
    foundationSkin = foundationPackage
        ? SkinCompiler::compile (*foundationPackage, {}, report) : nullptr;
    const auto defaultPackage = SkinBuiltInPackages::create (
        SkinBuiltInPackages::Package::defaultSkin, report);
    defaultSkin = defaultPackage && foundationSkin
        ? SkinCompiler::compile (*defaultPackage, foundationSkin, report) : nullptr;
    if (! foundationSkin || ! defaultSkin || report.hasErrors()) return false;

    activeSelectionValue = { defaultSkin->manifest.id, defaultSkin->manifest.version };
    committedSelectionValue = activeSelectionValue;
    active = makeSession (defaultSkin, activeSelectionValue, false);
    return active != nullptr;
}

SkinValidationReport SkinManager::importSkin (const juce::File& source)
{
    SkinValidationReport report;
    if (! installer || ! foundationSkin)
    {
        report.addError ("RUNTIME007", source.getFullPathName(), {},
                         "SkinManager is not initialised.");
        return report;
    }
    const auto package = SkinPackage::captureSnapshot (
        source, snapshotDirectory, report);
    const auto compiled = package
        ? SkinCompiler::compile (*package, foundationSkin, report) : nullptr;
    if (! package || ! compiled || report.hasErrors()) return report;
    const SkinSelection incoming { compiled->manifest.id,
                                   compiled->manifest.version };
    if (previewActive
        && ((incoming.id == activeSelectionValue.id
             && incoming.version != activeSelectionValue.version)
            || (incoming.id == committedSelectionValue.id
                && incoming.version != committedSelectionValue.version)))
    {
        report.addError ("RUNTIME005", compiled->manifest.id,
                         compiled->manifest.version,
                         "A different version of a previewed or committed skin cannot be imported during preview.",
                         "Apply or cancel the current preview before importing this version.");
        return report;
    }
    if (! installer->install (*package, compiled->manifest,
                              committedSelectionValue, report))
        return report;
    static_cast<void> (registry.refresh (report));
    return report;
}

std::optional<SkinManager::ResolvedSkin> SkinManager::compileSelection (
    const SkinSelection& requested, SkinValidationReport& report)
{
    // Older sessions may have selected Foundation before it became internal-only.
    const auto resolved = registry.resolve (requested.id == "builtin.foundation"
        ? SkinSelection { "builtin.default", {} } : requested);
    if (! resolved)
    {
        report.addError ("RUNTIME005", requested.id, requested.version,
                         "Skin preview selection is not installed.");
        return std::nullopt;
    }
    if (resolved->id == "builtin.default")
        return ResolvedSkin { *resolved, defaultSkin };

    const auto package = registry.packageFor (*resolved, snapshotDirectory, report);
    const auto compiled = package
        ? SkinCompiler::compile (*package, foundationSkin, report) : nullptr;
    if (! compiled || report.hasErrors()) return std::nullopt;
    if (compiled->manifest.id != resolved->id
        || compiled->manifest.version != resolved->version)
    {
        report.addError ("RUNTIME004", "manifest.json", {},
                         "Installed skin manifest does not match its registry path.");
        return std::nullopt;
    }
    return ResolvedSkin { *resolved, compiled };
}

std::shared_ptr<SkinSession> SkinManager::makeSession (
    std::shared_ptr<const CompiledSkin> compiled,
    const SkinSelection& selection,
    bool previewSession)
{
    if (! compiled) return {};
    auto session = std::make_shared<SkinSession> (
        std::move (compiled), previewSession);
    std::weak_ptr<int> lifetime (lifetimeToken);
    std::weak_ptr<SkinSession> weakSession (session);
    session->setFatalCallback ([this, lifetime, weakSession, selection]
    {
        if (lifetime.expired()) return;
        if (const auto failed = weakSession.lock())
            enqueueFatalSession (failed, selection);
    });
    return session;
}

void SkinManager::enqueueFatalSession (
    const std::shared_ptr<SkinSession>& session,
    const SkinSelection& selection)
{
    {
        const juce::ScopedLock lock (pendingFatalLock);
        pendingFatalSessions.push_back ({ session, selection });
    }
    triggerAsyncUpdate();
}

bool SkinManager::preview (const SkinSelection& selection,
                           SkinValidationReport& report)
{
    const auto resolved = compileSelection (selection, report);
    if (! resolved) return false;
    auto session = makeSession (resolved->compiled, resolved->selection, true);
    if (! session)
    {
        report.addError ("RUNTIME005", selection.id, selection.version,
                         "Skin preview session could not be created.");
        return false;
    }
    if (! previewActive)
    {
        beforePreview = active;
        beforePreviewSelection = activeSelectionValue;
    }
    active = std::move (session);
    activeSelectionValue = resolved->selection;
    previewActive = true;
    developerPreviewActive = false;
    listeners.call ([] (Listener& listener) { listener.skinChanged(); });
    return true;
}

bool SkinManager::applyPreview (SkinValidationReport& report)
{
    if (! previewActive || ! active)
    {
        report.addError ("RUNTIME005", {}, {},
                         "There is no skin preview to apply.");
        return false;
    }
    if (developerPreviewActive)
    {
        report.addError ("RUNTIME005", activeSelectionValue.id,
                         activeSelectionValue.version,
                         "Developer-directory skins are preview-only.",
                         "Import the developer skin package before applying it.");
        return false;
    }
    active->commitPreview();
    beforePreview.reset();
    beforePreviewSelection = {};
    previewActive = false;
    developerPreviewActive = false;
    committedSelectionValue = activeSelectionValue;
    persistCommittedSelection();
    listeners.call ([] (Listener& listener) { listener.skinChanged(); });
    return true;
}

void SkinManager::cancelPreview()
{
    if (! previewActive) return;
    active = std::move (beforePreview);
    activeSelectionValue = beforePreviewSelection;
    beforePreviewSelection = {};
    previewActive = false;
    developerPreviewActive = false;
    listeners.call ([] (Listener& listener) { listener.skinChanged(); });
}

bool SkinManager::remove (const SkinSelection& requested,
                          SkinValidationReport& report)
{
    if (! installer) return false;
    const auto resolved = registry.resolve (requested);
    if (! resolved)
    {
        report.addError ("PKG007", requested.id, requested.version,
                         "Requested installed skin version was not found.");
        return false;
    }
    if (*resolved == committedSelectionValue
        || (previewActive && (*resolved == activeSelectionValue
                              || *resolved == beforePreviewSelection)))
    {
        report.addError ("PKG008", resolved->id, resolved->version,
                         "A committed or previewed skin version cannot be removed.");
        return false;
    }
    if (! installer->remove (*resolved, activeSelectionValue, report)) return false;
    return registry.refresh (report);
}

bool SkinManager::setDeveloperDirectory (
    const juce::File& directory, SkinValidationReport& report)
{
    const auto package = SkinPackage::captureSnapshot (
        directory, snapshotDirectory, report);
    const auto compiled = package && foundationSkin
        ? SkinCompiler::compile (*package, foundationSkin, report) : nullptr;
    if (! compiled || report.hasErrors()) return false;
    developmentDirectory = directory;
    return true;
}

bool SkinManager::restoreDeveloperDirectory (
    const juce::File& directory, SkinValidationReport& report)
{
    return setDeveloperDirectory (directory, report);
}

bool SkinManager::reloadDeveloperSkin (SkinValidationReport& report)
{
    if (developmentDirectory == juce::File())
    {
        report.addError ("RUNTIME005", {}, {},
                         "No developer skin directory is configured.");
        return false;
    }
    const auto package = SkinPackage::captureSnapshot (
        developmentDirectory, snapshotDirectory, report);
    const auto compiled = package && foundationSkin
        ? SkinCompiler::compile (*package, foundationSkin, report) : nullptr;
    if (! compiled || report.hasErrors()) return false;
    const SkinSelection selection { compiled->manifest.id,
                                    compiled->manifest.version };
    auto session = makeSession (compiled, selection, true);
    if (! previewActive)
    {
        beforePreview = active;
        beforePreviewSelection = activeSelectionValue;
    }
    active = std::move (session);
    activeSelectionValue = selection;
    previewActive = true;
    developerPreviewActive = true;
    listeners.call ([] (Listener& listener) { listener.skinChanged(); });
    return true;
}

bool SkinManager::activate (const SkinSelection& selection, bool persist,
                            SkinValidationReport& report)
{
    const auto resolved = compileSelection (selection, report);
    if (! resolved) return false;
    auto session = makeSession (resolved->compiled, resolved->selection, false);
    if (! session) return false;
    beforePreview.reset();
    previewActive = false;
    developerPreviewActive = false;
    active = std::move (session);
    activeSelectionValue = resolved->selection;
    committedSelectionValue = activeSelectionValue;
    if (persist) persistCommittedSelection();
    listeners.call ([] (Listener& listener) { listener.skinChanged(); });
    return true;
}

void SkinManager::restoreFoundation()
{
    if (! foundationSkin) return;
    beforePreview.reset();
    beforePreviewSelection = {};
    previewActive = false;
    activeSelectionValue = { foundationSkin->manifest.id,
                             foundationSkin->manifest.version };
    active = makeSession (foundationSkin, activeSelectionValue, false);
    committedSelectionValue = activeSelectionValue;
    developerPreviewActive = false;
    persistCommittedSelection();
    listeners.call ([] (Listener& listener) { listener.skinChanged(); });
}

juce::File SkinManager::developerDirectory() const
{
    return developmentDirectory;
}

std::shared_ptr<SkinSession> SkinManager::activeSession() const
{
    return active;
}

SkinSelection SkinManager::activeSelection() const
{
    return activeSelectionValue;
}

SkinSelection SkinManager::committedSelection() const
{
    return committedSelectionValue;
}

bool SkinManager::isPreviewing() const noexcept
{
    return previewActive;
}

bool SkinManager::isDeveloperPreviewing() const noexcept
{
    return previewActive && developerPreviewActive;
}

std::optional<SkinFailedSessionRecord> SkinManager::lastFailedSession() const
{
    return failedSession;
}

void SkinManager::clearLastFailedSession()
{
    failedSession.reset();
}

std::vector<SkinSelection> SkinManager::installedSkins() const
{
    std::vector<SkinSelection> result;
    for (const auto& entry : registry.entries()) result.push_back (entry.selection);
    return result;
}

std::vector<InstalledSkinDescription> SkinManager::selectableSkinDescriptions() const
{
    std::vector<InstalledSkinDescription> result;
    const auto entries = registry.entries();
    result.reserve (entries.size() + 1);
    for (const auto& entry : entries)
    {
        if (entry.selection.id == "builtin.foundation") continue;

        InstalledSkinDescription description;
        description.selection = entry.selection;
        description.name = entry.name.isNotEmpty() ? entry.name
                                                   : entry.selection.id;
        description.author = entry.author;
        description.builtIn = entry.builtIn;

        const CompiledSkin* compiled = nullptr;
        if (entry.selection.id == "builtin.default")
            compiled = defaultSkin.get();
        else if (entry.selection == activeSelectionValue && active != nullptr)
            compiled = active->compiledSkin().get();

        if (compiled != nullptr)
        {
            description.name = compiled->manifest.name;
            description.author = compiled->manifest.authorName;
        }
        result.push_back (std::move (description));
    }

    const auto activeInstalled = std::any_of (
        result.begin(), result.end(), [this] (const auto& description)
        {
            return description.selection == activeSelectionValue;
        });
    if (! activeInstalled && activeSelectionValue.id != "builtin.foundation"
        && active != nullptr && active->compiledSkin() != nullptr)
    {
        const auto& manifest = active->compiledSkin()->manifest;
        result.push_back ({ activeSelectionValue, manifest.name,
                            manifest.authorName,
                            activeSelectionValue.id.startsWith ("builtin.") });
    }
    return result;
}

void SkinManager::setPersistenceCallback (
    std::function<void (const SkinSelection&)> callback)
{
    persistenceCallback = std::move (callback);
}

void SkinManager::addListener (Listener* listener)
{
    listeners.add (listener);
}

void SkinManager::removeListener (Listener* listener)
{
    listeners.remove (listener);
}

void SkinManager::persistCommittedSelection()
{
    if (persistenceCallback) persistenceCallback (committedSelectionValue);
}

void SkinManager::handleAsyncUpdate()
{
    std::vector<PendingFatalSession> pending;
    {
        const juce::ScopedLock lock (pendingFatalLock);
        pending.swap (pendingFatalSessions);
    }

    for (const auto& failure : pending)
    {
        if (! failure.session
            || failure.session->health() != SkinSessionHealth::fatal)
            continue;
        failedSession = SkinFailedSessionRecord {
            failure.selection,
            failure.session->health(),
            failure.session->runtimeReport()
        };

        if (failure.session == active)
        {
            restoreFoundation();
            continue;
        }

        if (previewActive && failure.session == beforePreview && foundationSkin)
        {
            beforePreviewSelection = { foundationSkin->manifest.id,
                                       foundationSkin->manifest.version };
            beforePreview = makeSession (foundationSkin,
                                         beforePreviewSelection, false);
            committedSelectionValue = beforePreviewSelection;
            persistCommittedSelection();
        }
    }
}

void SkinManager::processPendingFallback()
{
    handleUpdateNowIfNeeded();
}
