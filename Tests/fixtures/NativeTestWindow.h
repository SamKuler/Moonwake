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

#if JUCE_MAC
inline void activateNativeTestWindow (juce::Component& window)
{
    juce::Process::makeForegroundProcess();
    window.toFront (true);
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1500.0;
    while ((! juce::Process::isForegroundProcess() || ! window.getPeer()->isFocused())
           && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
}
#endif

// Synthetic focus changes require a foreground native peer. Restore the actual
// focus reached by the input only when the OS interrupts that precondition.
inline bool dispatchNativeFocusTestEvents (juce::Component& window, int milliseconds)
{
   #if JUCE_MAC || JUCE_WINDOWS
    const juce::Component::SafePointer<juce::Component> focused (juce::Component::getCurrentlyFocusedComponent());
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1500.0;
   #endif
    juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
   #if JUCE_MAC || JUCE_WINDOWS
    while (! juce::Process::isForegroundProcess() || ! window.getPeer()->isFocused())
    {
        if (juce::Time::getMillisecondCounterHiRes() >= deadline) return false;
        activateNativeTestWindow (window);
        if (! juce::Process::isForegroundProcess() || ! window.getPeer()->isFocused()) return false;
        if (focused != nullptr) focused->grabKeyboardFocus();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    }
   #else
    juce::ignoreUnused (window);
   #endif
    return true;
}
