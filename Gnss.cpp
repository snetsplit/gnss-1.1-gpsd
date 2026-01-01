#define LOG_TAG "GnssGpsd"

#include "Constants.h"
#include "Gnss.h"
#include "GnssDebug.h"
#include "GnssMeasurement.h"
#include "GpsdMonitor.h"
#include "Utils.h"

#include <android-base/properties.h>
#include <android/hardware/gnss/1.0/IGnssCallback.h>
#include <android/hardware/gnss/1.0/types.h>
#include <arpa/inet.h>
#include <chrono>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <log/log.h>
#include <netdb.h>
#include <regex>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "nlohmann/json.hpp"

namespace android {
namespace hardware {
namespace gnss {
namespace V1_1 {
namespace implementation {

using ::android::hardware::gnss::common::Utils;
using GnssSvFlags = IGnssCallback::GnssSvFlags;
using namespace ::android::hardware::gnss::common;
using json = nlohmann::json;

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

const uint32_t MIN_INTERVAL_MILLIS = 100;
sp<::android::hardware::gnss::V1_1::IGnssCallback> Gnss::sGnssCallback = nullptr;

Gnss::Gnss() : mMinIntervalMs(1000), mGnssConfiguration{new GnssConfiguration()} {}

Gnss::~Gnss() {
    stop();
}

// Methods from ::android::hardware::gnss::V1_0::IGnss follow.
Return<bool> Gnss::setCallback(const sp<::android::hardware::gnss::V1_0::IGnssCallback>&) {
    // Mock handles only new callback (see setCallback1_1) coming from Android P+
    return false;
}


Return<bool> Gnss::start() {
    if (mIsActive) {
        ALOGW("GnssGpsd has started. Restarting...");
        stop();
    }

    ALOGW("GnssGpsd starting");


    return true;
}

Return<bool> Gnss::stop() {
    mIsActive = false;
    if (mThread.joinable()) {
        mThread.join();
    }

    return true;
}

Return<void> Gnss::cleanup() {
    // TODO implement
    return Void();
}

Return<bool> Gnss::injectTime(int64_t, int64_t, int32_t) {
    // TODO implement
    return bool{};
}

Return<bool> Gnss::injectLocation(double, double, float) {
    // TODO implement
    return bool{};
}

Return<void> Gnss::deleteAidingData(::android::hardware::gnss::V1_0::IGnss::GnssAidingData) {
    return Void();
}

Return<bool> Gnss::setPositionMode(::android::hardware::gnss::V1_0::IGnss::GnssPositionMode,
                                   ::android::hardware::gnss::V1_0::IGnss::GnssPositionRecurrence,
                                   uint32_t, uint32_t, uint32_t) {
    // TODO implement
    return bool{};
}

Return<sp<::android::hardware::gnss::V1_0::IAGnssRil>> Gnss::getExtensionAGnssRil() {
    // TODO implement
    return ::android::sp<::android::hardware::gnss::V1_0::IAGnssRil>{};
}

Return<sp<::android::hardware::gnss::V1_0::IGnssGeofencing>> Gnss::getExtensionGnssGeofencing() {
    // TODO implement
    return ::android::sp<::android::hardware::gnss::V1_0::IGnssGeofencing>{};
}

Return<sp<::android::hardware::gnss::V1_0::IAGnss>> Gnss::getExtensionAGnss() {
    // TODO implement
    return ::android::sp<::android::hardware::gnss::V1_0::IAGnss>{};
}

Return<sp<::android::hardware::gnss::V1_0::IGnssNi>> Gnss::getExtensionGnssNi() {
    // TODO implement
    return ::android::sp<::android::hardware::gnss::V1_0::IGnssNi>{};
}

Return<sp<::android::hardware::gnss::V1_0::IGnssMeasurement>> Gnss::getExtensionGnssMeasurement() {
    // TODO implement
    return new GnssMeasurement();
}

Return<sp<::android::hardware::gnss::V1_0::IGnssNavigationMessage>>
Gnss::getExtensionGnssNavigationMessage() {
    // TODO implement
    return ::android::sp<::android::hardware::gnss::V1_0::IGnssNavigationMessage>{};
}

Return<sp<::android::hardware::gnss::V1_0::IGnssXtra>> Gnss::getExtensionXtra() {
    // TODO implement
    return ::android::sp<::android::hardware::gnss::V1_0::IGnssXtra>{};
}

Return<sp<::android::hardware::gnss::V1_0::IGnssConfiguration>>
Gnss::getExtensionGnssConfiguration() {
    // TODO implement
    return new GnssConfiguration();
}

Return<sp<::android::hardware::gnss::V1_0::IGnssDebug>> Gnss::getExtensionGnssDebug() {
    return new GnssDebug();
}

Return<sp<::android::hardware::gnss::V1_0::IGnssBatching>> Gnss::getExtensionGnssBatching() {
    // TODO implement
    return ::android::sp<::android::hardware::gnss::V1_0::IGnssBatching>{};
}


void Gnss::monitorLoop() {
    const std::string FIFO_PATH = android::base::GetProperty("persist.sys.gnss.gpsd.pipe", "/data/system/gps.pipe");

    LOGI("Using GPS FIFO %s from prop \"persist.sys.gnss.gpsd.pipe\"", FIFO_PATH.c_str());

    while (mIsActive) {

        /* Ensure FIFO exists */
        struct stat st;
        if (stat(FIFO_PATH.c_str(), &st) != 0) {
            if (errno == ENOENT) {
                LOGI("FIFO %s does not exist, creating it",
                     FIFO_PATH.c_str());

                if (mkfifo(FIFO_PATH.c_str(), 0666) != 0) {
                    LOGE("mkfifo(%s) failed: %s",
                         FIFO_PATH.c_str(), strerror(errno));
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    continue;
                }
            } else {
                LOGE("stat(%s) failed: %s",
                     FIFO_PATH.c_str(), strerror(errno));
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
        } else if (!S_ISFIFO(st.st_mode)) {
            LOGE("%s exists but is not a FIFO", FIFO_PATH.c_str());
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        /* Open FIFO (blocks until writer connects) */
        int fd = open(FIFO_PATH.c_str(), O_RDONLY);
        if (fd < 0) {
            LOGE("Failed to open FIFO %s: %s",
                 FIFO_PATH.c_str(), strerror(errno));
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        LOGI("Opened FIFO %s", FIFO_PATH.c_str());

        char buffer[1024];
        std::string partialLine;

        while (mIsActive) {
            ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
            if (n <= 0) {
                if (n < 0) {
                    LOGE("Read error on FIFO %s: %s",
                         FIFO_PATH.c_str(), strerror(errno));
                } else {
                    LOGI("FIFO %s closed (EOF), reopening...",
                         FIFO_PATH.c_str());
                }
                break;
            }

            buffer[n] = '\0';
            partialLine.append(buffer, n);

            size_t pos;
            while ((pos = partialLine.find('\n')) != std::string::npos) {
                std::string line = partialLine.substr(0, pos);
                partialLine.erase(0, pos + 1);

                if (!line.empty()) {
                    parseLine(line);
                }
            }
        }

        close(fd);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}


void Gnss::parseLine(const std::string& line) {
    nlohmann::json jsonRecord = json::parse(line, nullptr, false);

    if(jsonRecord.is_discarded() || !jsonRecord.contains("class")) {
        LOGE("JSON parse is a failure, or lacks class: %s", line.c_str());
        return;
    }


    if (jsonRecord["class"] == "SKY") {
        processSatelliteInfo(jsonRecord);
    } else if (jsonRecord["class"] == "TPV") {
        processVelocity(jsonRecord);
    } else {
        LOGE("Unknown class: %s", line.c_str());
    }
}



void Gnss::processSatelliteInfo(nlohmann::json jsonRecord) {
    std::lock_guard<std::mutex> lock(mMutex);

    GnssSvStatus svStatus = GnssSvStatus{};
    svStatus.numSvs = static_cast<int>(jsonRecord["satellites"].size());

    uint32_t index = 0;

    for (const auto& satellite : jsonRecord["satellites"]) {
        if (index >= svStatus.numSvs) {
            break; // safety guard
        }

        GnssSvInfo gnssSvInfo{};
        android::hardware::hidl_bitfield<GnssSvFlags> flags = 0;

        gnssSvInfo.svid = satellite.value("PRN", 0);
        switch (satellite.value("gnssid", -1)) {
            case 0: gnssSvInfo.constellation = GnssConstellationType::GPS; break;
            case 1: gnssSvInfo.constellation = GnssConstellationType::SBAS; break;
            case 2: gnssSvInfo.constellation = GnssConstellationType::GALILEO; break;
            case 3: gnssSvInfo.constellation = GnssConstellationType::BEIDOU; break;
            case 5: gnssSvInfo.constellation = GnssConstellationType::QZSS; break;
            case 6: gnssSvInfo.constellation = GnssConstellationType::GLONASS; break;
            default:
                gnssSvInfo.constellation = GnssConstellationType::UNKNOWN;
                break;
        }

        gnssSvInfo.azimuthDegrees   = satellite.value("az", 0.0f);
        gnssSvInfo.elevationDegrees = satellite.value("el", 0.0f);

        // Signal strength (mandatory)
        gnssSvInfo.cN0Dbhz = satellite.value("ss", 0.0f);

        // Ephemeris
        if (satellite.value("ephemeris", false)) {
            flags |= GnssSvFlags::HAS_EPHEMERIS_DATA;
        }

        // Almanac
        if (satellite.value("almanac", false)) {
            flags |= GnssSvFlags::HAS_ALMANAC_DATA;
        }

        // Used in fix (only if fix exists)
        if (hasFix && satellite.value("used", false)) {
            flags |= GnssSvFlags::USED_IN_FIX;
        }

        // Carrier frequency
        if (satellite.contains("freq")) {
            float freqHz = satellite.value("freq", 0.0);
            if (freqHz > 0.0) {
                gnssSvInfo.carrierFrequencyHz = freqHz;
                flags |= GnssSvFlags::HAS_CARRIER_FREQUENCY;
            }
        }

        gnssSvInfo.svFlag = flags;
        svStatus.gnssSvList[index++] = gnssSvInfo;
    }

    this->reportSvStatus(svStatus);
    mSvStatus = svStatus;
}

void Gnss::processVelocity(nlohmann::json jsonRecord){
    std::lock_guard<std::mutex> lock(mMutex);
    GnssLocation location = GnssLocationStarter;
    uint16_t flags = startLocationFlags;
    if (jsonRecord.contains("lat") && jsonRecord.contains("lon")) {

        location.latitudeDegrees  = jsonRecord.value("lat", 0.0);
        location.longitudeDegrees = jsonRecord.value("lon", 0.0);
        location.horizontalAccuracyMeters = jsonRecord.value("eph", 1.0);
        hasFix = true;

    } else {
        hasFix = false;
        return;
    }

    if (jsonRecord.contains("alt")) {

        flags = static_cast<uint16_t>( flags | GnssLocationFlags::HAS_ALTITUDE | GnssLocationFlags::HAS_VERTICAL_ACCURACY);

        location.verticalAccuracyMeters = jsonRecord.value("epv", 2 * jsonRecord.value("eph", 1.0));
        location.altitudeMeters = jsonRecord.value("alt", 0.0);
    }

    if (jsonRecord.contains("speed")) {
        flags |= GnssLocationFlags::HAS_SPEED;
        location.speedMetersPerSec = jsonRecord.value("speed",0.0);
        location.speedAccuracyMetersPerSecond = jsonRecord.value("eps", 0.5);
    }

    if (jsonRecord.contains("track")) {
        flags |= GnssLocationFlags::HAS_BEARING;
        location.bearingDegrees =  jsonRecord.value("track", 0.0);
        location.speedAccuracyMetersPerSecond = jsonRecord.value("epd", 10.0);
    }

    location.timestamp = (jsonRecord.contains("timestamp") && jsonRecord["timestamp"].is_number_integer())
    ? jsonRecord["timestamp"].get<int64_t>() : static_cast<int64_t>(time(NULL)) * 1000LL;

    location.gnssLocationFlags = flags;

   // this->reportLocation(location);

    mGnssLocation = location;
}

// Methods from ::android::hardware::gnss::V1_1::IGnss follow.
Return<bool> Gnss::setCallback_1_1(
    const sp<::android::hardware::gnss::V1_1::IGnssCallback>& callback) {
    if (callback == nullptr) {
        ALOGE("%s: Null callback ignored", __func__);
        return false;
    }

    mIsActive = true;
    mThread = std::thread([this]() {
        monitorLoop();
    });
    ALOGI("GpsdMonitor started");

    sGnssCallback = callback;

    uint32_t capabilities = 0x0;
    auto ret = sGnssCallback->gnssSetCapabilitesCb(capabilities);
    if (!ret.isOk()) {
        ALOGE("%s: Unable to invoke callback", __func__);
    }

    IGnssCallback::GnssSystemInfo gnssInfo = {.yearOfHw = 2018};

    ret = sGnssCallback->gnssSetSystemInfoCb(gnssInfo);
    if (!ret.isOk()) {
        ALOGE("%s: Unable to invoke callback", __func__);
    }

    auto gnssName = "GPSD GNSS Implementation v1.1";
    ret = sGnssCallback->gnssNameCb(gnssName);
    if (!ret.isOk()) {
        ALOGE("%s: Unable to invoke callback", __func__);
    }

    return true;
}

Return<bool> Gnss::setPositionMode_1_1(
    ::android::hardware::gnss::V1_0::IGnss::GnssPositionMode,
    ::android::hardware::gnss::V1_0::IGnss::GnssPositionRecurrence, uint32_t minIntervalMs,
    uint32_t, uint32_t, bool) {
    mMinIntervalMs = (minIntervalMs < MIN_INTERVAL_MILLIS) ? MIN_INTERVAL_MILLIS : minIntervalMs;
    return true;
}

Return<sp<::android::hardware::gnss::V1_1::IGnssConfiguration>>
Gnss::getExtensionGnssConfiguration_1_1() {
    return mGnssConfiguration;
}

Return<sp<::android::hardware::gnss::V1_1::IGnssMeasurement>>
Gnss::getExtensionGnssMeasurement_1_1() {
    // TODO implement
    return new GnssMeasurement();
}

Return<bool> Gnss::injectBestLocation(const GnssLocation&) {
    return true;
}


Return<GnssSvStatus> Gnss::getSvStatus() const {
    std::unique_lock<std::recursive_mutex> lock(mGnssConfiguration->getMutex());

    return mSvStatus;
}

Return<GnssLocation> Gnss::getGnssLocation() const {
    std::unique_lock<std::recursive_mutex> lock(mGnssConfiguration->getMutex());

    return mGnssLocation;
}

Return<void> Gnss::reportLocation(const GnssLocation& location) const {
    std::unique_lock<std::mutex> lock(mMutex);
    if (sGnssCallback == nullptr) {
        ALOGE("%s: sGnssCallback is null.", __func__);
        return Void();
    }

    auto ret = sGnssCallback->gnssLocationCb(location);
    if (!ret.isOk()) {
        ALOGE("%s: gnssLocationCb HIDL transport failed: %s", __func__, ret.description().c_str());
    }

    return Void();
}

Return<void> Gnss::reportSvStatus(const GnssSvStatus& svStatus) const {
    std::unique_lock<std::mutex> lock(mMutex);
    if (sGnssCallback == nullptr) {
        ALOGE("%s: sGnssCallback is null.", __func__);
        return Void();
    }

    auto ret = sGnssCallback->gnssSvStatusCb(svStatus);
    if (!ret.isOk()) {
        ALOGE("%s: gnssLocationCb HIDL transport failed: %s", __func__, ret.description().c_str());
    }

    return Void();
}

}  // namespace implementation
}  // namespace V1_1
}  // namespace gnss
}  // namespace hardware
}  // namespace android
