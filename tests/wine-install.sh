#!/bin/sh
# Runs install mode in a fresh Wine prefix and checks the prefix afterwards
# against <expected-dir>, which holds what extraction mode extracts.
# Wine has no dismapi.dll, so this covers the extraction route.
# Usage: wine-install.sh <dpsetup.exe> <expected-dir>
set -eu

exe=$(realpath "$1")
expected=$(realpath "$2")
log="$(dirname "$exe")/dpsetup.log"
WINEPREFIX="$(mktemp -d)/prefix"
export WINEPREFIX WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml='

wineboot --init
wineserver -w

status=0
wine "$exe" || status=$?
# COM registrations reach system.reg only when wineserver shuts down.
wineserver -w
echo 'dpsetup.log:'
sed 's/^/  | /' "$log"
[ "$status" -eq 0 ] || { echo "install mode exited with $status"; exit 1; }

grep -q 'LoadLibraryExW("dismapi.dll") -> error' "$log" ||
    { echo 'install mode did not take the extraction route'; exit 1; }

for file in "$expected"/*; do
    cmp "$file" "$WINEPREFIX/drive_c/windows/syswow64/$(basename "$file")"
done

for name in dplaysvr.exe dplay dplayx dpmodemx dpnet dpnhpast dpnhupnp dpnsvr.exe dpwsockx; do
    grep -qF "\"$name\"=\"native,builtin\"" "$WINEPREFIX/user.reg" ||
        { echo "no DLL override for $name"; exit 1; }
done

for dll in dplayx.dll dpnet.dll dpnhpast.dll dpnhupnp.dll; do
    grep -qE "DllRegisterServer\\(.*$dll.*\\) -> .*HRESULT\\{0x00000000\\}" "$log" ||
        { echo "$dll was not registered"; exit 1; }
done

echo 'Wine prefix checks passed'
