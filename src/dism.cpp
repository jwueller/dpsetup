#include <Windows.h>

#include <string>
#include <string_view>

#include "encoding.h"
#include "log.h"
#include "proc_address.h"
#include "unique_resource.h"

#include "steps.h"

namespace
{
// The Windows SDK has no dismapi.h (the ADK does), so the slice used here is
// declared from Microsoft's DISM API reference. Reached only through
// PROC_ADDRESS_OF, never linked.
#pragma pack(push, 1) // dismapi.h packs every structure
using DismSession = UINT;

enum DismLogLevel
{
    DismLogErrors = 0,
};

enum DismPackageIdentifier
{
    DismPackageNone = 0,
};

enum DismPackageFeatureState
{
    DismStateNotPresent = 0,
    DismStateUninstallPending = 1,
    DismStateStaged = 2,
    DismStateRemoved = 3,
    DismStateInstalled = 4,
    DismStateInstallPending = 5,
    DismStateSuperseded = 6,
    DismStatePartiallyInstalled = 7,
};

/// Allocated by DISM; only the leading members are declared because only
/// they are read.
struct DismFeatureInfo
{
    // Never read; declared to place FeatureState at its offset.
    // ReSharper disable once CppDeclaratorNeverUsed
    PCWSTR FeatureName;
    DismPackageFeatureState FeatureState;
};

struct DismString
{
    PCWSTR Value;
};
#pragma pack(pop)

using DISM_PROGRESS_CALLBACK = void(CALLBACK*)(
    UINT Current,
    UINT Total,
    PVOID UserData
);

// Declarations only, for PROC_ADDRESS_OF; dismapi.dll implements them.
// ReSharper disable CppFunctionIsNotImplemented
HRESULT WINAPI DismInitialize(
    DismLogLevel LogLevel,
    PCWSTR LogFilePath,
    PCWSTR ScratchDirectory
);

HRESULT WINAPI DismShutdown();

HRESULT WINAPI DismOpenSession(
    PCWSTR ImagePath,
    PCWSTR WindowsDirectory,
    PCWSTR SystemDrive,
    DismSession* Session
);

HRESULT WINAPI DismCloseSession(DismSession Session);

HRESULT WINAPI DismEnableFeature(
    DismSession Session,
    PCWSTR FeatureName,
    PCWSTR Identifier,
    DismPackageIdentifier PackageIdentifier,
    BOOL LimitAccess,
    PCWSTR* SourcePaths,
    UINT SourcePathCount,
    BOOL EnableAll,
    HANDLE CancelEvent,
    DISM_PROGRESS_CALLBACK Progress,
    PVOID UserData
);

HRESULT WINAPI DismGetFeatureInfo(
    DismSession Session,
    PCWSTR FeatureName,
    PCWSTR Identifier,
    DismPackageIdentifier PackageIdentifier,
    DismFeatureInfo** FeatureInfo
);

HRESULT WINAPI DismGetLastErrorMessage(DismString** ErrorMessage);

HRESULT WINAPI DismDelete(VOID* DismStructure);
// ReSharper restore CppFunctionIsNotImplemented

/// `DISM_ONLINE_IMAGE`: the running Windows installation.
inline constexpr auto kDismOnlineImage =
    L"DISM_{53BFAE52-B167-4E2F-A258-0A37B57FF845}";

constexpr LPCSTR NameFromFeatureState(
    const DismPackageFeatureState state
) noexcept
{
    switch (state)
    {
    case DismStateNotPresent:
        return "NotPresent";
    case DismStateUninstallPending:
        return "UninstallPending";
    case DismStateStaged:
        return "Staged";
    case DismStateRemoved:
        return "Removed";
    case DismStateInstalled:
        return "Installed";
    case DismStateInstallPending:
        return "InstallPending";
    case DismStateSuperseded:
        return "Superseded";
    case DismStatePartiallyInstalled:
        return "PartiallyInstalled";
    }
    // DISM may report a state newer than this declaration.
    return "?";
}
} // namespace

