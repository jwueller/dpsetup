/// @file
/// CAB/FDI extraction and integrity verification: opening `dxnt.cab`,
/// verifying it against the pinned size/SHA-256 before any FDI pass touches
/// it, running the single FDI pass that extracts @ref kWantedFiles into a
/// destination directory, and checking afterwards that the destination holds
/// the extracted files. Everything the other files call is declared in
/// `steps.h`; the rest here is this file's own machinery.

#include <Windows.h>
#include <bcrypt.h>
#include <fdi.h>

#include <algorithm>
// Declares std::free, std::malloc, which only reach this file transitively.
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdlib>
// Declares std::memcmp, std::strcmp, which only reach this file transitively.
// ReSharper disable once CppUnusedIncludeDirective
#include <cstring>
#include <filesystem>
#include <iterator>
#include <string>
#include <cstdint>
#include <system_error>

#include "base16.h"
#include "encoding.h"
#include "log.h"
#include "text.h"
#include "unique_resource.h"

#include "steps.h"

namespace
{
/**
 * @brief Pinned identity of the committed @ref kDxntCabName (the
 *  Feb-2010 DirectX redistributable's), checked by @ref VerifyDxntCabIntegrity
 *  before any FDI pass touches the file.
 *
 * @warning dxnt.cab is a fixed, known artifact, exactly like
 *  @ref kWantedFiles' exact sizes: this must never loosen to an
 *  existence check. A mismatch means the shipped cab is corrupt or has
 *  been tampered with, and extraction must not proceed: the allowlisted
 *  names in @ref kWantedFiles could otherwise overwrite the system
 *  directory with unverified content.
 */
constexpr long long kDxntCabExpectedSize = 13264168;
constexpr BYTE kDxntCabExpectedSha256[32] = {
    // @formatter:off
    0x82, 0x54, 0xBE, 0xB9, 0x72, 0x41, 0x39, 0xE0,
    0x15, 0x03, 0xEC, 0x65, 0xE7, 0x68, 0x2D, 0xA5,
    0x93, 0xCC, 0x4E, 0x81, 0xEC, 0x4B, 0xFF, 0x43,
    0x1E, 0x5F, 0xD3, 0x7C, 0xE5, 0x65, 0x60, 0x34,
    // @formatter:on
};

/**
 * @brief One stream FDI can touch through its file callbacks, as a tagged
 *  struct.
 *
 * @note FDI's callbacks carry no user context pointer, only an
 *  @ref INT_PTR "handle", so every stream FDI can touch (the dxnt.cab
 *  source it opens itself, and destination sinks we hand back from
 *  @ref fdintCOPY_FILE) is represented by this one struct, and the handle
 *  is just a pointer to one of these.
 */
struct FdiStream
{
    enum class Kind : std::uint8_t
    {
        /// read-only view of dxnt.cab
        SourceFile,
        /// real destination file (one of the final 8 binaries)
        SinkFile,
    };

    Kind kind = Kind::SourceFile;
    /// logical position
    long long pos = 0;

