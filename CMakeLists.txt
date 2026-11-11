cmake_minimum_required(VERSION 3.31)

project("dpsetup"
    # Above every version dpsetup.exe shipped under before it had a repository
    # of its own (the highest was 2.47.7), so a newer build never sorts below
    # an older one. Agent's choice: the next major, 3.0.0; continuing at 2.47.8
    # was passed over because the revision count in the fourth field starts
    # over here, and only a higher major makes that irrelevant.
    VERSION 3.0.0
    LANGUAGES CXX RC
    DESCRIPTION "DirectPlay prerequisite setup"
    HOMEPAGE_URL "https://retrofit.games"
)

set(PROJECT_AUTHOR "Johannes Wüller")
set(PROJECT_COPYRIGHT_YEAR "2026")
set(PROJECT_COPYRIGHT "Copyright © ${PROJECT_COPYRIGHT_YEAR} ${PROJECT_AUTHOR}. All rights reserved.")

# Add git metadata to the build.
# TODO: Figure out how to ensure that we never build with stale git metadata.
find_package(Git REQUIRED)

# The pattern is a bare `*`: no shell runs this command, so quotes around it
# would reach git as part of the pattern, match no tag and exclude nothing.
# With every tag excluded, `--always` leaves the abbreviated commit hash.
execute_process(
    COMMAND "${GIT_EXECUTABLE}" describe --always --abbrev=8 --dirty=-preview --exclude=*
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE GIT_DESCRIBE_RESULT
    OUTPUT_VARIABLE GIT_REF
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE GIT_REV_LIST_RESULT
    OUTPUT_VARIABLE GIT_REV_COUNT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# A preview is a build that names no commit: its version says `preview`, its
# version resource is flagged prerelease, and it is never signed.
option(PROJECT_PREVIEW "Stamp the build as a preview" OFF)

if(GIT_DESCRIBE_RESULT OR GIT_REV_LIST_RESULT)
    # No commit yet (or no git metadata): the sources are ahead of every
    # commit, so this is a preview of what would be revision 1.
    set(PROJECT_PREVIEW ON)
    set(GIT_REV_COUNT 1)
else()
    # The version names its commit with this: a tag name in it (what
    # `git describe` prints once the repository has tags and they are not
    # excluded) names no commit.
    if(NOT GIT_REF MATCHES "^[0-9a-f]+(-preview)?$")
        message(FATAL_ERROR "git describe printed '${GIT_REF}', not a commit hash; the version string would not name its commit")
    endif()

    # Uncommitted sources are ahead of the commit they were built from, so the
    # count names the commit they would become rather than the one they are not.
    # This is the *only* thing that earns the increment: a build of a clean tree is
    # the commit it was built from, however it was configured.
    if(GIT_REF MATCHES "-preview$")
        set(PROJECT_PREVIEW ON)
        math(EXPR GIT_REV_COUNT "${GIT_REV_COUNT}+1")
    endif()
endif()

# A debug build is a preview whether or not one was asked for, and so is a
# dirty tree: its source is not in the history, so it cannot be a release.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(PROJECT_PREVIEW ON)
endif()

# Set from the predicate rather than by each producer, so an explicitly
# requested preview is stamped like an implied one. Previews drop the hash: it
# identifies a commit, which for a dirty tree is not what was built, and for a
# clean one says nothing the count has not already said.
if(PROJECT_PREVIEW)
    set(GIT_REF "preview")
endif()

# The numeric file version carries the count in a 16-bit field, which the
# resource compiler would wrap without a word: a wrapped count sorts a newer
# build below every older one. If a branch ever gets there, count from a base
# commit instead (`rev-list --count <base>..HEAD`), which keeps the order
# within everything built after it.
if(GIT_REV_COUNT GREATER 65535)
    message(FATAL_ERROR "the revision count ${GIT_REV_COUNT} does not fit the 16-bit field of a file version")
endif()
set(PROJECT_VERSION_TWEAK "${GIT_REV_COUNT}")

# The version string every artifact carries (the log, the version
# resource's strings) is SemVer 2.0.0: the version, then build metadata after
# `+`, which says which build and orders nothing.
#   3.0.0+r15.1e340f7b    a build of a commit
#   3.0.0+r16.preview     a preview: no commit to name (see above)
set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}+r${PROJECT_VERSION_TWEAK}.${GIT_REF}")

# Plain text, no markdown: this string lands in a version resource and in a
# log record, neither of which renders it.
string(CONCAT PROJECT_NOTICE
    "${PROJECT_NAME} -- ${PROJECT_DESCRIPTION}\n"
    "Copyright © ${PROJECT_COPYRIGHT_YEAR} ${PROJECT_AUTHOR} (${PROJECT_HOMEPAGE_URL})\n"
    "\n"
    "DirectX and DirectPlay are registered trademarks of Microsoft Corporation.\n"
    "${PROJECT_NAME} is not affiliated with or endorsed by Microsoft Corporation."
)

