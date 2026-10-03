#include "PluginManager.h"

namespace
{
struct ScopedScanResultDirectory
{
    ~ScopedScanResultDirectory()
    {
        if (directory != juce::File())
            static_cast<void> (directory.deleteRecursively());
    }

    juce::File directory;
};
}

PluginManager::PluginManager()
    : PluginManager (PluginScannerConfiguration {})
{
}

PluginManager::PluginManager (PluginScannerConfiguration configuration)
    : juce::Thread ("Moonwake plug-in scanner")
    , scannerConfiguration (std::move (configuration))
{
    if (scannerConfiguration.executable == juce::File())
        scannerConfiguration.executable = getDefaultScannerExecutable();
    if (scannerConfiguration.resultDirectory == juce::File())
        scannerConfiguration.resultDirectory = juce::File::getSpecialLocation (
            juce::File::tempDirectory);
    scannerConfiguration.perCandidateTimeoutMilliseconds = juce::jmax (
        50, scannerConfiguration.perCandidateTimeoutMilliseconds);

    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());

   #if JUCE_MAC && JUCE_PLUGINHOST_AU
    formatManager.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif
    setFormatPreferences (formatPreferences);
}

PluginManager::~PluginManager()
{
    cancelScan();
    if (! waitForThreadToExit (3000))
        static_cast<void> (stopThread (1000));
}

bool PluginManager::startScan (const juce::File& customVst3Directory, PluginScanScope scope)
{
    if (isThreadRunning())
        return false;

    const auto customPath = customVst3Directory != juce::File();
    const auto formats = getRegisteredFormatNames();
    const PluginFormatPreferences requestedFormats {
        formatPreferences.vst3 && scope != PluginScanScope::audioUnit && formats.contains ("VST3"),
        formatPreferences.audioUnit && scope != PluginScanScope::vst3
            && ! customPath && formats.contains ("AudioUnit")
    };
    if (! requestedFormats.vst3 && ! requestedFormats.audioUnit)
    {
        updateScanStatus ([&] (auto& status)
        {
            status.message = scope == PluginScanScope::audioUnit && ! formats.contains ("AudioUnit")
                ? "Audio Unit plug-ins are not available on this platform."
                : customPath || scope == PluginScanScope::vst3
                ? "Enable VST3 to scan VST3 plug-ins."
                : scope == PluginScanScope::audioUnit
                    ? "Enable Audio Unit to scan system-registered Audio Units."
                    : "Enable a plug-in format before scanning.";
        });
        return false;
    }

    if (! scannerConfiguration.executable.existsAsFile())
    {
        updateScanStatus ([this] (auto& status)
        {
            status.scanning = false;
            status.message = "Plug-in scanner helper was not found: "
                           + scannerConfiguration.executable.getFullPathName();
        });
        return false;
    }

    if (customPath && ! customVst3Directory.isDirectory())
    {
        updateScanStatus ([&] (auto& status)
        {
            status.message = "The VST3 scan folder is unavailable: " + customVst3Directory.getFullPathName();
        });
        return false;
    }

    scanFormats = requestedFormats;
    scanVst3Directories = juce::FileSearchPath {};
    unavailableScanDirectories.clear();
    if (scanFormats.vst3)
    {
        if (customPath)
            scanVst3Directories.add (customVst3Directory);
        else
        {
            for (const auto& path : getDefaultVst3Directories())
                scanVst3Directories.addIfNotAlreadyThere (juce::File (path));
            for (const auto& path : customVst3Directories)
            {
                const juce::File directory (path);
                if (directory.isDirectory()) scanVst3Directories.addIfNotAlreadyThere (directory);
                else unavailableScanDirectories.add (path);
            }
        }
    }
    updateScanStatus ([] (auto& status)
    {
        status.scanning = true;
        status.cancellationRequested = false;
        status.progress = 0.0f;
        status.discovered = 0;
        status.failed = 0;
        status.currentItem.clear();
        status.message = "Preparing plug-in scan...";
    });

    startThread (juce::Thread::Priority::normal);
    return true;
}

void PluginManager::cancelScan()
{
    if (! isThreadRunning())
        return;

    signalThreadShouldExit();
    updateScanStatus ([] (auto& status)
    {
        status.cancellationRequested = true;
        status.message = "Cancelling plug-in scan...";
    });
}

bool PluginManager::cancelAndWaitForScan (int timeoutMilliseconds)
{
    cancelScan();
    return ! isThreadRunning() || waitForThreadToExit (juce::jmax (0, timeoutMilliseconds));
}

