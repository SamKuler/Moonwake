#include "NowPlayingBridge.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>
#include <systemmediatransportcontrolsinterop.h>
#include <shcore.h>
#include <roapi.h>

#include <cmath>
#include <mutex>
#include <optional>
#include <utility>

namespace
{
using namespace winrt::Windows::Media;
using namespace winrt::Windows::Storage::Streams;
using winrt::Windows::Foundation::TimeSpan;

TimeSpan mediaTime (double seconds)
{
    // Avoid undefined floating-point to integer conversions for malformed metadata.
    constexpr double maximumSeconds = 1.0e11;
    const auto bounded = std::isfinite (seconds)
        ? juce::jlimit (0.0, maximumSeconds, seconds) : 0.0;
    return TimeSpan { static_cast<int64_t> (bounded * 10000000.0) };
}

class CommandState
{
public:
    void setHandler (NowPlayingBridge::CommandHandler value)
    {
        const std::lock_guard lock (mutex);
        handler = std::move (value);
    }

    void setSnapshot (NowPlayingSnapshot value)
    {
        const std::lock_guard lock (mutex);
        snapshot = std::move (value);
    }

    void dispatch (RemoteTransportCommand command)
    {
        NowPlayingBridge::CommandHandler callback;
        {
            const std::lock_guard lock (mutex);
            switch (command.type)
            {
                case RemoteTransportCommandType::play:
                case RemoteTransportCommandType::pause:
                case RemoteTransportCommandType::togglePlayPause:
                    if (! snapshot.canPlay) return;
                    break;
                case RemoteTransportCommandType::next:
                    if (! snapshot.canGoNext) return;
                    break;
                case RemoteTransportCommandType::previous:
                    if (! snapshot.canGoPrevious) return;
                    break;
                case RemoteTransportCommandType::seekToSeconds:
                    if (! snapshot.canSeek || ! std::isfinite (command.positionSeconds)) return;
                    command.positionSeconds = juce::jlimit (
                        0.0, juce::jmax (0.0, snapshot.durationSeconds), command.positionSeconds);
                    break;
            }
            callback = handler;
        }
        // SMTC calls from a worker thread. The application handler posts to JUCE's
        // message thread and checks its lifetime before touching the player.
        if (callback) callback (command);
    }

private:
    std::mutex mutex;
    NowPlayingSnapshot snapshot;
    NowPlayingBridge::CommandHandler handler;
};

class WindowsNowPlayingBridge final : public NowPlayingBridge
{
public:
    WindowsNowPlayingBridge() : apartmentResult (RoInitialize (RO_INIT_SINGLETHREADED)) {}

    ~WindowsNowPlayingBridge() override
    {
        setCommandHandler ({});
        disconnect();
        if (SUCCEEDED (apartmentResult)) RoUninitialize();
    }

    void setCommandHandler (CommandHandler value) override
    {
        handler = std::move (value);
        if (commandState) commandState->setHandler (handler);
    }

    void setNativeWindowHandle (void* handle) override
    {
        const auto nextWindow = static_cast<HWND> (handle);
        if (window == nextWindow) return;
        disconnect();
        window = nextWindow;
        lastError.clear();
    }

    juce::String getLastError() const override { return lastError; }

    // Windows SMTC needs a timeline refresh at least every five seconds.
    double getTimelineRefreshIntervalSeconds() const noexcept override { return 5.0; }

    void publish (const NowPlayingSnapshot& snapshot) override
    {
        // MainComponent is constructed before it is attached to a native peer.
        if (window == nullptr) return;
        try
        {
            connect();
            lastError.clear();
            if (snapshot.hasTrack)
            {
                if (! lastPresentation || presentationChanged (*lastPresentation, snapshot))
                    publishMetadata (snapshot);
                lastPresentation = snapshot;
            }
            else
            {
                clearMetadata();
            }

            controls.IsPlayEnabled (snapshot.canPlay);
            controls.IsPauseEnabled (snapshot.canPlay);
            controls.IsNextEnabled (snapshot.canGoNext);
            controls.IsPreviousEnabled (snapshot.canGoPrevious);
            controls.PlaybackRate (1.0);

            const auto end = snapshot.hasTrack ? mediaTime (snapshot.durationSeconds) : TimeSpan{};
            const auto position = snapshot.hasTrack
                ? juce::jmin (mediaTime (snapshot.elapsedSeconds), end) : TimeSpan{};
            SystemMediaTransportControlsTimelineProperties timeline;
            timeline.StartTime (TimeSpan{});
            timeline.EndTime (end);
            timeline.Position (position);
            timeline.MinSeekTime (snapshot.canSeek ? TimeSpan{} : position);
            timeline.MaxSeekTime (snapshot.canSeek ? end : position);
            controls.UpdateTimelineProperties (timeline);
            controls.PlaybackStatus (snapshot.hasTrack
                ? (snapshot.playing ? MediaPlaybackStatus::Playing : MediaPlaybackStatus::Paused)
                : MediaPlaybackStatus::Stopped);
            commandState->setSnapshot (snapshot);
            // Restore-time commands can be queued even before a track is available.
            controls.IsEnabled (snapshot.hasTrack || snapshot.canPlay
                                || snapshot.canGoNext || snapshot.canGoPrevious);
        }
        catch (const winrt::hresult_error& error)
        {
            disconnect();
            reportError (error);
        }
    }

