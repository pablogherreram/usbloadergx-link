#ifndef GX_LINK_FTP_PATH_H
#define GX_LINK_FTP_PATH_H

#include <stddef.h>

namespace FTPPath {
    bool Normalize(const char *cwd, const char *input, char *virtualPath, size_t virtualSize);
    bool Resolve(const char *cwd, const char *input, char *physicalPath, size_t physicalSize, char *virtualPath = 0, size_t virtualSize = 0);
    bool IsForbiddenDevicePath(const char *input);
}

#endif
