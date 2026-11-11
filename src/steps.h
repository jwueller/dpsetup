#pragma once

/// @file
/// Declarations shared across dpsetup's per-concern translation units.

#include <Windows.h>

#include <cstdint>
#include <string>

#include "unique_resource.h"

namespace Retrofit::Setup
{
/**
 * @brief The cabinet shipped next to this exe, carrying the DirectPlay
 *  binaries we extract (see @ref kWantedFiles).
 */
inline constexpr auto kDxntCabName = "dxnt.cab";

/**
 * @brief The DirectPlay binaries extracted from @ref kDxntCabName, each paired
 *  with its exact decompressed size.
 *
 * @warning Extraction is allowlist-only on purpose: that cabinet also
 *  carries ancient Direct3D binaries that must never reach the system
 *  directory.
 *
 * @note The sizes are the committed Feb-2010 redist's (a fixed target)
 *  and serve as the extraction-success oracle in @ref VerifyExtractedFiles: they are what lets
 *  the installer route itself without any environment detection. A system
 *  directory holding files of exactly these sizes means our native binaries
 *  are in place; anything else (Wine's differently-sized builtins, a failed
 *  write) is treated as "not done yet" and the cascade falls through.
 */
struct WantedFile
{
    LPCSTR name;
    DWORD size;
};

inline constexpr WantedFile kWantedFiles[] = {
    {"dplaysvr.exe", 28160},
    {"dplayx.dll", 230400},
    {"dpmodemx.dll", 77824},
    {"dpnet.dll", 723968},
    {"dpnhpast.dll", 32768},
    {"dpnhupnp.dll", 68096},
    {"dpnsvr.exe", 16896},
    {"dpwsockx.dll", 79360},
};

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

/**
 * @brief Widens a cabinet member name so it can enter a destination path,
 *  which must go through a ...W API.
 *
 * @note FDI's cabinet member names (psz1/2/3 in `FDINOTIFICATION`) are plain
 *  ASCII in this narrow/OEM API: a sanctioned narrow-API surface, not a
 *  case to convert to wide strings.
 */
[[nodiscard]] std::wstring WidenAscii(LPCSTR s);

/**
 * @brief Opens @ref kDxntCabName, verifies its integrity, and runs a single FDI
 *  pass extracting @ref kWantedFiles into @ref destDir.
 */
[[nodiscard]] ExtractResult ExtractDirectPlayFiles(const std::wstring& dxntCabPath, const std::wstring& destDir);

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
 * @brief Finds @ref kDxntCabName next to this exe and runs
 *  @ref ExtractDirectPlayFiles on it, on every machine: install mode's first
 *  tier and the whole of extraction mode.
 */
[[nodiscard]] ExtractResult RunExtractionPipeline(const std::wstring& exeDir, const std::wstring& destDir);

/**
 * @brief The first Win32 error the last @ref RunExtractionPipeline hit
 *  creating the destination directory or a file in it, or `ERROR_SUCCESS`.
 *  Lets a failed extraction say "access denied" rather than a bare failure.
 */
[[nodiscard]] DWORD FirstExtractionError() noexcept;

/**
 * @brief The extraction-success oracle: whether @p destDir now holds our
 *  native DirectPlay binaries, checked by exact size (see @ref WantedFile).
 *
 * @note A match means the native binaries are in place; a miss (a failed
 *  write, or files of a different size such as Wine's builtins) means the
 *  extraction did not take effect.
 */
[[nodiscard]] bool VerifyExtractedFiles(const std::wstring& destDir);

/**
 * @brief Marks the extracted binaries as native-first in the Wine registry.
 *  Install mode only.
 */
void ApplyWineDllOverrides();

/**
 * @brief Registers the extracted COM servers in-process. Install mode only.
 */
void RegisterWineComServers(const std::wstring& destDir);

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
} // namespace Retrofit::Setup
