cmake_minimum_required(VERSION 3.31)

project("dpsetup"
    VERSION 2.48.0
    LANGUAGES CXX RC
    DESCRIPTION "Standalone DirectPlay prerequisite setup; extracted from DirectRetrofit v1.x"
    HOMEPAGE_URL "https://github.com/jwueller/dpsetup"
)

set(PROJECT_AUTHOR "Johannes Wüller")
set(PROJECT_COPYRIGHT_YEAR "2026")
set(PROJECT_COPYRIGHT "Copyright © ${PROJECT_COPYRIGHT_YEAR} ${PROJECT_AUTHOR}. All rights reserved.")

find_package(Git REQUIRED)

# The `*` is unquoted: no shell runs this, so quotes would reach git as part
# of the pattern. With every tag excluded, `--always` prints the commit hash.
execute_process(
    COMMAND "${GIT_EXECUTABLE}" describe --always --abbrev=8 --dirty=-preview --exclude=*
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE GIT_REF_EXIT_CODE
    OUTPUT_VARIABLE GIT_REF
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE GIT_REV_COUNT_EXIT_CODE
    OUTPUT_VARIABLE GIT_REV_COUNT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# A preview names no commit: it is flagged prerelease and never signed.
option(PROJECT_PREVIEW "Stamp the build as a preview" OFF)

if(NOT GIT_REF MATCHES "^[0-9a-f]+(-preview)?$")
    message(FATAL_ERROR "git describe printed '${GIT_REF}', not a commit hash; the version string would not name its commit")
endif()

# A dirty tree counts as the commit it would become.
if(GIT_REF MATCHES "-preview$")
    set(PROJECT_PREVIEW ON)
    math(EXPR GIT_REV_COUNT "${GIT_REV_COUNT}+1")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(PROJECT_PREVIEW ON)
endif()

# The hash would name a commit that is not what was built.
if(PROJECT_PREVIEW)
    set(GIT_REF "preview")
endif()

# The resource compiler would wrap the count silently.
if(GIT_REV_COUNT GREATER 65535)
    message(FATAL_ERROR "the revision count ${GIT_REV_COUNT} does not fit the 16-bit field of a file version")
endif()
set(PROJECT_VERSION_TWEAK "${GIT_REV_COUNT}")

# SemVer 2.0.0 with build metadata: 2.0.0+r15.1e340f7b, or 2.0.0+r16.preview
set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}+r${PROJECT_VERSION_TWEAK}.${GIT_REF}")

# Plain text: neither the version resource nor the log renders markdown.
string(CONCAT PROJECT_NOTICE
    "${PROJECT_NAME} -- ${PROJECT_DESCRIPTION}\n"
    "${PROJECT_COPYRIGHT}\n"
    "${PROJECT_HOMEPAGE_URL}\n"
    "\n"
    "DirectX and DirectPlay are registered trademarks of Microsoft Corporation.\n"
    "${PROJECT_NAME} is not affiliated with or endorsed by Microsoft Corporation."
)

# Release builds link PDBs too, for crash dumps. /DEBUG turns /OPT:REF and
# /OPT:ICF off, so they are named again. /PDBALTPATH keeps the build machine's
# path (and its login name) out of the binary.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,Release>:ProgramDatabase>")
add_link_options("$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF;/PDBALTPATH:%_PDB%>")

#
# code signing
#
# Spliced verbatim into `signtool sign`: `/sha1 <thumbprint>` for a store
# certificate, `/dlib <path> /dmdf <metadata.json>` for Azure Trusted Signing.
set(DPSETUP_SIGN_ARGS "" CACHE STRING
    "Verbatim signtool arguments identifying the signing certificate; empty means unsigned"
)

if(DPSETUP_SIGN_ARGS AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT PROJECT_PREVIEW)
    # signtool sits beside mt.exe, which CMake already located.
    get_filename_component(_signtool_dir "${CMAKE_MT}" DIRECTORY)
    find_program(SIGNTOOL_EXECUTABLE signtool.exe
        PATHS "${_signtool_dir}"
        NO_DEFAULT_PATH
        REQUIRED
    )
    unset(_signtool_dir)

    # NATIVE_COMMAND keeps a quoted path with spaces as one token.
    separate_arguments(_sign_args_list NATIVE_COMMAND "${DPSETUP_SIGN_ARGS}")
endif()

# Timestamped (/tr, /td) so the signature outlives the certificate.
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
    "src/dpsetup.exe.manifest"

    "src/log.cpp"

    # Listed only so an IDE attributes the headers to the target.
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

# Static CRT: Steam runs this before any vcredist is guaranteed present.
set_target_properties(${DPSETUP_MODULE} PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_compile_features(${DPSETUP_MODULE} PRIVATE cxx_std_23)

target_include_directories(${DPSETUP_MODULE} PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}"
)

target_compile_definitions(${DPSETUP_MODULE} PRIVATE
    _UNICODE
    UNICODE

    WIN32_LEAN_AND_MEAN
    NOMINMAX

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

# Delay-loaded so the hook in main.cpp loads them from the system directory
# only: this exe runs elevated from a game folder, where a DLL beside it
# would otherwise win.
target_link_options(${DPSETUP_MODULE} PRIVATE
    "/VERSION:${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    /DELAYLOAD:cabinet.dll
    /DELAYLOAD:bcrypt.dll
)

sign_target(${DPSETUP_MODULE} "${DPSETUP_DESCRIPTION}")

#
# distribution
#

# Staged under a folder named after the archive, so unpacking yields one folder.
set(PROJECT_DIST_NAME "${PROJECT_NAME}-${PROJECT_VERSION}")
set(PROJECT_DIST_ROOT "${CMAKE_BINARY_DIR}/dist/${PROJECT_DIST_NAME}")

# Agent's choice: staged in every configuration, zipped only in release.
# dpsetup.exe runs only with dxnt.cab beside it, so a debug build needs the
# staged folder; staging in release builds alone was passed over for that.
add_custom_target(dist ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/dist/"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_DIST_ROOT}/"

    COMMAND ${CMAKE_COMMAND} -E copy
    "${CMAKE_SOURCE_DIR}/README.md"
    "${PROJECT_DIST_ROOT}/"

    # Pins the tree byte for byte wherever it is vendored, so SHA256SUMS.txt
    # keeps validating there.
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

    # Outside redistributable/ so the PDB never reaches a player.
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_DIST_ROOT}/symbols/"
    COMMAND ${CMAKE_COMMAND} -E copy
    "$<TARGET_PDB_FILE:${DPSETUP_MODULE}>"
    "${PROJECT_DIST_ROOT}/symbols/"

    # Last: anything staged after it would ship unhashed.
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
        # Not Compress-Archive: it stores backslash separators, which unzip(1)
        # and Proton-side extractors read as filename characters.
        COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_BINARY_DIR}/dist/"
        ${CMAKE_COMMAND} -E tar cf "${PROJECT_DIST_FILE}" --format=zip -- "${PROJECT_DIST_NAME}"

        COMMENT "Packaging distribution ${PROJECT_DIST_FILE}"
    )
endif()