PluginScanStatus PluginManager::getScanStatus() const
{
    const juce::ScopedLock scopedLock (scanStatusLock);
    return scanStatus;
}

juce::Array<juce::PluginDescription> PluginManager::getKnownPlugins() const
{
    const juce::ScopedLock lock (knownPluginLock);
    return knownPluginList.getTypes();
}

std::unique_ptr<juce::PluginDescription> PluginManager::findPluginByIdentifier (const juce::String& identifier) const
{
    const juce::ScopedLock lock (knownPluginLock);
    return knownPluginList.getTypeForIdentifierString (identifier);
}

std::unique_ptr<juce::AudioPluginInstance> PluginManager::createPluginInstance (const juce::PluginDescription& description,
                                                                                double sampleRate,
                                                                                int blockSize,
                                                                                juce::String& errorMessage) const
{
    return formatManager.createPluginInstance (description, sampleRate, blockSize, errorMessage);
}

void PluginManager::createPluginInstanceAsync (const juce::PluginDescription& description,
                                               double sampleRate,
                                               int blockSize,
                                               juce::AudioPluginFormat::PluginCreationCallback callback)
{
    formatManager.createPluginInstanceAsync (description,
                                             sampleRate,
                                             blockSize,
                                             std::move (callback));
}

std::unique_ptr<juce::XmlElement> PluginManager::createKnownPluginXml() const
{
    if (isThreadRunning())
        return nullptr;

    const juce::ScopedLock lock (knownPluginLock);
    return knownPluginList.createXml();
}

void PluginManager::restoreKnownPluginsFromXml (const juce::XmlElement& xml)
{
    const juce::ScopedLock lock (knownPluginLock);
    knownPluginList.recreateFromXml (xml);
}

bool PluginManager::writeKnownPluginCacheAtomically (const juce::File& targetFile,
                                                     juce::String& errorMessage) const
{
    errorMessage.clear();
    if (isThreadRunning())
    {
        errorMessage = "Cannot save the plug-in cache while a scan is running.";
        return false;
    }

    const auto directory = targetFile.getParentDirectory();
    if (! directory.isDirectory() && ! directory.createDirectory())
    {
        errorMessage = "Could not create plug-in cache directory: " + directory.getFullPathName();
        return false;
    }

    auto xml = createKnownPluginXml();
    if (xml == nullptr)
    {
        errorMessage = "Could not serialize the known plug-in cache.";
        return false;
    }

    juce::TemporaryFile temporaryFile (targetFile);
    if (! xml->writeTo (temporaryFile.getFile()))
    {
        errorMessage = "Could not write temporary plug-in cache: " + temporaryFile.getFile().getFullPathName();
        return false;
    }

    if (! temporaryFile.overwriteTargetFileWithTemporary())
    {
        errorMessage = "Could not replace plug-in cache: " + targetFile.getFullPathName();
        return false;
    }

    return true;
}

bool PluginManager::readKnownPluginCache (const juce::File& sourceFile, juce::String& errorMessage)
{
    errorMessage.clear();
    if (! sourceFile.existsAsFile())
    {
        errorMessage = "Plug-in cache file not found: " + sourceFile.getFullPathName();
        return false;
    }

    auto xml = juce::XmlDocument::parse (sourceFile);
    if (xml == nullptr)
    {
        errorMessage = "Plug-in cache is not valid XML: " + sourceFile.getFullPathName();
        return false;
    }

    restoreKnownPluginsFromXml (*xml);
    return true;
}

juce::AudioPluginFormatManager& PluginManager::getFormatManager()
{
    return formatManager;
}

juce::KnownPluginList& PluginManager::getKnownPluginList()
{
    return knownPluginList;
}

const juce::KnownPluginList& PluginManager::getKnownPluginList() const
{
    return knownPluginList;
}

juce::StringArray PluginManager::getRegisteredFormatNames() const
{
    juce::StringArray names;

    for (int i = 0; i < formatManager.getNumFormats(); ++i)
        if (const auto* format = formatManager.getFormat (i))
            names.add (format->getName());

    return names;
}

bool PluginManager::setFormatPreferences (PluginFormatPreferences next)
{
    if (isThreadRunning()) return false;
    const auto formats = getRegisteredFormatNames();
    next.vst3 = next.vst3 && formats.contains ("VST3");
    next.audioUnit = next.audioUnit && formats.contains ("AudioUnit");
    formatPreferences = next;
    return true;
}

