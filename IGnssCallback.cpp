#include <android/hardware/gnss/1.1/IGnss.h>
#include <android/hardware/gnss/1.1/IGnssCallback.h>
#include <android/hidl/Status.h>
#include <android/hidl/string.h>
#include <iostream>

using android::hardware::gnss::V1_1::IGnss;
using android::hardware::gnss::V1_1::IGnssCallback;
using android::hardware::Return;
using android::hardware::hidl_string;
using android::sp;

struct MyGnssCallback : public IGnssCallback {
    Return<void> nmeaReceived(int64_t timestamp, const hidl_string& nmea) override {
        std::cout << "NMEA received @ " << timestamp << ": " << nmea.c_str() << "\n";
        return Return<void>();
    }

    Return<void> gnssLocationCb(const IGnssCallback::GnssLocation& location) override {
        std::cout << "Location lat=" << location.latitudeDegrees
                  << " lon=" << location.longitudeDegrees << "\n";
        return Return<void>();
    }

    Return<void> gnssSetCapabilitesCb(uint32_t /*capabilities*/) override { return Return<void>(); }
    Return<void> gnssSetSystemInfoCb(const IGnssCallback::GnssSystemInfo&) override { return Return<void>(); }
    Return<void> gnssNameCb(const hidl_string&) override { return Return<void>(); }
    Return<void> gnssStatusCb(IGnssCallback::GnssStatus) override { return Return<void>(); }
    Return<void> gnssSvStatusCb(IGnssCallback::GnssSvStatus) override { return Return<void>(); }
};
