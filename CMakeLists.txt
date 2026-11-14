cmake_minimum_required(VERSION 3.31)

project("dpsetup"
    VERSION 2.48.0
    LANGUAGES CXX RC
    DESCRIPTION "Standalone DirectPlay prerequisite setup"
    HOMEPAGE_URL "https://github.com/jwueller/dpsetup"
)

set(PROJECT_AUTHOR "Johannes Wüller")
set(PROJECT_COPYRIGHT_YEAR "2026")
set(PROJECT_COPYRIGHT "Copyright © ${PROJECT_COPYRIGHT_YEAR} ${PROJECT_AUTHOR}. All rights reserved.")

# Git metadata must be available for traceability.
find_package(Git REQUIRED)
execute_process(
    # This doesn't run in a shell, so the wildcard `*` reaches git correctly here.
    COMMAND "${GIT_EXECUTABLE}" describe --always --abbrev=8 --dirty=-preview --exclude=*
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_REF
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_REV_COUNT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Previews are treated as ephemeral builds, so they are not
# signed and do not necessarily receive a unique version.
option(PROJECT_PREVIEW "Stamp the build as a preview" OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(PROJECT_PREVIEW ON)
endif()

if(NOT GIT_REF MATCHES "^[0-9a-f]+(-preview)?$")
    message(FATAL_ERROR "Expected a git commit hash due to all tags being excluded, but got '${GIT_REF}' instead.")
endif()
if(GIT_REF MATCHES "-preview$")
    set(PROJECT_PREVIEW ON)
    math(EXPR GIT_REV_COUNT "${GIT_REV_COUNT}+1")
endif()
if(PROJECT_PREVIEW)
    set(GIT_REF "preview")
endif()

if(GIT_REV_COUNT GREATER 65535)
    message(FATAL_ERROR "Expected the git revision count (${GIT_REV_COUNT}) to fit into a Windows version resource.")
endif()
set(PROJECT_VERSION_TWEAK "${GIT_REV_COUNT}")

# SemVer 2.0.0 format with build metadata.
set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}+r${PROJECT_VERSION_TWEAK}.${GIT_REF}")

string(CONCAT PROJECT_NOTICE
    "${PROJECT_NAME} -- ${PROJECT_DESCRIPTION}\n"
    "${PROJECT_COPYRIGHT}\n"
    "${PROJECT_HOMEPAGE_URL}\n"
    "\n"
    "This tool was originally developed for DirectRetrofit v1.x and is now\n"
    "maintained as a standalone utility, since DirectRetrofit v2.x and later no\n"
    "longer need it.\n"
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
set(SIGNTOOL_ARGS "" CACHE STRING
    "Verbatim signtool arguments identifying the signing certificate; empty means unsigned"
)

set(PROJECT_SIGNABLE OFF)
if(SIGNTOOL_ARGS AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT PROJECT_PREVIEW)
    set(PROJECT_SIGNABLE ON)

    # signtool sits beside mt.exe, which CMake already located.
    get_filename_component(_signtool_dir "${CMAKE_MT}" DIRECTORY)
    find_program(SIGNTOOL_EXECUTABLE signtool.exe
        PATHS "${_signtool_dir}"
        NO_DEFAULT_PATH
        REQUIRED
    )
    unset(_signtool_dir)

    # NATIVE_COMMAND keeps a quoted path with spaces as one token.
    separate_arguments(SIGNTOOL_ARGS_NATIVE NATIVE_COMMAND "${SIGNTOOL_ARGS}")
endif()

# Timestamped (/tr, /td) so the signature outlives the certificate.
function(sign_target target description)
    if(PROJECT_SIGNABLE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${SIGNTOOL_EXECUTABLE}" sign
            /fd sha256 ${SIGNTOOL_ARGS_NATIVE}
            /tr http://timestamp.digicert.com /td sha256
            /d "${description}" "$<TARGET_FILE:${target}>"

            COMMENT "Signing ${target}"
        )
    endif()
endfunction()

#
# metadata
#

set(PROJECT_MODULE "dpsetup")

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
        PROJECT_VERSION
        PROJECT_DESCRIPTION
        PROJECT_HOMEPAGE_URL
        PROJECT_AUTHOR
        PROJECT_COPYRIGHT
        PROJECT_NOTICE
        PROJECT_MODULE
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
# build
#

add_executable(${PROJECT_MODULE} WIN32
    "src/main.cpp"
    "src/cab.cpp"
    "src/wine.cpp"
    "src/dism.cpp"
    "src/main.rc"
    "src/dpsetup.exe.manifest"

    "src/log.cpp"

    # Listed only so an IDE attributes the headers to the target.
    "src/steps.h"
    "src/casts.h"
    "src/encoding.h"
    "src/log.h"
    "src/proc_address.h"
    "src/unique_resource.h"
)

# Static CRT: Games may run this before any vcredist is guaranteed to be present.
set_target_properties(${PROJECT_MODULE} PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_compile_features(${PROJECT_MODULE} PRIVATE cxx_std_23)

target_include_directories(${PROJECT_MODULE} PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}"
)

target_compile_definitions(${PROJECT_MODULE} PRIVATE
    _UNICODE
    UNICODE

    WIN32_LEAN_AND_MEAN
    NOMINMAX

    WINVER=_WIN32_WINNT_WIN10
    _WIN32_WINNT=_WIN32_WINNT_WIN10
)

target_compile_options(${PROJECT_MODULE} PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
)

target_link_libraries(${PROJECT_MODULE} PRIVATE
    cabinet
    bcrypt
    delayimp
)

# Delay-loaded so the hook in main.cpp loads them from the system directory
# only: this exe runs elevated from a game folder, where a DLL beside it
# would otherwise win.
target_link_options(${PROJECT_MODULE} PRIVATE
    "/VERSION:${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    /DELAYLOAD:cabinet.dll
    /DELAYLOAD:bcrypt.dll
)

sign_target(${PROJECT_MODULE} "${PROJECT_DESCRIPTION}")

#
# distribution
#

# Staged under a folder named after the archive, so unpacking yields one folder.
set(PROJECT_DIST_NAME "${PROJECT_NAME}-${PROJECT_VERSION}")
set(PROJECT_DIST_ROOT "${CMAKE_BINARY_DIR}/dist/${PROJECT_DIST_NAME}")

# dpsetup.exe runs only with dxnt.cab beside it, so a debug build needs the
# staged folder; staging in release builds alone was passed over for that.
add_custom_target(dist ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/dist/"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_DIST_ROOT}/"

    COMMAND ${CMAKE_COMMAND} -E copy
    "${CMAKE_SOURCE_DIR}/README.md"
    "${CMAKE_SOURCE_DIR}/LICENSE"
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
    "$<TARGET_FILE:${PROJECT_MODULE}>"
    "${PROJECT_DIST_ROOT}/redistributable/"

    # Outside `redistributable/` so the PDB isn't shipped by default.
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_DIST_ROOT}/symbols/"
    COMMAND ${CMAKE_COMMAND} -E copy
    "$<TARGET_PDB_FILE:${PROJECT_MODULE}>"
    "${PROJECT_DIST_ROOT}/symbols/"

    # Last: anything staged after it would ship unhashed.
    COMMAND ${CMAKE_COMMAND}
    -D "OUTPUT=${PROJECT_DIST_ROOT}/SHA256SUMS.txt"
    -D "PRODUCT=${PROJECT_NAME}"
    -D "RELEASE_ID=${PROJECT_VERSION}"
    -D "ROOT=${PROJECT_DIST_ROOT}"
    -P "${CMAKE_SOURCE_DIR}/cmake/GenerateManifest.cmake"

    DEPENDS ${PROJECT_MODULE}
    COMMENT "Staging distribution ${PROJECT_DIST_ROOT}"
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(PROJECT_DIST_FILE "${CMAKE_BINARY_DIR}/${PROJECT_DIST_NAME}.zip")
    add_custom_command(TARGET dist POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_BINARY_DIR}/dist/"
        ${CMAKE_COMMAND} -E tar cf "${PROJECT_DIST_FILE}" --format=zip -- "${PROJECT_DIST_NAME}"

        COMMENT "Packaging distribution ${PROJECT_DIST_FILE}"
    )
endif()
