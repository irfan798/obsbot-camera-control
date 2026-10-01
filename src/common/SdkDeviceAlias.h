#ifndef SDKDEVICEALIAS_H
#define SDKDEVICEALIAS_H

#include <cstdint>
#include <string>

/**
 * @brief Lets the bundled OBSBOT SDK drive cameras released after it
 *
 * SDK v1.0.2 maps USB product ids 0xFEF0-0xFF01 to models. Any other id gets
 * the legacy V0 wire protocol, which newer cameras ignore, so the SDK never
 * finishes initialising them ("error occurred in getDevUuidR, ret_val = -3").
 *
 * The SDK learns the id by fopen()ing /sys/class/video4linux/videoN/device/modalias.
 * SdkDeviceAlias.cpp interposes fopen() and, for the cameras in its alias table,
 * serves that file with the id of the closest supported model, so the SDK talks
 * the V3 protocol these cameras understand. Everything else reads the real file.
 */
namespace SdkDeviceAlias {

constexpr uint16_t kObsbotVendorId = 0x3564;
constexpr uint16_t kMeetFlipProductId = 0xff06;

// Real USB product id of the camera behind a /dev/videoN node, bypassing the alias.
// Returns 0 for non-OBSBOT devices or when the id can't be read.
uint16_t realProductId(const std::string &videoDevPath);

} // namespace SdkDeviceAlias

#endif // SDKDEVICEALIAS_H
