#pragma once

#include <3ds.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

Result createFolderRecursively(const char * path, FS_Archive archive);
Result openFile(const char * path, FS_Archive archive, Handle * handle, bool write);
Result deleteFile(const char * path, FS_Archive archive);

#ifdef __cplusplus
}
#endif
