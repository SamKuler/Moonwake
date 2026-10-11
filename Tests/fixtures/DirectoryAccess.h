#pragma once

#include "platform/FilesystemPath.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <aclapi.h>
#endif

// Restrict only a fixture-owned directory, and restore its original permissions
// even if a test exits early. POSIX mode bits do not deny directory listing on Windows.
class ScopedTestDirectoryAccess final
{
public:
    explicit ScopedTestDirectoryAccess (const juce::File& directory)
        : path (nativeFilesystem::toNativePath (directory)) {}
    ~ScopedTestDirectoryAccess()
    {
        static_cast<void> (restore());
       #if JUCE_WINDOWS
        if (descriptor != nullptr) LocalFree (descriptor);
       #endif
    }

    bool denyListing()
    {
       #if JUCE_WINDOWS
        if (GetNamedSecurityInfoW (path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                   nullptr, nullptr, &originalAcl, nullptr, &descriptor) != ERROR_SUCCESS)
            return false;
        BYTE everyone[SECURITY_MAX_SID_SIZE] {};
        DWORD size = sizeof (everyone);
        if (! CreateWellKnownSid (WinWorldSid, nullptr, everyone, &size)) return false;
        EXPLICIT_ACCESSW entry {};
        entry.grfAccessPermissions = FILE_LIST_DIRECTORY;
        entry.grfAccessMode = DENY_ACCESS;
        entry.grfInheritance = NO_INHERITANCE;
        entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entry.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        entry.Trustee.ptstrName = reinterpret_cast<LPWSTR> (everyone);
        PACL deniedAcl = nullptr;
        if (SetEntriesInAclW (1, &entry, originalAcl, &deniedAcl) != ERROR_SUCCESS) return false;
        changed = SetNamedSecurityInfoW (const_cast<LPWSTR> (path.c_str()), SE_FILE_OBJECT,
                                         DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                         deniedAcl, nullptr) == ERROR_SUCCESS;
        LocalFree (deniedAcl);
       #else
        std::error_code error;
        originalPermissions = std::filesystem::status (path, error).permissions();
        if (error) return false;
        std::filesystem::permissions (path, std::filesystem::perms::none, error);
        changed = ! error;
       #endif
        return changed;
    }

    bool restore()
    {
        if (! changed) return true;
       #if JUCE_WINDOWS
        const auto ok = SetNamedSecurityInfoW (const_cast<LPWSTR> (path.c_str()), SE_FILE_OBJECT,
                                               DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                               originalAcl, nullptr) == ERROR_SUCCESS;
       #else
        std::error_code error;
        std::filesystem::permissions (path, originalPermissions, error);
        const auto ok = ! error;
       #endif
        if (ok) changed = false;
        return ok;
    }

private:
    const std::filesystem::path path;
    bool changed = false;
   #if JUCE_WINDOWS
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL originalAcl = nullptr;
   #else
    std::filesystem::perms originalPermissions {};
   #endif
    JUCE_DECLARE_NON_COPYABLE (ScopedTestDirectoryAccess)
};
