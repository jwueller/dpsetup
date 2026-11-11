#pragma once

#include <winresrc.h>
#include "meta.h"

#if PROJECT_PREVIEW
#define PROJECT_VERSIONINFO_FILEFLAGS_PRERELEASE    VS_FF_PRERELEASE
#else
#define PROJECT_VERSIONINFO_FILEFLAGS_PRERELEASE    0
#endif

#if _DEBUG
#define PROJECT_VERSIONINFO_FILEFLAGS_DEBUG         VS_FF_DEBUG
#else
#define PROJECT_VERSIONINFO_FILEFLAGS_DEBUG         0
#endif

#define PROJECT_VERSIONINFO_FILEFLAGS               (PROJECT_VERSIONINFO_FILEFLAGS_PRERELEASE | PROJECT_VERSIONINFO_FILEFLAGS_DEBUG)

#define PROJECT_VERSIONINFO_FILEVERSION             PROJECT_VERSION_MAJOR, \
                                                    PROJECT_VERSION_MINOR, \
                                                    PROJECT_VERSION_PATCH, \
                                                    PROJECT_VERSION_TWEAK

#define PROJECT_VERSIONINFO_PRODUCTVERSION          PROJECT_VERSION_MAJOR, \
                                                    PROJECT_VERSION_MINOR, \
                                                    PROJECT_VERSION_PATCH, \
                                                    PROJECT_VERSION_TWEAK