# Release builds link PDBs too: a crash dump from a shipped build symbolizes
# only against the PDBs of that exact link. /DEBUG alone turns /OPT:REF and
# /OPT:ICF off, so they are named again to keep the shipped code what it was
# without PDBs. /PDBALTPATH records the bare PDB name in the binary instead of
# the build machine's path, which carries the login name.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,Release>:ProgramDatabase>")
add_link_options("$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF;/PDBALTPATH:%_PDB%>")

#
# code signing
#
# DPSETUP_SIGN_ARGS is spliced verbatim into `signtool sign`, so a store
# certificate (`/sha1 <thumbprint>`) and Azure Trusted Signing
# (`/dlib <path> /dmdf <metadata.json>`) are both a value for it rather than a
# rename. Empty (the default): nothing is signed, so an unsigned developer
# build is what everyone gets. A preview (a dirty tree included) is never
# signed: it names no commit, so it can never ship.
set(DPSETUP_SIGN_ARGS "" CACHE STRING
    "Verbatim signtool arguments identifying the signing certificate; empty means unsigned")

if(DPSETUP_SIGN_ARGS AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT PROJECT_PREVIEW)
    # signtool sits beside mt.exe, which CMake already located to merge
    # dpsetup's manifest; reusing its directory finds signtool without a
    # second Windows Kits search.
    get_filename_component(_signtool_dir "${CMAKE_MT}" DIRECTORY)
    find_program(SIGNTOOL_EXECUTABLE signtool.exe
        PATHS "${_signtool_dir}"
        NO_DEFAULT_PATH
        REQUIRED
    )
    unset(_signtool_dir)

    # NATIVE_COMMAND splits DPSETUP_SIGN_ARGS the way a Windows shell
    # would, so a quoted metadata.json path with spaces survives as one
    # token instead of being torn apart by CMake's own list separator.
    separate_arguments(_sign_args_list NATIVE_COMMAND "${DPSETUP_SIGN_ARGS}")
endif()

# Signs one target's output in place, POST_BUILD, when a certificate is
# configured. Timestamped (/tr + /td): without one the signature dies with
# the certificate instead of staying valid for the artifact's life.
function(sign_target target description)
    if(DPSETUP_SIGN_ARGS AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT PROJECT_PREVIEW)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${SIGNTOOL_EXECUTABLE}" sign
            /fd sha256 ${_sign_args_list}
            /tr http://timestamp.digicert.com /td sha256
            /d "${description}" "$<TARGET_FILE:${target}>"

            COMMENT "Signing ${target}"
        )
    endif()
endfunction()

#
# metadata
#

set(DPSETUP_MODULE "dpsetup")
set(DPSETUP_DESCRIPTION "${PROJECT_DESCRIPTION}")

