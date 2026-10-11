#include "state/ListeningPresetStore.h"
#include "fixtures/TemporaryDirectoryCleanup.h"
#include <juce_data_structures/juce_data_structures.h>
#include <limits>

namespace
{
EffectChainPreset makeChain()
{
    EffectChainPreset preset;
    preset.name = "  Night chain  ";
    preset.chain.addSlotWithId (17, "Missing EQ", "VST3", "missing:eq",
                               { juce::AudioChannelSet::right, juce::AudioChannelSet::left });
    preset.chain.addSlotWithId (4, "Room", "AudioUnit", "au:room");
    auto* slot = preset.chain.getSlotById (17);
    const unsigned char bytes[] { 0, 1, 255, 0, 127 };
    slot->serializedState.append (bytes, sizeof (bytes));
    slot->bypassed = true;
    slot->available = false;
    slot->errorMessage = "Missing plug-in";
    return preset;
}

ListeningPreset makeListening()
{
    ListeningPreset preset;
    preset.name = "Surround room";
    preset.chain = makeChain().chain;
    preset.output.deviceIdentifier = "CoreAudio:room";
    preset.output.deviceName = "Room device";
    preset.output.layout = OutputLayout::fiveOne;
    preset.output.sampleRate = 96000.0;
    preset.output.bufferSize = 512;
    preset.output.routing = { 7, 2, -1, 3, 4, 5 };
    preset.output.channelMix = { { juce::AudioChannelSet::left, 0.7f, true, false } };
    preset.output.calibration = { { juce::AudioChannelSet::centre, -3.5f, 25.25, true } };
    return preset;
}

class ListeningPresetsTests final : public juce::UnitTest
{
public:
    ListeningPresetsTests() : UnitTest ("ListeningPresets", "state") {}
    void runTest() override
    {
        beginTest ("CRUD keeps IDs stable and duplicates own independent snapshots");
        {
            ListeningPresetLibrary library;
            auto original = makeChain();
            const auto id = original.id;
            expect (library.put (original).wasOk());
            expectEquals (library.findEffectChain (id)->name, juce::String ("Night chain"));
            expect (library.renameEffectChain (id, "  Late night  ").wasOk());
            expectEquals (library.findEffectChain (id)->name, juce::String ("Late night"));
            const auto count = library.getEffectChainPresets().size();
            expect (library.renameEffectChain (id, "  ").failed());
            juce::String copyId;
            expect (library.duplicateEffectChain (id, "Copy", copyId).wasOk());
            expect (copyId != id);
            auto replacement = *library.findEffectChain (id);
            replacement.chain.getSlotById (17)->serializedState.reset();
            expect (library.put (replacement).wasOk());
            expectEquals (library.findEffectChain (copyId)->chain.getSlotById (17)->serializedState.getSize(), size_t (5));
            expectEquals (library.getEffectChainPresets().size(), count + 1);
            auto room = makeListening();
            expect (library.put (room).wasOk());
            expect (library.duplicateListening (room.id, "Room copy", copyId).wasOk());
            expect (library.removeListening (room.id).wasOk());
            expect (library.findListening (copyId) != nullptr);
            expect (library.removeEffectChain (id).wasOk());
            expect (library.findListening (copyId)->chain.size() == 2);
            expect (library.removeEffectChain (id).failed());
        }

        beginTest ("invalid snapshots are rejected without mutating the library");
        {
            ListeningPresetLibrary library;
            auto room = makeListening();
            expect (library.put (room).wasOk());
            for (const auto value : { std::numeric_limits<double>::quiet_NaN(),
                                      std::numeric_limits<double>::infinity(), -1.0, 201.0 })
            {
                auto invalid = room;
                invalid.output.calibration[0].delayMs = value;
                expect (library.put (invalid).failed());
            }
            auto invalid = room;
            invalid.output.calibration[0].trimDb = std::numeric_limits<float>::infinity();
            expect (library.put (invalid).failed());
            invalid = room; invalid.output.routing[0] = 2;
            expect (library.put (invalid).failed());
            invalid = room; invalid.output.layout = static_cast<OutputLayout> (1234);
            expect (library.put (invalid).failed());
            invalid = room; invalid.chain.getSlotById (17)->scope = { juce::AudioChannelSet::left, juce::AudioChannelSet::left };
            expect (library.put (invalid).failed());
            expectWithinAbsoluteError (library.findListening (room.id)->output.calibration[0].delayMs, 25.25, 0.0001);
        }

        const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile ("build")
            .getNonexistentChildFile ("moonwake-presets-tests", {});
        expect (directory.createDirectory());
        const auto file = directory.getChildFile ("presets.xml");
        juce::String error;
        beginTest ("library and single-file exports preserve full chain and output snapshots");
        {
            ListeningPresetLibrary library;
            auto room = makeListening();
            auto chain = makeChain();
            expect (library.put (room).wasOk());
            expect (library.put (chain).wasOk());
            expect (ListeningPresetStore::writeAtomically (library, file, error), error);
            ListeningPresetLibrary restored;
            expect (ListeningPresetStore::readFromFile (file, restored, error), error);
            expect (error.isEmpty());
            expectEquals (restored.findEffectChain (chain.id)->chain.getSlot (0)->id, 17);
            expectEquals (restored.findEffectChain (chain.id)->chain.getSlot (1)->id, 4);
            expect (restored.findEffectChain (chain.id)->chain.getSlot (0)->scope == chain.chain.getSlot (0)->scope);
            expect (restored.findEffectChain (chain.id)->chain.getSlot (0)->serializedState == chain.chain.getSlot (0)->serializedState);
            expect (restored.findEffectChain (chain.id)->chain.getSlot (0)->bypassed);
            expect (! restored.findEffectChain (chain.id)->chain.getSlot (0)->available);
            expect (restored.findListening (room.id)->output.calibration == room.output.calibration);
            expect (restored.findListening (room.id)->output.channelMix == room.output.channelMix);
            expect (restored.findListening (room.id)->output.routing == room.output.routing);
            expectEquals (restored.findListening (room.id)->output.deviceIdentifier, juce::String ("CoreAudio:room"));
            for (const bool listening : { false, true })
            {
                const auto exported = directory.getChildFile (listening ? "room.moonwake-preset" : "chain.moonwake-preset");
                expect (listening ? ListeningPresetStore::exportPreset (room, exported, error)
                                  : ListeningPresetStore::exportPreset (chain, exported, error), error);
                ImportedListeningPreset imported;
                expect (ListeningPresetStore::importPreset (exported, imported, error), error);
                expectEquals (imported.index(), listening ? size_t (1) : size_t (0));
                std::visit ([&] (const auto& value)
                {
                    expectEquals (value.chain.getSlot (0)->pluginFormat, juce::String ("VST3"));
                    expectEquals (value.chain.getSlot (1)->pluginIdentifier, juce::String ("au:room"));
                    expect (value.chain.getSlot (0)->serializedState == chain.chain.getSlot (0)->serializedState);
                }, imported);
            }
        }

        beginTest ("backup recovery preserves the last valid snapshot and reports recovery");
        {
            ListeningPresetLibrary library;
            auto chain = makeChain();
            expect (library.put (chain).wasOk());
            expect (ListeningPresetStore::writeAtomically (library, file, error), error);
            expect (library.renameEffectChain (chain.id, "New name").wasOk());
            expect (ListeningPresetStore::writeAtomically (library, file, error), error);
            expect (file.replaceWithText ("broken"));
            ListeningPresetLibrary restored;
            expect (ListeningPresetStore::readFromFile (file, restored, error));
            expect (error.isNotEmpty());
            expectEquals (restored.findEffectChain (chain.id)->name, juce::String ("Night chain"));
            expect (ListeningPresetStore::writeAtomically (restored, file, error), error);
            expect (ListeningPresetStore::readFromFile (file, restored, error));
            expect (error.isEmpty());
            expect (file.replaceWithText ("broken"));
            expect (ListeningPresetStore::backupFileFor (file).replaceWithText ("also broken"));
            expect (! ListeningPresetStore::readFromFile (file, restored, error));
            expect (error.isNotEmpty());
            expect (restored.findEffectChain (chain.id) != nullptr);
            expect (! ListeningPresetStore::writeAtomically (restored, file, error));
            expectEquals (file.loadFileAsString(), juce::String ("broken"));
        }

        beginTest ("a newer library is preserved even when an older backup exists");
        {
            ListeningPresetLibrary library;
            expect (library.put (makeChain()).wasOk());
            const auto newer = directory.getChildFile ("newer.xml");
            expect (ListeningPresetStore::writeAtomically (library, newer, error), error);
            expect (ListeningPresetStore::writeAtomically (library, newer, error), error);
            auto xml = juce::XmlDocument::parse (newer);
            expect (xml != nullptr);
            if (xml != nullptr)
            {
                xml->setAttribute ("schemaVersion", 2);
                expect (xml->writeTo (newer));
                const auto original = newer.loadFileAsString();
                expect (! ListeningPresetStore::readFromFile (newer, library, error));
                expect (! ListeningPresetStore::writeAtomically (library, newer, error));
                expectEquals (newer.loadFileAsString(), original);
            }
        }

        beginTest ("a new library can create its first snapshot without an existing backup");
        {
            const auto fresh = directory.getChildFile ("fresh.xml");
            ListeningPresetLibrary library;
            expect (library.put (makeChain()).wasOk());
            expect (! fresh.exists());
            expect (! ListeningPresetStore::backupFileFor (fresh).exists());
            expect (ListeningPresetStore::writeAtomically (library, fresh, error), error);
            ListeningPresetLibrary restored;
            expect (ListeningPresetStore::readFromFile (fresh, restored, error), error);
            expectEquals (restored.getEffectChainPresets().size(), size_t (1));
        }

        beginTest ("a valid backup can recover a missing primary and remains intact on the next save");
        {
            const auto missing = directory.getChildFile ("missing-primary.xml");
            const auto backup = ListeningPresetStore::backupFileFor (missing);
            ListeningPresetLibrary library;
            const auto chain = makeChain();
            expect (library.put (chain).wasOk());
            expect (ListeningPresetStore::writeAtomically (library, missing, error), error);
            expect (missing.moveFileTo (backup));
            juce::MemoryBlock original;
            expect (backup.loadFileAsData (original));
            ListeningPresetLibrary restored;
            expect (ListeningPresetStore::readFromFile (missing, restored, error), error);
            expect (error.isNotEmpty());
            expect (restored.renameEffectChain (chain.id, "Recovered chain").wasOk());
            expect (ListeningPresetStore::writeAtomically (restored, missing, error), error);
            juce::MemoryBlock preserved;
            expect (backup.loadFileAsData (preserved));
            expect (preserved == original);
            expect (ListeningPresetStore::readFromFile (missing, restored, error), error);
            expectEquals (restored.findEffectChain (chain.id)->name, juce::String ("Recovered chain"));
        }

        beginTest ("an unreadable or newer sole backup blocks writes and preserves its original bytes");
        {
            const auto missing = directory.getChildFile ("protected-backup.xml");
            const auto backup = ListeningPresetStore::backupFileFor (missing);
            ListeningPresetLibrary library;
            expect (library.put (makeChain()).wasOk());
            for (const auto& content : {
                    juce::String ("broken preset backup"),
                    juce::String ("<MoonwakePresets schemaVersion=\"2\"><FutureData/></MoonwakePresets>") })
            {
                expect (missing.deleteFile());
                expect (backup.replaceWithText (content));
                juce::MemoryBlock original;
                expect (backup.loadFileAsData (original));
                ListeningPresetLibrary restored;
                expect (! ListeningPresetStore::readFromFile (missing, restored, error));
                expect (error.isNotEmpty());
                expect (! ListeningPresetStore::writeAtomically (library, missing, error));
                expect (error.isNotEmpty());
                expect (! missing.exists());
                juce::MemoryBlock preserved;
                expect (backup.loadFileAsData (preserved));
                expect (preserved == original);
            }
        }

        beginTest ("oversized and recursively structured imports fail before changing the caller");
        {
            const auto malformed = directory.getChildFile ("oversized.moonwake-preset");
            {
                auto stream = malformed.createOutputStream();
                expect (stream != nullptr);
                if (stream != nullptr)
                {
                    expect (stream->setPosition (ListeningPresetStore::maximumFileBytes));
                    expect (stream->writeByte ('x'));
                }
            }
            ImportedListeningPreset target = makeChain();
            const auto oldId = std::get<EffectChainPreset> (target).id;
            expect (! ListeningPresetStore::importPreset (malformed, target, error));
            expect (error.isNotEmpty());
            for (const auto& xml : {
                    juce::String ("<!DOCTYPE preset [<!ENTITY payload 'expanded'>]><MoonwakePreset/>") ,
                    juce::String::repeatedString ("<Node>", 128) + juce::String::repeatedString ("</Node>", 128) })
            {
                expect (malformed.replaceWithText (xml));
                expect (! ListeningPresetStore::importPreset (malformed, target, error));
                expect (error.isNotEmpty());
                expectEquals (std::get<EffectChainPreset> (target).id, oldId);
            }
        }

        beginTest ("writer and reader share structural budgets and rejected writes preserve both snapshots");
        {
            const auto budgetFile = directory.getChildFile ("budget.xml");
            ListeningPresetLibrary library;
            EffectChainPreset preset;
            preset.name = "Scoped rack";
            for (int slot = 0; slot < 128; ++slot)
                preset.chain.addSlot ("EQ", "fixture:eq", "VST3",
                                      { juce::AudioChannelSet::left, juce::AudioChannelSet::right });
            expect (library.put (preset).wasOk());
            expect (ListeningPresetStore::writeAtomically (library, budgetFile, error), error);
            for (int index = 1; index < 194; ++index)
            {
                preset.id = juce::Uuid().toString();
                expect (library.put (preset).wasOk());
            }
            expect (ListeningPresetStore::writeAtomically (library, budgetFile, error), error);
            ListeningPresetLibrary restored;
            expect (ListeningPresetStore::readFromFile (budgetFile, restored, error), error);
            expect (error.isEmpty());
            expectEquals (restored.getEffectChainPresets().size(), size_t (194));
            const auto primary = budgetFile.loadFileAsString();
            const auto backup = ListeningPresetStore::backupFileFor (budgetFile).loadFileAsString();
            preset.id = juce::Uuid().toString();
            expect (library.put (preset).wasOk());
            expect (! ListeningPresetStore::writeAtomically (library, budgetFile, error));
            expect (error.isNotEmpty());
            expect (budgetFile.loadFileAsString() == primary);
            expect (ListeningPresetStore::backupFileFor (budgetFile).loadFileAsString() == backup);
            expect (ListeningPresetStore::readFromFile (budgetFile, restored, error), error);
            expect (error.isEmpty());
            expectEquals (restored.getEffectChainPresets().size(), size_t (194));
        }

        beginTest ("malformed imports cannot replace the caller's preset");
        {
            const auto exported = directory.getChildFile ("room.moonwake-preset");
            auto xml = juce::XmlDocument::parse (exported);
            expect (xml != nullptr);
            if (xml != nullptr)
            {
                const auto good = juce::ValueTree::fromXml (*xml);
                for (int corruption = 0; corruption < 15; ++corruption)
                {
                    auto tree = good.createCopy();
                    auto preset = tree.getChild (0);
                    auto output = preset.getChildWithName ("Output");
                    auto chain = preset.getChildWithName ("EffectChain");
                    if (corruption == 0) tree.setProperty ("kind", "unknown", nullptr);
                    if (corruption == 1) tree.setProperty ("schemaVersion", 99, nullptr);
                    if (corruption == 2) chain.appendChild (chain.getChild (0).createCopy(), nullptr);
                    if (corruption == 3) chain.getChild (0).setProperty ("serializedState", "not-base64", nullptr);
                    if (corruption == 4) output.setProperty ("layout", "imaginary", nullptr);
                    if (corruption == 5) output.getChildWithName ("Calibration").getChild (0).setProperty ("delayMs", "nan", nullptr);
                    if (corruption == 6) output.getChildWithName ("Routing").getChild (0).setProperty ("port", 2, nullptr);
                    if (corruption == 7) preset.setProperty ("name", " ", nullptr);
                    if (corruption == 8) preset.setProperty ("extra", "unsupported", nullptr);
                    if (corruption == 9) output.setProperty ("bufferSize", "256junk", nullptr);
                    if (corruption == 10) chain.getChild (0).setProperty ("serializedState", "2147483647.A", nullptr);
                    if (corruption == 11) chain.getChild (0).setProperty ("pluginFormat", "Unknown", nullptr);
                    if (corruption == 12) chain.getChild (0).getChildWithName ("Scope").getChild (0).setProperty ("type", 0, nullptr);
                    if (corruption == 13) output.getChildWithName ("Calibration").getChild (0).setProperty ("inverted", "maybe", nullptr);
                    if (corruption == 14) output.getChildWithName ("Calibration").getChild (0).setProperty ("trimDb", "inf", nullptr);
                    const auto malformed = directory.getChildFile ("bad.moonwake-preset");
                    expect (tree.createXml()->writeTo (malformed));
                    ImportedListeningPreset target = makeChain();
                    const auto oldId = std::get<EffectChainPreset> (target).id;
                    expect (! ListeningPresetStore::importPreset (malformed, target, error), juce::String (corruption));
                    expect (error.isNotEmpty());
                    expect (std::holds_alternative<EffectChainPreset> (target));
                    if (std::holds_alternative<EffectChainPreset> (target))
                        expectEquals (std::get<EffectChainPreset> (target).id, oldId);
                }
            }
        }
        expect (deleteTestDirectory (directory), directory.getFullPathName());
    }
};
ListeningPresetsTests listeningPresetsTests;
}