    /// @ref Kind::SourceFile / @ref Kind::SinkFile
    HANDLE hFile = INVALID_HANDLE_VALUE;
    /// @ref Kind::SourceFile: dxnt.cab's size, for SEEK_END
    long long size = 0;
};

/**
 * @brief Single-pass, single-threaded extraction state.
 *
 * @note @ref FdiOpen dispatches on a marker string to the pre-positioned
 *  source stream below rather than opening a real path (see
 *  @ref ExtractDirectPlayFiles); there is only ever one active pass, so
 *  one global context is enough.
 */
struct ExtractionContext
{
    FdiStream source;
    std::wstring destDir;
};

ExtractionContext g_ctx;

/// First Win32 error of the current extraction, see @ref FirstExtractionError.
DWORD g_firstError = ERROR_SUCCESS;

FNALLOC(FdiAlloc)
{
    return std::malloc(cb);
}

FNFREE(FdiFree)
{
    std::free(pv);
}

/**
 * @brief FDI's file-open callback; only ever sees the marker string, never
 *  a real path.
 *
 * @note @ref FDICopy concatenates its cabPath + cabinet-name arguments
 *  and hands the result here. We pass `""` + `"*cab*"` as those
 *  arguments (see @ref ExtractDirectPlayFiles), so @ref pszFile always
 *  arrives as that marker. The destination side never goes through
 *  @ref FNOPEN: `fdintCOPY_FILE`'s return value IS the write handle.
 *
 * @warning FDI opens the cabinet marker more than once and keeps both
 *  "handles" live concurrently (observed: two @ref FNOPEN calls with no
 *  @ref FNCLOSE between them): it uses one to decompress sequentially
 *  and the other to peek/validate. Returning the SAME stream object for
 *  both would alias one @ref FdiStream::pos field between two
 *  logically-independent FDI read cursors, corrupting whichever one runs
 *  behind. So every @ref FNOPEN call gets its OWN stream instance (own
 *  `pos`, starting at 0), copied from the template in @ref g_ctx; the
 *  underlying Win32 file handle is shared (safe, since every read
 *  explicitly seeks first) but owned and closed only once, by
 *  @ref ExtractDirectPlayFiles, not by @ref FdiClose.
 */
FNOPEN(FdiOpen)
{
    if (std::strcmp(pszFile, "*cab*") != 0)
        return -1;

    auto* s = new FdiStream{};
    s->kind = FdiStream::Kind::SourceFile;
    s->hFile = g_ctx.source.hFile;
    s->size = g_ctx.source.size;
    return reinterpret_cast<INT_PTR>(s);
}

FNREAD(FdiRead)
{
    auto* s = reinterpret_cast<FdiStream*>(hf);
    switch (s->kind)
    {
    case FdiStream::Kind::SourceFile:
    {
        LARGE_INTEGER li{};
        li.QuadPart = s->pos;
        if (!SetFilePointerEx(s->hFile, li, nullptr, FILE_BEGIN))
            return static_cast<UINT>(-1);

        DWORD read = 0;
        if (!ReadFile(s->hFile, pv, cb, &read, nullptr))
            return static_cast<UINT>(-1);

        s->pos += read;
        return read;
    }

    default:
        return static_cast<UINT>(-1);
    }
}

FNWRITE(FdiWrite)
{
    auto* s = reinterpret_cast<FdiStream*>(hf);
    switch (s->kind)
    {
    case FdiStream::Kind::SinkFile:
    {
        DWORD written = 0;
        const HANDLE hFile = s->hFile;
        if (!WriteFile(
            hFile,
            pv,
            cb,
            &written,
            nullptr
        ))
        {
            const DWORD error = GetLastError();
            LOG(
                "WriteFile({} bytes) -> error {}",
                cb,
                error
            );
            return static_cast<UINT>(-1);
        }
        s->pos += written;
        return written;
    }

    default:
        return static_cast<UINT>(-1);
    }
}

/**
 * @brief Frees the per-open stream wrapper allocated by @ref FdiOpen,
 *  and nothing else.
 *
 * @note Only ever called on streams FDI itself opened via @ref FdiOpen;
 *  destination files are closed and finalized from @ref fdintCLOSE_FILE_INFO
 *  instead (FDI documents that it will NOT call @ref FNCLOSE for those).
 *  The real Win32 file handle is shared across every `"*cab*"` open and
 *  owned by @ref ExtractDirectPlayFiles, so only the per-open wrapper is
 *  freed here, never the handle itself.
 */
FNCLOSE(FdiClose)
{
    const auto* s = reinterpret_cast<FdiStream*>(hf);
    delete s;
    return 0;
}

FNSEEK(FdiSeek)
{
    auto* s = reinterpret_cast<FdiStream*>(hf);

    long long newPos;
    switch (seektype)
    {
    case SEEK_SET:
        newPos = dist;
        break;

    case SEEK_CUR:
        newPos = s->pos + dist;
        break;

    case SEEK_END:
        switch (s->kind)
        {
        case FdiStream::Kind::SourceFile:
            newPos = s->size + dist; // SEEK_END is relative to dxnt.cab's own size
            break;
        default:
            newPos = s->pos + dist;
            break;
        }
        break;

    default:
        return -1;
    }

    s->pos = newPos;
    return static_cast<long>(newPos);
}

/**
 * @brief @ref fdintCOPY_FILE: decides whether to accept a cabinet member
 *  and, if so, hands back the stream that will receive it. Accepts only
 *  @ref kWantedFiles.
 */
INT_PTR OnCopyFile(const PFDINOTIFICATION pfdin)
{
    const bool wanted = std::ranges::any_of(
        kWantedFiles,
        [&](const WantedFile& w)
        {
            return EqualsAsciiNoCase(pfdin->psz1, w.name);
        }
    );
    if (!wanted)
        return 0;

    const auto psz1 = pfdin->psz1;
    const std::wstring fileName = WidenAscii(psz1);
    const std::wstring destPath = g_ctx.destDir + L"\\" + fileName;

    // Clear the destination so CREATE_ALWAYS writes a fresh regular file:
    // never write *through* an existing entry. On Wine/Proton the system
    // directory is pre-populated with transparent symlinks into a read-only
    // builtin; Wine presents such a link to Win32 as its target, so a plain
    // CreateFile would follow it and (try to) corrupt the shared DLL. A
    // Win32 delete removes an ordinary file; if that is refused (the
    // transparent-symlink case), unlink the link at the Unix level, which is
    // a no-op off Wine. Only create the file once the entry is confirmed
    // gone, so we never write through a surviving link.
    if (GetFileAttributesW(destPath.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        SetFileAttributesW(destPath.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileW(destPath.c_str()))
            UnixRemoveEntry(g_ctx.destDir, fileName);
    }
    if (GetFileAttributesW(destPath.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        LOG(
            "OnCopyFile('{}') -> refusing to write through the surviving entry",
            psz1
        );
        return -1;
    }

    const HANDLE hFile = CreateFileW(
        destPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        if (g_firstError == ERROR_SUCCESS)
            g_firstError = error;
        LOG(
            "CreateFileW('{}') -> error {}",
            Utf8FromWide(destPath),
            error
        );
        return -1;
    }
    LOG(
        "OnCopyFile('{}') -> placed",
        psz1
    );

    auto* s = new FdiStream{};
    s->kind = FdiStream::Kind::SinkFile;
    s->hFile = hFile;
    return reinterpret_cast<INT_PTR>(s);
}

/**
 * @brief @ref fdintCLOSE_FILE_INFO: finalizes a destination stream handed
 *  out by @ref OnCopyFile.
 */
BOOL OnCloseFileInfo(const PFDINOTIFICATION pfdin)
{
    const auto* s = reinterpret_cast<FdiStream*>(pfdin->hf);
    if (s->kind == FdiStream::Kind::SinkFile)
        CloseHandle(s->hFile);
    delete s;
    return TRUE;
}

/// Dispatches the few FDI notifications this extraction cares about.
FNFDINOTIFY(FdiNotify)
{
    switch (fdint)
    {
    case fdintCOPY_FILE:
        return OnCopyFile(pfdin);

    case fdintCLOSE_FILE_INFO:
        return OnCloseFileInfo(pfdin) ? TRUE : FALSE;

    case fdintNEXT_CABINET:
        return -1; // dxnt.cab is never split across volumes

    default:
        return 0;
    }
}

/**
 * @brief Verifies dxnt.cab is byte-for-byte the committed artifact
 *  (exact size, then SHA-256) before any FDI pass touches it.
 *
 * @note Cheap fields first: a size mismatch skips the hash entirely.
 *  @ref FdiRead always seeks to an absolute position before reading, so
 *  hashing the whole file here does not need to rewind the handle
 *  afterward.
 */
[[nodiscard]] bool VerifyDxntCabIntegrity(const HANDLE hCab, const long long fileSize)
{
    if (fileSize != kDxntCabExpectedSize)
    {
        LOG(
            "VerifyDxntCabIntegrity('{}') -> size mismatch, {} bytes, expected {}",
            kDxntCabName,
            fileSize,
            kDxntCabExpectedSize
        );

        return false;
    }

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    const NTSTATUS openStatus = BCryptOpenAlgorithmProvider(
        &hAlg,
        BCRYPT_SHA256_ALGORITHM,
        nullptr,
        0
    );
    if (openStatus != 0)
    {
        LOG(
            "BCryptOpenAlgorithmProvider(SHA256) -> status 0x{:08X}",
            static_cast<unsigned>(openStatus)
        );

        return false;
    }

    BCRYPT_HASH_HANDLE hHash = nullptr;
    const NTSTATUS createStatus = BCryptCreateHash(
        hAlg,
        &hHash,
        nullptr,
        0,
        nullptr,
        0,
        0
    );
    if (createStatus != 0)
    {
        LOG(
            "BCryptCreateHash -> status 0x{:08X}",
            static_cast<unsigned>(createStatus)
        );

        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    constexpr LARGE_INTEGER zero{};
    bool ok = SetFilePointerEx(
        hCab,
        zero,
        nullptr,
        FILE_BEGIN
    ) != FALSE;

    BYTE buf[64 * 1024];
    while (ok)
    {
        DWORD read = 0;
        if (!ReadFile(
            hCab,
            buf,
            sizeof(buf),
            &read,
            nullptr
        ))
        {
            ok = false;
            break;
        }
        if (read == 0)
            break;
        if (BCryptHashData(
            hHash,
            buf,
            read,
            0
        ) != 0)
        {
            ok = false;
            break;
        }
    }

    BYTE digest[32]{};
    if (BCryptFinishHash(
        hHash,
        digest,
        sizeof(digest),
        0
    ) != 0)
    {
        ok = false;
    }

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!ok)
    {
        LOG(
            "VerifyDxntCabIntegrity('{}') -> SHA-256 computation failed",
            kDxntCabName
        );

        return false;
    }

    if (std::memcmp(digest, kDxntCabExpectedSha256, sizeof(digest)) != 0)
    {
        const std::string sha256 = Base16FromBytes(digest);
        const std::string expected = Base16FromBytes(kDxntCabExpectedSha256);
        LOG(
            "VerifyDxntCabIntegrity('{}') -> SHA-256 mismatch, got {}, expected {}",
            kDxntCabName,
            sha256,
            expected
        );

        return false;
    }

    return true;
}
} // namespace

std::wstring WidenAscii(const LPCSTR s)
{
    std::wstring out;
    for (const char* p = s; *p; ++p)
        out.push_back(static_cast<unsigned char>(*p));
    return out;
}

ExtractResult ExtractDirectPlayFiles(const std::wstring& dxntCabPath, const std::wstring& destDir)
{
    // PFNCLOSE only frees per-open stream wrappers (see FdiClose); the
    // underlying dxnt.cab file handle is owned here.
    const UniqueHandle hCab(
        CreateFileW(
            dxntCabPath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        )
    );
    if (!hCab)
    {
        const DWORD error = GetLastError();
        LOG(
            "CreateFileW('{}') -> error {}",
            Utf8FromWide(dxntCabPath),
            error
        );
        return ExtractResult::Failed;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(hCab.Get(), &fileSize))
    {
        const DWORD error = GetLastError();
        LOG(
            "GetFileSizeEx('{}') -> error {}",
            Utf8FromWide(dxntCabPath),
            error
        );
        return ExtractResult::Failed;
    }

    if (!VerifyDxntCabIntegrity(hCab.Get(), fileSize.QuadPart))
        return ExtractResult::Fatal;

    g_ctx = ExtractionContext{};
    g_ctx.destDir = destDir;

    g_ctx.source.kind = FdiStream::Kind::SourceFile;
    g_ctx.source.hFile = hCab.Get();
    g_ctx.source.size = fileSize.QuadPart;

    ERF erf{};
    const auto hFdi = FDICreate(
        FdiAlloc,
        FdiFree,
        FdiOpen,
        FdiRead,
        FdiWrite,
        FdiClose,
        FdiSeek,
        cpuUNKNOWN,
        &erf
    );
    if (!hFdi)
    {
        LOG(
            "FDICreate(cpuUNKNOWN) -> failed"
        );
        return ExtractResult::Failed;
    }

    char cabName[] = "*cab*";
    char cabPath[] = "";
    const bool ok = FDICopy(
        hFdi,
        cabName,
        cabPath,
        0,
        FdiNotify,
        nullptr,
        nullptr
    ) != FALSE;
    LOG(
        "FDICopy -> {} (erfOper {}, erfType {})",
        ok ? "ok" : "failed",
        erf.erfOper,
        erf.erfType
    );

    FDIDestroy(hFdi);

    return ok ? ExtractResult::Ok : ExtractResult::Failed;
}

ExtractResult RunExtractionPipeline(const std::wstring& exeDir, const std::wstring& destDir)
{
    const std::wstring cabPath = exeDir + L"\\" + WidenAscii(kDxntCabName);
    if (GetFileAttributesW(cabPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        const DWORD error = GetLastError();
        LOG(
            "GetFileAttributesW('{}') -> error {}",
            Utf8FromWide(cabPath),
            error
        );
        return ExtractResult::Failed;
    }

    g_firstError = ERROR_SUCCESS;
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);
    if (ec)
    {
        // The value of an MSVC filesystem error is the Win32 error code.
        g_firstError = static_cast<DWORD>(ec.value());
        LOG(
            "create_directories('{}') -> error {}",
            Utf8FromWide(destDir),
            g_firstError
        );
        return ExtractResult::Failed;
    }

    return ExtractDirectPlayFiles(cabPath, destDir);
}

DWORD FirstExtractionError() noexcept
{
    return g_firstError;
}

bool VerifyExtractedFiles(const std::wstring& destDir)
{
    for (const auto& [name, expectedSize] : kWantedFiles)
    {
        const std::wstring path = destDir + L"\\" + WidenAscii(name);
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (
            !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
        )
        {
            LOG(
                "VerifyExtractedFiles('{}') -> {} missing",
                Utf8FromWide(destDir),
                name
            );
            return false;
        }
        if (info.nFileSizeHigh != 0 || info.nFileSizeLow != expectedSize)
        {
            const auto actualSize = info.nFileSizeLow;
            LOG(
                "VerifyExtractedFiles('{}') -> {} size mismatch, {} bytes, expected {}",
                Utf8FromWide(destDir),
                name,
                actualSize,
                expectedSize
            );
            return false;
        }
    }
    constexpr auto cFiles = std::size(kWantedFiles);
    LOG(
        "VerifyExtractedFiles('{}') -> all {} native binaries present",
        Utf8FromWide(destDir),
        cFiles
    );
    return true;
}
