#pragma once

/// @file
/// Declarations shared across dpsetup's per-concern translation units.

#include <Windows.h>

#include <cstdint>
#include <string>

#include "unique_resource.h"

/**
 * @brief Outcome of an extraction attempt, distinguishing an environment
 *  failure from a bad cabinet.
 *
 * @note @ref Failed covers what a later run may get past (a missing cab, a
 *  folder that cannot be written, an FDI error, ...). @ref Fatal is reserved
 *  for the integrity check rejecting the cab itself
 *  (`VerifyDxntCabIntegrity`): corruption or tampering in the shipped
 *  files, which no retry fixes and the log names as such.
 */
enum class ExtractResult : std::uint8_t
{
    Ok,
    Failed,
    Fatal,
};

/// Verifies `dxnt.cab` next to this exe, then extracts `kDxntCabPayload`
/// from it into @p destDir.
[[nodiscard]] ExtractResult ExtractDirectPlayFiles(const std::wstring& exeDir, const std::wstring& destDir);

/**
 * @brief The first Win32 error the last @ref ExtractDirectPlayFiles hit
 *  creating the destination directory or a file in it, or `ERROR_SUCCESS`.
 *  Lets a failed extraction say "access denied" rather than a bare failure.
 */
[[nodiscard]] DWORD FirstExtractionError() noexcept;

/// Whether @p destDir holds every file of `kDxntCabPayload` at its exact size.
[[nodiscard]] bool VerifyExtractedFiles(const std::wstring& destDir);

/**
 * @brief Whether this process runs under Wine/Proton, by
 *  `wine_get_unix_file_name` being resolvable in `kernel32.dll`.
 */
[[nodiscard]] bool IsWine() noexcept;

/**
 * @brief Best-effort removal of a system-directory entry at the Unix level,
 *  for the one case Win32 cannot handle: a transparent Wine/Proton symlink
 *  into a read-only builtin DLL.
 *
 * @note Wine presents such a link to Win32 as its read-only target, so
 *  DeleteFile/MoveFile/CreateFile all resolve to that target and are refused
 *  (writing through would corrupt the shared build). Only a Unix `unlink`
 *  removes the link itself. There is no environment check: off Wine,
 *  `wine_get_unix_file_name` is absent and this simply returns false.
 *
 * @warning The Unix path is built from the *directory* (a real directory,
 *  never a transparent symlink) plus the file name, so Wine never resolves
 *  the link for us. This is what guarantees we can only ever target an entry
 *  inside this prefix's system directory, never the shared read-only DLL the
 *  link points at.
 */
bool UnixRemoveEntry(const std::wstring& dir, const std::wstring& fileName);

/**
 * @brief Makes the prefix load the binaries just extracted into @p sysDir
 *  over its own builtins: DLL overrides, then in-process COM registration.
 *  Install mode only.
 */
void PreferExtractedDirectPlay(const std::wstring& sysDir);

/**
 * @brief Loads the DISM API from the system directory: Windows' own servicing
 *  stack, which is what routes install mode to the optional feature.
 *
 * @returns Null in a Wine/Proton prefix, which has no `dismapi.dll` (it has
 *  a `dism.exe` stand-in that prints nothing and exits 0, which is why the
 *  executable is no marker).
 */
[[nodiscard]] UniqueModule LoadDismApi();

/**
 * @brief Windows path: DirectPlay is an off-by-default optional feature,
 *  turned on through the DISM API rather than by installing files ourselves.
 *
 * @returns Whether the feature reads back as installed (or pending a
 *  restart) afterwards. The enabling call's HRESULT is logged, never trusted.
 */
bool EnableDirectPlayFeature(HMODULE hDismApi);
