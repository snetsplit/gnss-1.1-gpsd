#ifndef ANDROID_HARDWARE_GNSS_LOCATION_LISTENER_H
#define ANDROID_HARDWARE_GNSS_LOCATION_LISTENER_H

#include <android/hardware/gnss/1.0/types.h>

namespace android {
namespace hardware {
namespace gnss {
namespace V1_1 {
namespace implementation {

using GnssLocation = V1_0::GnssLocation;

class IGnssLocationListener {
public:
    virtual ~IGnssLocationListener() = default;
    virtual void onLocationUpdated(const GnssLocation& location) = 0;
};

}  // namespace implementation
}  // namespace V1_1
}  // namespace gnss
}  // namespace hardware
}  // namespace android

#endif
