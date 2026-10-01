#include "SdkDeviceAlias.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

struct Alias {
    uint16_t productId;     // id the camera reports
    uint16_t sdkProductId;  // id the SDK is shown
};

// Meet Flip -> Meet 2. Verified on Flip firmware 4.5.3.3: auto-framing, HDR,
// face AE/focus and pan/tilt work through the Meet 2 commands; zoom does not
// (CameraController routes it through V4L2 instead).
constexpr Alias kAliases[] = {
    {SdkDeviceAlias::kMeetFlipProductId, 0xfefb},
};

bool isVideo4LinuxModalias(const char *path)
{
    static const char kPrefix[] = "/sys/class/video4linux/";
    static const char kSuffix[] = "/device/modalias";
    const size_t len = strlen(path);
    return len > sizeof kSuffix - 1
        && strncmp(path, kPrefix, sizeof kPrefix - 1) == 0
        && strcmp(path + len - (sizeof kSuffix - 1), kSuffix) == 0;
}

// Reads a small sysfs file without going through the fopen() interposer.
bool readSysfs(const char *path, char *buf, size_t size)
{
    int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    ssize_t n = ::read(fd, buf, size - 1);
    ::close(fd);
    if (n <= 0)
        return false;
    buf[n] = '\0';
    return true;
}

// A USB modalias starts "usb:vVVVVpPPPP": vendor id at offset 5, product id at offset 10.
bool parseIds(const char *modalias, uint16_t &vendorId, uint16_t &productId)
{
    if (strlen(modalias) < 14 || strncmp(modalias, "usb:v", 5) != 0 || modalias[9] != 'p')
        return false;
    char hex[5] = {};
    memcpy(hex, modalias + 5, 4);
    vendorId = static_cast<uint16_t>(strtoul(hex, nullptr, 16));
    memcpy(hex, modalias + 10, 4);
    productId = static_cast<uint16_t>(strtoul(hex, nullptr, 16));
    return true;
}

// Returns the aliased modalias as a read-only stream, or nullptr to read the real file.
FILE *openAliasedModalias(const char *path, const char *mode)
{
    if (!path || !mode || mode[0] != 'r' || !isVideo4LinuxModalias(path))
        return nullptr;

    char modalias[256];
    uint16_t vendorId = 0, productId = 0;
    if (!readSysfs(path, modalias, sizeof modalias) || !parseIds(modalias, vendorId, productId)
        || vendorId != SdkDeviceAlias::kObsbotVendorId)
        return nullptr;

    for (const Alias &alias : kAliases) {
        if (alias.productId != productId)
            continue;

        char hex[5];
        snprintf(hex, sizeof hex, "%04X", alias.sdkProductId);
        memcpy(modalias + 10, hex, 4);

        int fd = memfd_create("modalias", MFD_CLOEXEC);
        if (fd < 0)
            return nullptr;
        const size_t len = strlen(modalias);
        if (write(fd, modalias, len) != static_cast<ssize_t>(len) || lseek(fd, 0, SEEK_SET) != 0) {
            ::close(fd);
            return nullptr;
        }
        FILE *stream = fdopen(fd, "r");
        if (!stream)
            ::close(fd);
        return stream;
    }
    return nullptr;
}

using FopenFn = FILE *(*)(const char *, const char *);

} // namespace

uint16_t SdkDeviceAlias::realProductId(const std::string &videoDevPath)
{
    const size_t slash = videoDevPath.rfind('/');
    const std::string node = videoDevPath.substr(slash == std::string::npos ? 0 : slash + 1);
    const std::string path = "/sys/class/video4linux/" + node + "/device/modalias";

    char modalias[256];
    uint16_t vendorId = 0, productId = 0;
    if (node.empty() || !readSysfs(path.c_str(), modalias, sizeof modalias)
        || !parseIds(modalias, vendorId, productId) || vendorId != kObsbotVendorId)
        return 0;
    return productId;
}

// Defined in the executable, these take precedence over libc for libdev.so too.
extern "C" FILE *fopen(const char *path, const char *mode)
{
    static const auto real = reinterpret_cast<FopenFn>(dlsym(RTLD_NEXT, "fopen"));
    if (FILE *aliased = openAliasedModalias(path, mode))
        return aliased;
    return real(path, mode);
}

extern "C" FILE *fopen64(const char *path, const char *mode)
{
    static const auto real = reinterpret_cast<FopenFn>(dlsym(RTLD_NEXT, "fopen64"));
    if (FILE *aliased = openAliasedModalias(path, mode))
        return aliased;
    return real(path, mode);
}
