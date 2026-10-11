#include "app/NowPlayingBridge.h"
#include "app/WindowsApplicationIdentity.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <systemmediatransportcontrolsinterop.h>
#include <shlobj.h>

#include <chrono>
#include <future>
#include <mutex>

namespace
{
using namespace winrt::Windows::Media;
using namespace winrt::Windows::Media::Control;
using namespace std::chrono_literals;

template <typename Operation>
auto awaitOperation (const Operation& operation)
{
    if (operation.wait_for (5s) == winrt::Windows::Foundation::AsyncStatus::Started)
    {
        operation.Cancel();
        throw std::runtime_error ("Windows media operation did not complete within five seconds");
    }
    return operation.GetResults();
}

// WinRT blocking waits are forbidden on the JUCE STA. Keep pumping its message
// loop while the MTA worker waits for the real Windows media session service.
template <typename Function>
auto onMta (Function function)
{
    auto future = std::async (std::launch::async, [function = std::move (function)]
    {
        winrt::init_apartment (winrt::apartment_type::multi_threaded);
        struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
        return function();
    });
    while (future.wait_for (0ms) != std::future_status::ready)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    return future.get();
}

class WindowsNowPlayingBridgeTests final : public juce::UnitTest
{
public:
    WindowsNowPlayingBridgeTests() : UnitTest ("Windows now playing bridge", "App") {}

