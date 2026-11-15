/*
 * Creates one DirectPlay class per DLL and reports which DLL served it:
 * Wine's builtin or the native copy dpsetup installed. Run inside the
 * prefix. The report goes to comcheck.log beside the exe and ends with
 * "all native" only when every class was created from a native DLL: Proton
 * passes on neither the console output nor, verifiably, the exit code.
 * The registry cannot tell the two apart: Wine registers its builtins under
 * the same class IDs and the same paths.
 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>

/* Wine marks its builtin PE images with this string at offset 0x40, inside
 * the DOS header region that is mapped with the image. */
static const char kBuiltinSignature[] = "Wine builtin DLL";

static const struct
{
    const char* dll;
    const wchar_t* clsid;
} kClasses[] = {
    // @formatter:off
    {"dplayx.dll",   L"{D1EB6D20-8923-11D0-9D97-00A0C90A43CB}"},
    {"dpnet.dll",    L"{286F484D-375E-4458-A272-B138E2F80A6A}"},
    {"dpnhupnp.dll", L"{B9C2E9C4-68C1-4D42-A7A1-E76A26982AD6}"},
    {"dpnhpast.dll", L"{963AB779-16A1-477C-A36D-CB5E711938F7}"},
    // @formatter:on
};

int main(void)
{
    char path[MAX_PATH];
    const DWORD cchPath = GetModuleFileNameA(NULL, path, MAX_PATH);
    char* const slash = strrchr(path, '\\');
    if (cchPath == 0 || cchPath >= MAX_PATH || !slash)
        return 1;
    strcpy(slash + 1, "comcheck.log");
    FILE* const report = fopen(path, "w");
    if (!report)
        return 1;

    int failures = 0;
    const HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hrInit))
    {
        fprintf(
            report,
            "CoInitializeEx -> HRESULT 0x%08lX\nFAILED\n",
            (unsigned long)hrInit
        );
        fclose(report);
        return 1;
    }

    for (size_t i = 0; i < sizeof(kClasses) / sizeof(kClasses[0]); ++i)
    {
        CLSID clsid;
        IUnknown* pUnknown = NULL;
        HRESULT hr = CLSIDFromString(kClasses[i].clsid, &clsid);
        if (SUCCEEDED(hr))
        {
            hr = CoCreateInstance(
                &clsid,
                NULL,
                CLSCTX_INPROC_SERVER,
                &IID_IUnknown,
                (void**)&pUnknown
            );
        }

        const HMODULE hModule = GetModuleHandleA(kClasses[i].dll);
        const char* origin = "not loaded";
        if (hModule)
        {
            const int builtin = memcmp(
                (const char*)hModule + 0x40,
                kBuiltinSignature,
                sizeof(kBuiltinSignature) - 1
            ) == 0;
            origin = builtin ? "builtin" : "native";
        }

        fprintf(
            report,
            "%ls (%s) -> HRESULT 0x%08lX, %s\n",
            kClasses[i].clsid,
            kClasses[i].dll,
            (unsigned long)hr,
            origin
        );
        if (FAILED(hr) || strcmp(origin, "native") != 0)
            ++failures;
        if (pUnknown)
            IUnknown_Release(pUnknown);
    }

    CoUninitialize();
    fputs(failures == 0 ? "all native\n" : "FAILED\n", report);
    fclose(report);
    return failures == 0 ? 0 : 1;
}
