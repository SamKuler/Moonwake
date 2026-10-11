#include "WindowsApplicationIdentity.h"
#include "ProductIdentity.h"

#include <winrt/base.h>
#include <shlobj.h>
#include <propkey.h>
#include <propvarutil.h>

#include <array>

namespace
{
struct ComApartment
{
    const HRESULT result = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
    ~ComApartment() { if (SUCCEEDED (result)) CoUninitialize(); }
};

struct PropertyValue
{
    PROPVARIANT value {};
    ~PropertyValue() { PropVariantClear (&value); }
};

juce::String readAppId (const winrt::com_ptr<IShellLinkW>& link)
{
    PropertyValue id;
    winrt::check_hresult (link.as<IPropertyStore>()->GetValue (PKEY_AppUserModel_ID, &id.value));
    return id.value.vt == VT_LPWSTR ? juce::String (id.value.pwszVal) : juce::String{};
}

bool matches (const winrt::com_ptr<IShellLinkW>& link, const juce::String& displayName,
              const juce::File& executable)
{
    std::array<wchar_t, 32768> text {};
    winrt::check_hresult (link->GetPath (text.data(), static_cast<int> (text.size()), nullptr, SLGP_RAWPATH));
    if (juce::String (text.data()) != executable.getFullPathName()) return false;
    winrt::check_hresult (link->GetWorkingDirectory (text.data(), static_cast<int> (text.size())));
    if (juce::String (text.data()) != executable.getParentDirectory().getFullPathName()) return false;
    winrt::check_hresult (link->GetDescription (text.data(), static_cast<int> (text.size())));
    if (juce::String (text.data()) != displayName) return false;
    int iconIndex = 0;
    winrt::check_hresult (link->GetIconLocation (text.data(), static_cast<int> (text.size()), &iconIndex));
    return juce::String (text.data()) == executable.getFullPathName() && iconIndex == 0;
}

juce::Result failure (const winrt::hresult_error& error)
{
    return juce::Result::fail ("Windows application identity unavailable: "
        + juce::String (error.message().c_str()) + " (0x"
        + juce::String::toHexString (static_cast<uint32_t> (error.code().value)) + ")");
}
}

juce::Result WindowsApplicationIdentity::configure (
    const juce::String& appId, const juce::String& displayName,
    const juce::File& executable, const juce::File& shortcut)
{
    try
    {
        // SMTC identifies the owning process, independently of song metadata.
        winrt::check_hresult (SetCurrentProcessExplicitAppUserModelID (appId.toWideCharPointer()));
        const ComApartment apartment;
        if (apartment.result != RPC_E_CHANGED_MODE) winrt::check_hresult (apartment.result);
        auto link = winrt::create_instance<IShellLinkW> (CLSID_ShellLink);
        const auto persist = link.as<IPersistFile>();
        const bool existed = shortcut.existsAsFile();
        if (existed)
        {
            winrt::check_hresult (persist->Load (shortcut.getFullPathName().toWideCharPointer(), STGM_READ));
            // Never replace a shortcut belonging to another application.
            if (readAppId (link) != appId)
                return juce::Result::fail ("Windows application shortcut already belongs to another identity: "
                                           + shortcut.getFullPathName());
            if (matches (link, displayName, executable)) return juce::Result::ok();
        }

        const auto created = shortcut.getParentDirectory().createDirectory();
        if (created.failed()) return created;
        winrt::check_hresult (link->SetPath (executable.getFullPathName().toWideCharPointer()));
        winrt::check_hresult (link->SetWorkingDirectory (executable.getParentDirectory().getFullPathName().toWideCharPointer()));
        winrt::check_hresult (link->SetDescription (displayName.toWideCharPointer()));
        winrt::check_hresult (link->SetIconLocation (executable.getFullPathName().toWideCharPointer(), 0));
        const auto properties = link.as<IPropertyStore>();
        PropertyValue id;
        winrt::check_hresult (InitPropVariantFromString (appId.toWideCharPointer(), &id.value));
        winrt::check_hresult (properties->SetValue (PKEY_AppUserModel_ID, id.value));
        winrt::check_hresult (properties->Commit());

        // An indexed Start Menu entry gives an unpackaged build an application
        // name and icon even when its executable is launched directly.
        const juce::TemporaryFile temporary (shortcut);
        winrt::check_hresult (persist->Save (temporary.getFile().getFullPathName().toWideCharPointer(), TRUE));
        if (! MoveFileExW (temporary.getFile().getFullPathName().toWideCharPointer(),
                           shortcut.getFullPathName().toWideCharPointer(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            winrt::throw_last_error();
        SHChangeNotify (existed ? SHCNE_UPDATEITEM : SHCNE_CREATE, SHCNF_PATHW,
                        shortcut.getFullPathName().toWideCharPointer(), nullptr);
        return juce::Result::ok();
    }
    catch (const winrt::hresult_error& error)
    {
        return failure (error);
    }
}

juce::Result WindowsApplicationIdentity::initialise()
{
    PWSTR programs = nullptr;
    const auto result = SHGetKnownFolderPath (FOLDERID_Programs, KF_FLAG_DEFAULT, nullptr, &programs);
    const juce::String directory (programs);
    CoTaskMemFree (programs);
    if (FAILED (result)) return failure (winrt::hresult_error (result));
    return configure (MoonwakeIdentity::appBundleId, MoonwakeIdentity::productName,
                      juce::File::getSpecialLocation (juce::File::currentExecutableFile),
                      juce::File (directory).getChildFile ("Moonwake/Moonwake.lnk"));
}
