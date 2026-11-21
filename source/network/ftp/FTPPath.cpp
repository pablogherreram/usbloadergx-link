/* SD-only path resolver for USB Loader GX Link. No NAND/ISFS APIs are used. */
#include "FTPPath.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace {
static const char *kRoot = "sd:/";
static bool appendComponent(char *out, size_t outSize, const char *part, size_t len)
{
    size_t used = strlen(out);
    if (len == 0 || (len == 1 && part[0] == '.')) return true;
    if (len == 2 && part[0] == '.' && part[1] == '.') {
        if (used > 1) {
            char *slash = strrchr(out, '/');
            if (slash == out) out[1] = 0;
            else if (slash) *slash = 0;
        }
        return true;
    }
    if (used + (used > 1 ? 1 : 0) + len + 1 > outSize) return false;
    if (used > 1) strcat(out, "/");
    strncat(out, part, len);
    return true;
}
}

bool FTPPath::IsForbiddenDevicePath(const char *input)
{
    if (!input) return false;
    while (*input == ' ' || *input == '\t') ++input;
    const char *colon = strchr(input, ':');
    if (!colon) return false;
    for (const char *p = input; p < colon; ++p)
        if (*p == '/' || *p == '\\') return false;
    return true; // Reject every explicit device prefix, including sd: from the client.
}

bool FTPPath::Normalize(const char *cwd, const char *input, char *out, size_t outSize)
{
    if (!out || outSize < 2 || IsForbiddenDevicePath(input)) return false;
    strcpy(out, "/");
    char combined[1024];
    const char *request = (input && *input) ? input : ".";
    if (request[0] == '/' || request[0] == '\\')
        snprintf(combined, sizeof(combined), "%s", request);
    else
        snprintf(combined, sizeof(combined), "%s%s%s", (cwd && *cwd) ? cwd : "/", ((cwd && strlen(cwd) > 1) ? "/" : ""), request);
    combined[sizeof(combined)-1] = 0;

    const char *p = combined;
    while (*p) {
        while (*p == '/' || *p == '\\') ++p;
        const char *start = p;
        while (*p && *p != '/' && *p != '\\') {
            unsigned char c = (unsigned char)*p;
            if (c < 32 || c == 127) return false;
            ++p;
        }
        if (!appendComponent(out, outSize, start, (size_t)(p - start))) return false;
    }
    return true;
}

bool FTPPath::Resolve(const char *cwd, const char *input, char *physical, size_t physicalSize, char *virtualOut, size_t virtualSize)
{
    char normalized[768];
    if (!Normalize(cwd, input, normalized, sizeof(normalized))) return false;
    int written = snprintf(physical, physicalSize, "%s%s", kRoot, normalized[0] == '/' ? normalized + 1 : normalized);
    if (written < 0 || (size_t)written >= physicalSize) return false;
    if (virtualOut && virtualSize) {
        if (strlen(normalized) + 1 > virtualSize) return false;
        strcpy(virtualOut, normalized);
    }
    return strncmp(physical, kRoot, strlen(kRoot)) == 0;
}
