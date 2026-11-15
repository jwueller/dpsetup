#!/bin/sh
# Runs install mode in a fresh Wine or Proton prefix and checks the prefix
# afterwards against <expected-dir>, which holds what extraction mode
# extracts. Neither has dismapi.dll, so this covers the extraction route.
# Usage: [PROTON=<proton-dir>] install-wine.sh <dpsetup.exe> <expected-dir>
set -eu

exe=$(realpath "$1")
expected=$(realpath "$2")
log="$(dirname "$exe")/dpsetup.log"
export WINEDEBUG=-all

if [ -n "${PROTON:-}" ]; then
    # Proton's own script builds the prefix on first run, as under Steam.
    STEAM_COMPAT_DATA_PATH=$(mktemp -d)
    STEAM_COMPAT_CLIENT_INSTALL_PATH=$STEAM_COMPAT_DATA_PATH
    export STEAM_COMPAT_DATA_PATH STEAM_COMPAT_CLIENT_INSTALL_PATH
    export WINEPREFIX="$STEAM_COMPAT_DATA_PATH/pfx"
    run() { python3 "$PROTON/proton" run "$@"; }
    wineserver() { "$PROTON/files/bin/wineserver" "$@"; }
else
    export WINEPREFIX="$(mktemp -d)/prefix" WINEDLLOVERRIDES='mscoree,mshtml='
    run() { wine "$@"; }
    wineboot --init
    wineserver -w
fi

status=0
run "$exe" /quiet || status=$?
# COM registrations reach system.reg only when wineserver shuts down.
wineserver -w
echo
sed 's/^/  /' "$log"
echo
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

echo 'Prefix checks passed'