UniqueModule LoadDismApi()
{
    UniqueModule dismApi(
        LoadLibraryExW(L"dismapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)
    );
    if (dismApi)
    {
        LOG("LoadLibraryExW(\"dismapi.dll\") -> ok");
    }
    else
    {
        LOG("LoadLibraryExW(\"dismapi.dll\") -> error {}", GetLastError());
    }
    return dismApi;
}

bool EnableDirectPlayFeature(const HMODULE hDismApi)
{
    // @formatter:off
    const auto pfnDismInitialize = PROC_ADDRESS_OF(hDismApi, DismInitialize);
    const auto pfnDismShutdown = PROC_ADDRESS_OF(hDismApi, DismShutdown);
    const auto pfnDismOpenSession = PROC_ADDRESS_OF(hDismApi, DismOpenSession);
    const auto pfnDismCloseSession = PROC_ADDRESS_OF(hDismApi, DismCloseSession);
    const auto pfnDismEnableFeature = PROC_ADDRESS_OF(hDismApi, DismEnableFeature);
    const auto pfnDismGetFeatureInfo = PROC_ADDRESS_OF(hDismApi, DismGetFeatureInfo);
    const auto pfnDismGetLastErrorMessage = PROC_ADDRESS_OF(hDismApi, DismGetLastErrorMessage);
    const auto pfnDismDelete = PROC_ADDRESS_OF(hDismApi, DismDelete);
    // @formatter:on

    if (
        !pfnDismInitialize ||
        !pfnDismShutdown ||
        !pfnDismOpenSession ||
        !pfnDismCloseSession ||
        !pfnDismEnableFeature ||
        !pfnDismGetFeatureInfo ||
        !pfnDismGetLastErrorMessage ||
        !pfnDismDelete
    )
    {
        LOG("GetProcAddress -> dismapi.dll lacks a DISM API export");
        return false;
    }

    // DISM keeps the reason for a failure apart from its HRESULT.
    const auto logDismCall = [&](const LPCSTR symbol, const HRESULT hr)
    {
        if (SUCCEEDED(hr))
        {
            LOG(
                "{} -> HRESULT{{0x{:08X}}}",
                symbol,
                static_cast<unsigned>(hr)
            );
            return;
        }

        DismString* pMessage = nullptr;
        const HRESULT hrMessage = pfnDismGetLastErrorMessage(&pMessage);
        const bool hasMessage =
            SUCCEEDED(hrMessage) && pMessage && pMessage->Value;
        const std::wstring_view message = hasMessage ? pMessage->Value : L"";

        LOG(
            "{} -> failed HRESULT{{0x{:08X}}} {}",
            symbol,
            static_cast<unsigned>(hr),
            Utf8FromWide(message)
        );

        if (pMessage)
            (void)pfnDismDelete(pMessage);
    };

    HRESULT hr = pfnDismInitialize(DismLogErrors, nullptr, nullptr);
    logDismCall("DismInitialize", hr);
    if (FAILED(hr))
        return false;

    bool enabled = false;
    DismSession session = 0;
    hr = pfnDismOpenSession(kDismOnlineImage, nullptr, nullptr, &session);
    logDismCall("DismOpenSession", hr);
    if (SUCCEEDED(hr))
    {
        // Not the outcome: a success that wants a restart is not S_OK. It
        // came back as the bare ERROR_SUCCESS_REBOOT_REQUIRED, 0x00000BC2
        // (2026-11-12, Windows 11 26300); Microsoft documents the
        // HRESULT_FROM_WIN32 form, which is a failure code. The state read
        // back below decides. The API never restarts the machine itself.
        hr = pfnDismEnableFeature(
            session,
            L"DirectPlay",
            nullptr,
            DismPackageNone,
            FALSE /* LimitAccess */,
            nullptr,
            0,
            TRUE /* EnableAll */,
            nullptr,
            nullptr,
            nullptr
        );
        logDismCall("DismEnableFeature", hr);

        DismFeatureInfo* pFeatureInfo = nullptr;
        hr = pfnDismGetFeatureInfo(
            session,
            L"DirectPlay",
            nullptr,
            DismPackageNone,
            &pFeatureInfo
        );
        logDismCall("DismGetFeatureInfo", hr);
        if (SUCCEEDED(hr) && pFeatureInfo)
        {
            const DismPackageFeatureState state = pFeatureInfo->FeatureState;
            enabled = state == DismStateInstalled
                || state == DismStateInstallPending;
            const std::string_view featureState = NameFromFeatureState(state);
            LOG("DismGetFeatureInfo -> feature state {}", featureState);
            (void)pfnDismDelete(pFeatureInfo);
        }

        (void)pfnDismCloseSession(session);
    }
    (void)pfnDismShutdown();
    return enabled;
}