    void runTest() override
    {
        beginTest ("unpackaged application identity resolves through the Windows shell");
        try
        {
            runNativeTests();
        }
        catch (const winrt::hresult_error& error)
        {
            expect (false, juce::String (error.message().c_str()));
        }
        catch (const std::exception& error)
        {
            expect (false, error.what());
        }
    }

private:
    void runNativeTests()
    {
        const auto appId = "com.samkuler.moonwake.tests." + juce::Uuid().toString();
        const auto appName = juce::String::fromUTF8 ("Moonwake 媒体身份测试");
        PWSTR programs = nullptr;
        winrt::check_hresult (SHGetKnownFolderPath (FOLDERID_Programs, KF_FLAG_DEFAULT, nullptr, &programs));
        const auto directory = juce::File (juce::String (programs)).getChildFile (appId);
        CoTaskMemFree (programs);
        const auto shortcut = directory.getChildFile (appName + ".lnk");
        struct RemoveShortcut
        {
            juce::File file;
            ~RemoveShortcut()
            {
                file.deleteFile();
                SHChangeNotify (SHCNE_DELETE, SHCNF_PATHW, file.getFullPathName().toWideCharPointer(), nullptr);
                file.getParentDirectory().deleteFile();
            }
        } removeShortcut { shortcut };
        const auto executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        const auto identity = WindowsApplicationIdentity::configure (appId, appName, executable, shortcut);
        expect (identity.wasOk(), identity.getErrorMessage());
        const auto firstWrite = shortcut.getLastModificationTime();
        expect (WindowsApplicationIdentity::configure (appId, appName, executable, shortcut).wasOk());
        expect (shortcut.getLastModificationTime() == firstWrite, "Unchanged identity must not rewrite its shortcut");
        PWSTR processId = nullptr;
        winrt::check_hresult (GetCurrentProcessExplicitAppUserModelID (&processId));
        expectEquals (juce::String (processId), appId);
        CoTaskMemFree (processId);
        const auto resolvedName = onMta ([appId]
        {
            const auto deadline = std::chrono::steady_clock::now() + 15s;
            do
            {
                winrt::com_ptr<IShellItem> item;
                if (SUCCEEDED (SHCreateItemInKnownFolder (FOLDERID_AppsFolder, KF_FLAG_DEFAULT,
                        appId.toWideCharPointer(), IID_PPV_ARGS (item.put()))))
                {
                    PWSTR name = nullptr;
                    winrt::check_hresult (item->GetDisplayName (SIGDN_NORMALDISPLAY, &name));
                    const juce::String value (name);
                    CoTaskMemFree (name);
                    return value;
                }
                std::this_thread::sleep_for (100ms);
            } while (std::chrono::steady_clock::now() < deadline);
            return juce::String{};
        });
        expectEquals (resolvedName, appName);

        beginTest ("publishes native metadata, availability, timeline and artwork");
        juce::Component host;
        host.setName ("Moonwake media integration test");
        host.setBounds (0, 0, 160, 80);
        host.addToDesktop (juce::ComponentPeer::windowHasTitleBar);
        const auto window = static_cast<HWND> (host.getPeer()->getNativeHandle());
        auto bridge = createSystemNowPlayingBridge();
        expectEquals (bridge->getTimelineRefreshIntervalSeconds(), 5.0);
        NowPlayingSnapshot snapshot;
        snapshot.trackId = juce::Uuid().toString();
        snapshot.title = juce::String::fromUTF8 ("月醒 · ") + snapshot.trackId;
        snapshot.artist = juce::String::fromUTF8 ("艺术家 / Sam Kuler");
        snapshot.album = juce::String::fromUTF8 ("森林与光");
        snapshot.hasTrack = snapshot.canPlay = snapshot.canSeek = true;
        snapshot.canGoNext = true;
        snapshot.durationSeconds = 180.0;
        snapshot.elapsedSeconds = 12.5;
        // Startup publication before the native window exists must be harmless.
        bridge->publish (snapshot);
        expect (bridge->getLastError().isEmpty());
        bridge->setNativeWindowHandle (window);

        const auto artwork = juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getNonexistentChildFile (juce::String::fromUTF8 ("Moonwake 封面"), ".png");
        struct RemoveArtwork { juce::File file; ~RemoveArtwork() { file.deleteFile(); } } cleanup { artwork };
        juce::Image image (juce::Image::RGB, 8, 8, true);
        {
            juce::FileOutputStream output (artwork);
            expect (output.openedOk());
            expect (juce::PNGImageFormat().writeImageToStream (image, output));
        }
        snapshot.artworkPath = artwork.getFullPathName();
        bridge->publish (snapshot);
        expect (bridge->getLastError().isEmpty(), bridge->getLastError());
        const auto interop = winrt::get_activation_factory<
            SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
        SystemMediaTransportControls controls { nullptr };
        winrt::check_hresult (interop->GetForWindow (window,
            winrt::guid_of<SystemMediaTransportControls>(), winrt::put_abi (controls)));
        expect (controls.IsEnabled());
        expect (controls.IsPlayEnabled() && controls.IsPauseEnabled());
        expect (controls.IsNextEnabled() && ! controls.IsPreviousEnabled());
        expect (! controls.IsStopEnabled() && ! controls.IsFastForwardEnabled());
        expect (controls.PlaybackStatus() == MediaPlaybackStatus::Paused);
        const auto updater = controls.DisplayUpdater();
        expect (updater.Type() == MediaPlaybackType::Music);
        expectEquals (juce::String (updater.MusicProperties().Title().c_str()), snapshot.title);
        expectEquals (juce::String (updater.MusicProperties().Artist().c_str()), snapshot.artist);
        expectEquals (juce::String (updater.MusicProperties().AlbumTitle().c_str()), snapshot.album);
        expect (updater.Thumbnail() != nullptr);
        const auto thumbnail = updater.Thumbnail();
        expect (onMta ([thumbnail] { return awaitOperation (thumbnail.OpenReadAsync()).Size() > 0; }));

        beginTest ("real system session sees the timeline and sends transport commands");
        const auto session = onMta ([title = snapshot.title]
        {
            const auto manager = awaitOperation (GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            do
            {
                for (const auto& candidate : manager.GetSessions())
                {
                    const auto properties = awaitOperation (candidate.TryGetMediaPropertiesAsync());
                    if (juce::String (properties.Title().c_str()) == title) return candidate;
                }
                std::this_thread::sleep_for (50ms);
            } while (std::chrono::steady_clock::now() < deadline);
            return GlobalSystemMediaTransportControlsSession { nullptr };
        });
        expect (session != nullptr, "The Windows media session service must expose the test session");
        if (session)
        {
            expectEquals (juce::String (session.SourceAppUserModelId().c_str()), appId);
            const auto timeline = session.GetTimelineProperties();
            expectEquals (timeline.EndTime().count(), int64_t { 1800000000 });
            expectEquals (timeline.Position().count(), int64_t { 125000000 });
            struct Recorder
            {
                std::mutex mutex;
                std::vector<RemoteTransportCommand> commands;
            };
            const auto recorder = std::make_shared<Recorder>();
            bridge->setCommandHandler ([recorder] (const RemoteTransportCommand& command)
            {
                const std::lock_guard lock (recorder->mutex);
                recorder->commands.push_back (command);
            });
            const auto request = [&] (auto operation, RemoteTransportCommandType expected, double position = 0.0)
            {
                {
                    const std::lock_guard lock (recorder->mutex);
                    recorder->commands.clear();
                }
                expect (onMta ([operation] { return awaitOperation (operation()); }));
                const auto deadline = std::chrono::steady_clock::now() + 2s;
                for (;;)
                {
                    {
                        const std::lock_guard lock (recorder->mutex);
                        if (! recorder->commands.empty()) break;
                    }
                    if (std::chrono::steady_clock::now() >= deadline) break;
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
                }
                const std::lock_guard lock (recorder->mutex);
                expectEquals (static_cast<int> (recorder->commands.size()), 1);
                if (! recorder->commands.empty())
                {
                    expect (recorder->commands.front().type == expected);
                    expectWithinAbsoluteError (recorder->commands.front().positionSeconds, position, 0.001);
                }
            };
            request ([session] { return session.TryPlayAsync(); }, RemoteTransportCommandType::play);
            snapshot.playing = true;
            snapshot.canGoPrevious = true;
            bridge->publish (snapshot);
            request ([session] { return session.TryPauseAsync(); }, RemoteTransportCommandType::pause);
            request ([session] { return session.TrySkipNextAsync(); }, RemoteTransportCommandType::next);
            request ([session] { return session.TrySkipPreviousAsync(); }, RemoteTransportCommandType::previous);
            request ([session] { return session.TryChangePlaybackPositionAsync (420000000); },
                     RemoteTransportCommandType::seekToSeconds, 42.0);
            bridge->clear();
            {
                const std::lock_guard lock (recorder->mutex);
                recorder->commands.clear();
            }
            onMta ([session] { return awaitOperation (session.TryPlayAsync()); });
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            {
                const std::lock_guard lock (recorder->mutex);
                expect (recorder->commands.empty(), "Cleared sessions must ignore pending system commands");
            }
            bridge->setCommandHandler ({});
        }

        beginTest ("clears stale artwork, disables empty queues and supports deferred commands");
        snapshot.artworkPath.clear();
        snapshot.title = "Without artwork";
        bridge->publish (snapshot);
        expect (controls.DisplayUpdater().Thumbnail() == nullptr);
        expect (controls.PlaybackStatus() == MediaPlaybackStatus::Playing);
        snapshot.artworkPath = artwork.getSiblingFile ("missing-artwork.png").getFullPathName();
        bridge->publish (snapshot);
        expect (bridge->getLastError().isNotEmpty());
        expect (controls.IsEnabled() && controls.IsPlayEnabled());
        expect (controls.DisplayUpdater().Thumbnail() == nullptr);
        snapshot.artworkPath.clear();
        bridge->publish (snapshot);
        expect (bridge->getLastError().isEmpty());
        bridge->publish ({});
        expect (! controls.IsEnabled());
        expect (! controls.IsPlayEnabled() && ! controls.IsNextEnabled());
        expect (controls.DisplayUpdater().Type() == MediaPlaybackType::Unknown);
        NowPlayingSnapshot deferred;
        deferred.canPlay = deferred.canGoNext = deferred.canGoPrevious = true;
        bridge->publish (deferred);
        expect (controls.IsEnabled() && controls.IsPlayEnabled() && controls.IsPreviousEnabled());

        beginTest ("detaches old windows, republishes and cleans up on destruction");
        bridge->setNativeWindowHandle (nullptr);
        expect (! controls.IsEnabled());
        bridge->setNativeWindowHandle (window);
        bridge->publish (snapshot);
        expect (controls.IsEnabled());
        bridge->clear();
        expect (! controls.IsEnabled());
        bridge->publish (snapshot);
        expect (controls.IsEnabled());
        juce::Component replacement;
        replacement.addToDesktop (juce::ComponentPeer::windowHasTitleBar);
        const auto replacementWindow = static_cast<HWND> (replacement.getPeer()->getNativeHandle());
        bridge->setNativeWindowHandle (replacementWindow);
        bridge->publish (snapshot);
        expect (! controls.IsEnabled());
        SystemMediaTransportControls replacementControls { nullptr };
        winrt::check_hresult (interop->GetForWindow (replacementWindow,
            winrt::guid_of<SystemMediaTransportControls>(), winrt::put_abi (replacementControls)));
        expect (replacementControls.IsEnabled());
        bridge.reset();
        expect (! replacementControls.IsEnabled());
        expect (replacementControls.DisplayUpdater().Type() == MediaPlaybackType::Unknown);
    }
};

WindowsNowPlayingBridgeTests windowsNowPlayingBridgeTests;
} // namespace
