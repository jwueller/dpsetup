#!/bin/sh
# Runs install mode in a fresh Wine or Proton prefix and checks the prefix
# afterwards against <expected-dir>, which holds what extraction mode
# extracts, then creates the DirectPlay classes in it (comcheck.c). Neither
# has dismapi.dll, so this covers the extraction route. Needs
# i686-w64-mingw32-gcc.
# Usage: [PROTON=<proton-dir>] install-wine.sh <dpsetup.exe> <expected-dir>
set -eu

exe=$(realpath "$1")
expected=$(realpath "$2")
log="$(dirname "$exe")/dpsetup.log"
export WINEDEBUG=-all

# Both build the missing prefix within the run itself, as Proton does under
# Steam.
if [ -n "${PROTON:-}" ]; then
    STEAM_COMPAT_DATA_PATH=$(mktemp -d)
    STEAM_COMPAT_CLIENT_INSTALL_PATH=$STEAM_COMPAT_DATA_PATH
    export STEAM_COMPAT_DATA_PATH STEAM_COMPAT_CLIENT_INSTALL_PATH
    export WINEPREFIX="$STEAM_COMPAT_DATA_PATH/pfx"
    run() { python3 "$PROTON/proton" run "$@"; }
    wineserver() { "$PROTON/files/bin/wineserver" "$@"; }
else
    export WINEPREFIX="$(mktemp -d)/prefix" WINEDLLOVERRIDES='mscoree,mshtml='
    run() { wine "$@"; }
fi

status=0
# Traced, so the output shows the command each runner really executes.
set -x
run "$exe" /quiet || status=$?
{ set +x; } 2>/dev/null
# COM registrations reach system.reg only when wineserver shuts down.
wineserver -w

echo
sed 's/^/  /' "$log"
echo
[ "$status" -eq 0 ] || { echo "install mode exited with $status"; exit 1; }

for file in "$expected"/*; do
    cmp "$file" "$WINEPREFIX/drive_c/windows/syswow64/$(basename "$file")"
done

for name in dplaysvr.exe dplay dplayx dpmodemx dpnet dpnhpast dpnhupnp dpnsvr.exe dpwsockx; do
    grep -qF "\"$name\"=\"native,builtin\"" "$WINEPREFIX/user.reg" ||
        { echo "no DLL override for $name"; exit 1; }
done

checker="$(mktemp -d)/comcheck.exe"
i686-w64-mingw32-gcc -O2 -Wall -static-libgcc \
    -o "$checker" "$(dirname "$0")/comcheck.c" -lole32 -luuid
set -x
run "$checker"
{ set +x; } 2>/dev/null

echo 'Prefix checks passed'