    void clear() override
    {
        if (commandState) commandState->setSnapshot ({});
        if (! controls) return;
        try
        {
            controls.IsEnabled (false);
            controls.IsPlayEnabled (false);
            controls.IsPauseEnabled (false);
            controls.IsNextEnabled (false);
            controls.IsPreviousEnabled (false);
            controls.PlaybackStatus (MediaPlaybackStatus::Closed);
            controls.UpdateTimelineProperties (SystemMediaTransportControlsTimelineProperties{});
            clearMetadata();
        }
        catch (const winrt::hresult_error& error)
        {
            reportError (error);
        }
    }

private:
    const HRESULT apartmentResult;
    HWND window = nullptr;
    SystemMediaTransportControls controls { nullptr };
    SystemMediaTransportControls::ButtonPressed_revoker buttonRevoker;
    SystemMediaTransportControls::PlaybackPositionChangeRequested_revoker positionRevoker;
    std::shared_ptr<CommandState> commandState;
    CommandHandler handler;
    std::optional<NowPlayingSnapshot> lastPresentation;
    juce::String lastError;

    void connect()
    {
        if (controls) return;
        // JUCE may already have initialised COM in a different apartment.
        if (apartmentResult != RPC_E_CHANGED_MODE) winrt::check_hresult (apartmentResult);
        const auto interop = winrt::get_activation_factory<
            SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
        winrt::check_hresult (interop->GetForWindow (window,
            winrt::guid_of<SystemMediaTransportControls>(), winrt::put_abi (controls)));
        controls.IsEnabled (false);
        controls.IsStopEnabled (false);
        controls.IsFastForwardEnabled (false);
        controls.IsRewindEnabled (false);
        controls.IsRecordEnabled (false);
        controls.IsChannelUpEnabled (false);
        controls.IsChannelDownEnabled (false);
        commandState = std::make_shared<CommandState>();
        commandState->setHandler (handler);
        const std::weak_ptr<CommandState> weakState = commandState;
        buttonRevoker = controls.ButtonPressed (winrt::auto_revoke,
            [weakState] (const auto&, const SystemMediaTransportControlsButtonPressedEventArgs& args)
            {
                if (auto state = weakState.lock())
                {
                    switch (args.Button())
                    {
                        case SystemMediaTransportControlsButton::Play:
                            state->dispatch ({ RemoteTransportCommandType::play }); break;
                        case SystemMediaTransportControlsButton::Pause:
                            state->dispatch ({ RemoteTransportCommandType::pause }); break;
                        case SystemMediaTransportControlsButton::Next:
                            state->dispatch ({ RemoteTransportCommandType::next }); break;
                        case SystemMediaTransportControlsButton::Previous:
                            state->dispatch ({ RemoteTransportCommandType::previous }); break;
                        default: break;
                    }
                }
            });
        positionRevoker = controls.PlaybackPositionChangeRequested (winrt::auto_revoke,
            [weakState] (const auto&, const PlaybackPositionChangeRequestedEventArgs& args)
            {
                if (auto state = weakState.lock())
                    state->dispatch ({ RemoteTransportCommandType::seekToSeconds,
                        static_cast<double> (args.RequestedPlaybackPosition().count()) / 10000000.0 });
            });
    }

    void disconnect()
    {
        // A new state is created for each window. Late callbacks from the old
        // window cannot control the new session or outlive the application.
        if (commandState) commandState->setHandler ({});
        buttonRevoker.revoke();
        positionRevoker.revoke();
        clear();
        commandState.reset();
        controls = nullptr;
        lastPresentation.reset();
    }

    void clearMetadata()
    {
        const auto updater = controls.DisplayUpdater();
        updater.ClearAll();
        updater.Update();
        lastPresentation.reset();
    }

    static bool presentationChanged (const NowPlayingSnapshot& a, const NowPlayingSnapshot& b)
    {
        return a.trackId != b.trackId || a.title != b.title || a.artist != b.artist
            || a.album != b.album || a.artworkPath != b.artworkPath;
    }

    void publishMetadata (const NowPlayingSnapshot& snapshot)
    {
        const auto updater = controls.DisplayUpdater();
        updater.ClearAll();
        updater.Type (MediaPlaybackType::Music);
        updater.AppMediaId (snapshot.trackId.toWideCharPointer());
        const auto music = updater.MusicProperties();
        music.Title (snapshot.title.toWideCharPointer());
        music.Artist (snapshot.artist.toWideCharPointer());
        music.AlbumTitle (snapshot.album.toWideCharPointer());
        if (snapshot.artworkPath.isNotEmpty())
        {
            // Local file URIs are not supported by CreateFromUri. Use a native
            // stream so Unicode paths work without an async wait on the UI thread.
            try
            {
                IRandomAccessStream stream { nullptr };
                winrt::check_hresult (CreateRandomAccessStreamOnFile (
                    snapshot.artworkPath.toWideCharPointer(), 0 /* FileAccessMode::Read */,
                    winrt::guid_of<IRandomAccessStream>(), winrt::put_abi (stream)));
                updater.Thumbnail (RandomAccessStreamReference::CreateFromStream (stream));
            }
            catch (const winrt::hresult_error& error)
            {
                // A missing cache file must not disable transport or leave old art.
                reportError (error, "System media artwork unavailable: ");
            }
        }
        updater.Update();
    }

    void reportError (const winrt::hresult_error& error,
                      const juce::String& prefix = "Windows media controls unavailable: ")
    {
        const auto message = prefix + juce::String (error.message().c_str())
            + " (0x" + juce::String::toHexString (static_cast<uint32_t> (error.code().value)) + ")";
        if (message != lastError) juce::Logger::writeToLog (message);
        lastError = message;
    }
};
} // namespace

std::unique_ptr<NowPlayingBridge> createSystemNowPlayingBridge()
{
    return std::make_unique<WindowsNowPlayingBridge>();
}