function(make_c_string_literal out_var value)
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\n" "\\n" value "${value}")
    string(REPLACE "\t" "\\t" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    set("${out_var}" "\"${value}\"" PARENT_SCOPE)
endfunction()

# Source files require an escaped version of these values.
block()
    foreach(name IN ITEMS
        PROJECT_NAME
        PROJECT_DESCRIPTION
        PROJECT_AUTHOR
        PROJECT_HOMEPAGE_URL
        PROJECT_COPYRIGHT
        PROJECT_NOTICE
        PROJECT_VERSION

        DPSETUP_MODULE
        DPSETUP_DESCRIPTION
    )
        make_c_string_literal(${name} "${${name}}")
    endforeach()

    configure_file(
        "src/meta.h.in"
        "${CMAKE_BINARY_DIR}/meta.h"
        @ONLY
    )
endblock()

#
# dpsetup.exe - DirectPlay prerequisite setup
#

add_executable(${DPSETUP_MODULE} WIN32
    "src/main.cpp"
    "src/cab.cpp"
    "src/wine.cpp"
    "src/dism.cpp"
    "src/main.rc"
    # Merged into the linker's manifest by CMake (mt.exe); declares the
    # elevation level (asInvoker) and the supported OS range.
    "src/dpsetup.exe.manifest"

    "src/log.cpp"

    # Headers are listed so an IDE attributes them (Rider shows "does not
    # belong to any project target" otherwise); the build does not compile them.
    "src/steps.h"
    "src/base16.h"
    "src/casts.h"
    "src/duration.h"
    "src/encoding.h"
    "src/log.h"
    "src/metarc.h"
    "src/module_handle.h"
    "src/symbol.h"
    "src/text.h"
    "src/unique_resource.h"
)

# This exe runs during Steam's prerequisite install step, before any vcredist
# is guaranteed present, so it statically links the CRT to be self-contained.
set_target_properties(${DPSETUP_MODULE} PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_compile_features(${DPSETUP_MODULE} PRIVATE cxx_std_23)

# The generated meta.h sits beside the sources in the build tree.
target_include_directories(${DPSETUP_MODULE} PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}"
)

# Enforce consistent Unicode behavior.
target_compile_definitions(${DPSETUP_MODULE} PRIVATE
    _UNICODE
    UNICODE

    # Configure <Windows.h> sensibly.
    WIN32_LEAN_AND_MEAN
    NOMINMAX

    # Minimum supported Windows version.
    # See: https://learn.microsoft.com/en-us/cpp/porting/modifying-winver-and-win32-winnt?view=msvc-170
    WINVER=_WIN32_WINNT_WIN10
    _WIN32_WINNT=_WIN32_WINNT_WIN10
)

target_compile_options(${DPSETUP_MODULE} PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
)

target_link_libraries(${DPSETUP_MODULE} PRIVATE
    cabinet
    bcrypt
    delayimp
)

# The two imports that are no KnownDLLs load through the hook in main.cpp,
# from the system directory only: this exe runs elevated from a game
# folder, where a DLL beside it would otherwise win.
target_link_options(${DPSETUP_MODULE} PRIVATE
    "/VERSION:${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    /DELAYLOAD:cabinet.dll
    /DELAYLOAD:bcrypt.dll
)

sign_target(${DPSETUP_MODULE} "${DPSETUP_DESCRIPTION}")

#
# distribution
#

# Staged under a folder named after the archive, so that unpacking the zip
# straight into Downloads/ yields one folder. Its layout:
#   README.md                   the repository's own
#   redistributable/            what a game ships, as it is
#   examples/                   files a developer copies and edits into a build
#   symbols/                    the PDB of this exact link, for crash dumps
#   SHA256SUMS.txt
set(PROJECT_DIST_NAME "${PROJECT_NAME}-${PROJECT_VERSION}")
set(PROJECT_DIST_ROOT "${CMAKE_BINARY_DIR}/dist/${PROJECT_DIST_NAME}")

# Agent's choice: the tree is staged in every configuration and only a
# release build is zipped. dpsetup.exe runs only with dxnt.cab beside it, so
# a debug build needs the staged folder to be tried at all; staging in
# release builds alone was passed over for that reason.
add_custom_target(dist ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/dist/"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_DIST_ROOT}/"

    # The .gitattributes pins the tree byte for byte in any repository that
    # vendors it, so SHA256SUMS.txt keeps validating there.
    COMMAND ${CMAKE_COMMAND} -E copy
    "${CMAKE_SOURCE_DIR}/README.md"
    "${PROJECT_DIST_ROOT}/"
    COMMAND ${CMAKE_COMMAND} -E copy
    "${CMAKE_SOURCE_DIR}/cmake/dist.gitattributes"
    "${PROJECT_DIST_ROOT}/.gitattributes"

    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/redistributable/"
    "${PROJECT_DIST_ROOT}/redistributable/"

    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/examples/"
    "${PROJECT_DIST_ROOT}/examples/"

    COMMAND ${CMAKE_COMMAND} -E copy
    "$<TARGET_FILE:${DPSETUP_MODULE}>"
    "${PROJECT_DIST_ROOT}/redistributable/"

    # A crash dump from a shipped build symbolizes only against the PDB of
    # that exact link; a rebuild of the same commit makes a new one. It sits
    # outside redistributable/ so it never reaches a player.
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_DIST_ROOT}/symbols/"
    COMMAND ${CMAKE_COMMAND} -E copy
    "$<TARGET_PDB_FILE:${DPSETUP_MODULE}>"
    "${PROJECT_DIST_ROOT}/symbols/"

    # SHA256SUMS.txt lets a recipient verify the shipped files with the
    # standard `sha256sum -c`. Last before the zip: it reads the finished
    # tree, so anything staged after it would ship unhashed.
    COMMAND ${CMAKE_COMMAND}
    -D "OUTPUT=${PROJECT_DIST_ROOT}/SHA256SUMS.txt"
    -D "PRODUCT=${PROJECT_NAME}"
    -D "RELEASE_ID=${PROJECT_VERSION}"
    -D "ROOT=${PROJECT_DIST_ROOT}"
    -P "${CMAKE_SOURCE_DIR}/cmake/GenerateManifest.cmake"

    DEPENDS ${DPSETUP_MODULE}
    COMMENT "Staging distribution ${PROJECT_DIST_ROOT}"
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(PROJECT_DIST_FILE "${CMAKE_BINARY_DIR}/${PROJECT_DIST_NAME}.zip")
    add_custom_command(TARGET dist POST_BUILD
        # Zipped via CMake rather than Compress-Archive: Windows PowerShell's
        # cmdlet stores entry names with backslash separators, which unzip(1)
        # and Proton-side extractors treat as literal filename characters
        # instead of directories.
        COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_BINARY_DIR}/dist/"
        ${CMAKE_COMMAND} -E tar cf "${PROJECT_DIST_FILE}" --format=zip -- "${PROJECT_DIST_NAME}"

        COMMENT "Packaging distribution ${PROJECT_DIST_FILE}"
    )
endif()
