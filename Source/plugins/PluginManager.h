#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginScanProcess.h"
#include "PluginFormatPreferences.h"

#include <functional>

enum class PluginScanScope
{
    enabledFormats,
    vst3,
    audioUnit
};

struct PluginScanStatus
{
    bool scanning = false;
    bool cancellationRequested = false;
    float progress = 0.0f;
    int discovered = 0;
    int failed = 0;
    juce::String currentItem;
    juce::String message;
    uint64_t revision = 0;
};

struct PluginScannerConfiguration
{
    using CandidateProvider = std::function<juce::StringArray (
        const juce::String&, const juce::FileSearchPath&)>;

    juce::File executable;
    juce::File resultDirectory;
    juce::StringArray argumentsBeforeProtocol;
    int perCandidateTimeoutMilliseconds = 15000;
    CandidateProvider candidateProvider;
};

class PluginManager : private juce::Thread
{
public:
    PluginManager();
    explicit PluginManager (PluginScannerConfiguration);
    ~PluginManager() override;

    bool startScan (const juce::File& customVst3Directory = {},
                    PluginScanScope = PluginScanScope::enabledFormats);
    void cancelScan();
    bool cancelAndWaitForScan (int timeoutMilliseconds = 15000);
    [[nodiscard]] PluginScanStatus getScanStatus() const;
    [[nodiscard]] juce::Array<juce::PluginDescription> getKnownPlugins() const;
    [[nodiscard]] std::unique_ptr<juce::PluginDescription> findPluginByIdentifier (const juce::String& identifier) const;
    [[nodiscard]] std::unique_ptr<juce::AudioPluginInstance> createPluginInstance (const juce::PluginDescription& description,
                                                                                   double sampleRate,
                                                                                   int blockSize,
                                                                                   juce::String& errorMessage) const;
    void createPluginInstanceAsync (const juce::PluginDescription& description,
                                    double sampleRate,
                                    int blockSize,
                                    juce::AudioPluginFormat::PluginCreationCallback callback);
    [[nodiscard]] std::unique_ptr<juce::XmlElement> createKnownPluginXml() const;
    void restoreKnownPluginsFromXml (const juce::XmlElement& xml);
    bool writeKnownPluginCacheAtomically (const juce::File& targetFile, juce::String& errorMessage) const;
    bool readKnownPluginCache (const juce::File& sourceFile, juce::String& errorMessage);
    [[nodiscard]] juce::AudioPluginFormatManager& getFormatManager();
    [[nodiscard]] juce::KnownPluginList& getKnownPluginList();
    [[nodiscard]] const juce::KnownPluginList& getKnownPluginList() const;
    [[nodiscard]] juce::StringArray getRegisteredFormatNames() const;
    [[nodiscard]] PluginFormatPreferences getFormatPreferences() const noexcept { return formatPreferences; }
    bool setFormatPreferences (PluginFormatPreferences);
    [[nodiscard]] juce::StringArray getCustomVst3Directories() const { return customVst3Directories; }
    bool setCustomVst3Directories (juce::StringArray);
    [[nodiscard]] juce::StringArray getDefaultVst3Directories() const;
    static juce::Array<juce::PluginDescription> filterInsertEffects (const juce::Array<juce::PluginDescription>& plugins);
    static juce::File getDefaultScannerExecutable();

private:
    void run() override;
    void updateScanStatus (const std::function<void (PluginScanStatus&)>& update);

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    PluginScannerConfiguration scannerConfiguration;
    juce::StringArray customVst3Directories;
    juce::FileSearchPath scanVst3Directories;
    juce::StringArray unavailableScanDirectories;
    mutable juce::CriticalSection knownPluginLock;
    mutable juce::CriticalSection scanStatusLock;
    PluginScanStatus scanStatus;
    PluginFormatPreferences formatPreferences;
    PluginFormatPreferences scanFormats;
};
