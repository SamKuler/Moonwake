#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginScanProcess.h"

#include <functional>

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

    int scanDirectory (const juce::File& directory, bool recursive = true);
    bool startScan (const juce::File& customVst3Directory = {});
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
    static juce::Array<juce::PluginDescription> filterInsertEffects (const juce::Array<juce::PluginDescription>& plugins);
    static juce::String getDisplayGroup (const juce::PluginDescription& description);
    static juce::File getDefaultScannerExecutable();

private:
    void run() override;
    void updateScanStatus (const std::function<void (PluginScanStatus&)>& update);

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    PluginScannerConfiguration scannerConfiguration;
    juce::File customScanDirectory;
    mutable juce::CriticalSection knownPluginLock;
    mutable juce::CriticalSection scanStatusLock;
    PluginScanStatus scanStatus;
};