bool PluginManager::setCustomVst3Directories (juce::StringArray directories)
{
    if (isThreadRunning()) return false;
    juce::StringArray normalised;
    for (const auto& directory : directories)
    {
        if (directory.isEmpty() || ! juce::File::isAbsolutePath (directory)) return false;
        normalised.addIfNotAlreadyThere (juce::File (directory).getFullPathName());
    }
    customVst3Directories = std::move (normalised);
    return true;
}

juce::StringArray PluginManager::getDefaultVst3Directories() const
{
    juce::StringArray result;
    for (int index = 0; index < formatManager.getNumFormats(); ++index)
        if (auto* format = formatManager.getFormat (index); format != nullptr && format->getName() == "VST3")
        {
            const auto paths = format->getDefaultLocationsToSearch();
            for (int pathIndex = 0; pathIndex < paths.getNumPaths(); ++pathIndex)
                result.add (paths[pathIndex].getFullPathName());
        }
    return result;
}

juce::Array<juce::PluginDescription> PluginManager::filterInsertEffects (const juce::Array<juce::PluginDescription>& plugins)
{
    juce::Array<juce::PluginDescription> effects;

    for (const auto& plugin : plugins)
        if (! plugin.isInstrument)
            effects.add (plugin);

    return effects;
}

juce::File PluginManager::getDefaultScannerExecutable()
{
   #if defined (MOONWAKE_PLUGIN_SCANNER_PATH)
    const juce::File configured (MOONWAKE_PLUGIN_SCANNER_PATH);
    if (configured.existsAsFile())
        return configured;
   #endif

    const auto executable = juce::File::getSpecialLocation (
        juce::File::currentExecutableFile);
   #if JUCE_MAC
    const auto macOSDirectory = executable.getParentDirectory();
    if (macOSDirectory.getFileName() == "MacOS")
        return macOSDirectory.getParentDirectory()
            .getChildFile ("Helpers")
            .getChildFile ("MoonwakePluginScanner.app")
            .getChildFile ("Contents")
            .getChildFile ("MacOS")
            .getChildFile ("MoonwakePluginScanner");
   #endif
   #if JUCE_WINDOWS
    return executable.getSiblingFile ("MoonwakePluginScanner.exe");
   #else
    return executable.getSiblingFile ("MoonwakePluginScanner");
   #endif
}

