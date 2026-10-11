#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>

// Synthetic peer events do not count as native input. Reacquire the test
// window's focus even after a long-running suite has lost the foreground.
inline void activateNativeTestWindow (juce::Component& window)
{
    auto* peer = window.getPeer();
    if (peer == nullptr || peer->getNativeHandle() == nullptr) return;
    const auto handle = static_cast<HWND> (peer->getNativeHandle());

    struct ScopedInputAttachment
    {
        ScopedInputAttachment()
            : current (GetCurrentThreadId()),
              foreground (GetWindowThreadProcessId (GetForegroundWindow(), nullptr)),
              attached (foreground != 0 && current != foreground
                        && AttachThreadInput (current, foreground, TRUE) != FALSE) {}
        ~ScopedInputAttachment()
        {
            if (attached) AttachThreadInput (current, foreground, FALSE);
        }
        DWORD current, foreground;
        bool attached;
    } attachment;

    window.toFront (true);
    SetForegroundWindow (handle);
    SetActiveWindow (handle);
    SetFocus (handle);
}
#endif
