# Writes SHA256SUMS.txt into the dist root: one line per shipped file in the
# coreutils `sha256sum` format, so a recipient verifies the whole set with
# `sha256sum -c SHA256SUMS.txt` instead of reading a bespoke format nobody else
# speaks. The extension is for Windows, which has nothing to open an
# extensionless file with; the tool does not care what the file is called.
# Hashes are computed here from the staged tree: there is no separate
# manifest to keep in sync.
#
# Names are written relative to the manifest, i.e. to the archive root, so
# one `sha256sum -c` from the unpacked directory covers every shipped file.
#
# Only the hashes live here. The release id and the git ref are already
# compiled into the exe's version resource (see metarc.h), so repeating them
# in a side file would be a second copy free to disagree with the artifact it
# describes. The leading comment carries the
# human-readable release id purely as an anchor: `sha256sum -c` skips `#`
# lines (verified against GNU coreutils 8.32), so it costs nothing at
# verification time.
#
# Invoked as: cmake -D OUTPUT=... -D PRODUCT=... -D RELEASE_ID=... -D ROOT=...
#             -P GenerateManifest.cmake
#
# ROOT is the finished archive tree. Reading it is what makes the manifest
# describe the release rather than somebody's memory of it: a file added to the
# payload is covered without being named here, and nothing can be listed that
# the archive does not contain. Run it after the tree is complete and before it
# is zipped.

foreach(_var OUTPUT PRODUCT RELEASE_ID ROOT)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "GenerateManifest.cmake: missing required -D ${_var}=...")
    endif()
endforeach()

file(GLOB_RECURSE _paths LIST_DIRECTORIES false "${ROOT}/*")
list(REMOVE_ITEM _paths "${OUTPUT}")

# Sorted so the manifest depends on the tree's contents and not on the order a
# filesystem happened to enumerate them in.
list(SORT _paths)

if(NOT _paths)
    message(FATAL_ERROR "GenerateManifest.cmake: nothing staged under ${ROOT}")
endif()

set(_lines "# ${PRODUCT} v${RELEASE_ID}")

# Two spaces separate hash from name: the format's text-mode convention,
# which `sha256sum -c` accepts for either mode.
foreach(_path IN LISTS _paths)
    file(SHA256 "${_path}" _sha)
    cmake_path(RELATIVE_PATH _path BASE_DIRECTORY "${ROOT}" OUTPUT_VARIABLE _name)
    list(APPEND _lines "${_sha}  ${_name}")
endforeach()

list(JOIN _lines "\n" _content)

# Written through configure_file for its NEWLINE_STYLE: `file(WRITE)` emits
# CRLF on Windows, and `sha256sum -c` then reads every name with a trailing
# \r and reports the whole set as missing: a file that looks correct and
# verifies nothing. @ONLY is belt-and-braces; hex digests and file names
# contain neither @VAR@ nor ${VAR}.
file(WRITE "${OUTPUT}.in" "${_content}\n")
configure_file("${OUTPUT}.in" "${OUTPUT}" @ONLY NEWLINE_STYLE UNIX)
file(REMOVE "${OUTPUT}.in")