void PluginManager::run()
{
    int initialCount = 0;
    {
        const juce::ScopedLock lock (knownPluginLock);
        initialCount = knownPluginList.getNumTypes();
    }
    struct FormatCandidates
    {
        juce::String format;
        juce::StringArray candidates;
    };
    std::vector<FormatCandidates> work;
    int totalCandidates = 0;

    for (int index = 0; index < formatManager.getNumFormats()
                        && ! threadShouldExit(); ++index)
    {
        auto* format = formatManager.getFormat (index);
        if (format == nullptr || ! format->canScanForPlugins()
            || ! scanFormats.allows (format->getName()))
            continue;

        const auto searchPath = format->getName() == "VST3"
                                  ? scanVst3Directories : format->getDefaultLocationsToSearch();
        auto candidates = scannerConfiguration.candidateProvider
                            ? scannerConfiguration.candidateProvider (
                                  format->getName(), searchPath)
                            : format->searchPathsForPlugins (
                                  searchPath, true, true);
        candidates.removeDuplicates (false);
        totalCandidates += candidates.size();
        work.push_back ({ format->getName(), std::move (candidates) });
    }

    int processed = 0;
    int failedCount = 0;
    juce::String lastFailure;
    bool cancelled = threadShouldExit();

    ScopedScanResultDirectory scanResults;
    if (totalCandidates > 0)
    {
        const auto& base = scannerConfiguration.resultDirectory;
        if (! base.isDirectory() && ! base.createDirectory())
        {
            updateScanStatus ([&] (auto& status)
            {
                status.scanning = false;
                status.progress = 1.0f;
                status.failed = totalCandidates;
                status.currentItem.clear();
                status.message = "Could not create the plug-in scan result directory: "
                               + base.getFullPathName();
            });
            return;
        }

        scanResults.directory = base.getChildFile (
            "moonwake-plugin-scan-" + juce::Uuid().toString());
        if (! scanResults.directory.createDirectory())
        {
            updateScanStatus ([&] (auto& status)
            {
                status.scanning = false;
                status.progress = 1.0f;
                status.failed = totalCandidates;
                status.currentItem.clear();
                status.message = "Could not reserve an isolated plug-in scan workspace: "
                               + scanResults.directory.getFullPathName();
            });
            scanResults.directory = juce::File {};
            return;
        }
    }

    for (const auto& formatWork : work)
    {
        for (const auto& candidate : formatWork.candidates)
        {
            if (threadShouldExit())
            {
                cancelled = true;
                break;
            }

            updateScanStatus ([&] (auto& status)
            {
                status.currentItem = candidate;
                status.message = "Scanning " + formatWork.format + " plug-in...";
                status.progress = totalCandidates > 0
                                    ? static_cast<float> (processed)
                                          / static_cast<float> (totalCandidates)
                                    : 1.0f;
            });

            const auto resultFile = scanResults.directory.getChildFile (
                "result-" + juce::Uuid().toString() + ".xml");
            juce::StringArray command { scannerConfiguration.executable
                                           .getFullPathName() };
            command.addArray (scannerConfiguration.argumentsBeforeProtocol);
            command.addArray ({ "--scan", formatWork.format,
                                candidate, resultFile.getFullPathName() });
            const auto processResult = PluginScanProcess::run (
                command,
                scannerConfiguration.perCandidateTimeoutMilliseconds,
                [this] { return threadShouldExit(); });

            if (processResult.outcome == PluginScanProcessOutcome::cancelled)
            {
                cancelled = true;
                static_cast<void> (resultFile.deleteFile());
                break;
            }

            bool accepted = false;
            if (processResult.outcome == PluginScanProcessOutcome::completed)
            {
                if (const auto xml = juce::XmlDocument::parse (resultFile))
                {
                    juce::KnownPluginList discovered;
                    discovered.recreateFromXml (*xml);
                    const auto types = discovered.getTypes();
                    if (! types.isEmpty())
                    {
                        const juce::ScopedLock lock (knownPluginLock);
                        for (const auto& description : types)
                            knownPluginList.addType (description);
                        accepted = true;
                    }
                    else
                    {
                        lastFailure = "Scanner returned no plug-in descriptions for "
                                    + candidate + ".";
                    }
                }
                else
                {
                    lastFailure = "Scanner returned invalid XML for " + candidate + ".";
                }
            }
            else
            {
                lastFailure = processResult.diagnostic;
            }
            static_cast<void> (resultFile.deleteFile());
            if (! accepted)
                ++failedCount;
            ++processed;

            int discoveredCount = 0;
            {
                const juce::ScopedLock lock (knownPluginLock);
                discoveredCount = juce::jmax (
                    0, knownPluginList.getNumTypes() - initialCount);
            }
            updateScanStatus ([&] (auto& status)
            {
                status.discovered = discoveredCount;
                status.failed = failedCount;
                status.progress = totalCandidates > 0
                                    ? static_cast<float> (processed)
                                          / static_cast<float> (totalCandidates)
                                    : 1.0f;
            });
        }
        if (cancelled)
            break;
    }

    int discoveredCount = 0;
    {
        const juce::ScopedLock lock (knownPluginLock);
        knownPluginList.scanFinished();
        knownPluginList.sort (juce::KnownPluginList::sortAlphabetically, true);
        discoveredCount = juce::jmax (
            0, knownPluginList.getNumTypes() - initialCount);
    }
    updateScanStatus ([&] (auto& status)
    {
        status.scanning = false;
        status.cancellationRequested = false;
        status.progress = cancelled ? status.progress : 1.0f;
        status.currentItem.clear();
        status.discovered = discoveredCount;
        status.failed = failedCount;
        status.message = cancelled
                             ? "Plug-in scan cancelled."
                             : "Plug-in scan complete. Added " + juce::String (status.discovered)
                                   + " effect" + (status.discovered == 1 ? "." : "s.")
                                   + (failedCount > 0
                                        ? " " + juce::String (failedCount)
                                            + " candidate"
                                            + (failedCount == 1 ? " failed. " : "s failed. ")
                                            + lastFailure
                                        : juce::String{});
        if (! unavailableScanDirectories.isEmpty())
            status.message += " Skipped unavailable VST3 folders: "
                            + unavailableScanDirectories.joinIntoString (", ") + ".";
    });
}

void PluginManager::updateScanStatus (const std::function<void (PluginScanStatus&)>& update)
{
    const juce::ScopedLock scopedLock (scanStatusLock);
    update (scanStatus);
    ++scanStatus.revision;
}
