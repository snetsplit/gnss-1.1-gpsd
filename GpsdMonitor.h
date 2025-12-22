#pragma once

#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <map>
#include <android/log.h>
#include <android/hardware/gnss/1.0/types.h>
#include <android/hardware/gnss/1.0/IGnssCallback.h>

#include "nlohmann/json.hpp"
#include <chrono>

using json = nlohmann::json;
using GnssSvInfo = android::hardware::gnss::V1_0::IGnssCallback::GnssSvInfo;
using GnssSvStatus = android::hardware::gnss::V1_0::IGnssCallback::GnssSvStatus;
using GnssLocation = android::hardware::gnss::V1_0::GnssLocation;
using GnssLocationFlags = android::hardware::gnss::V1_0::GnssLocationFlags;
using GnssConstellationType = android::hardware::gnss::V1_0::GnssConstellationType;
using GnssSvFlags = android::hardware::gnss::V1_0::IGnssCallback::GnssSvFlags;

struct SkyInfo {
    std::string devicePath;                     // Device that produced this SKY record
    std::string measurementTimeUtc;             // ISO8601 timestamp of measurement

    double longitudeDop;                        // Longitude DOP (XDOP)
    double latitudeDop;                         // Latitude DOP (YDOP)
    double verticalDop;                         // Vertical DOP (VDOP)
    double timeDop;                             // Time DOP (TDOP)
    double horizontalDop;                       // Horizontal DOP (HDOP)
    double threeDimensionalPositionDop;         // Position DOP (PDOP)
    double geometricDop;                        // Overall geometry DOP (GDOP)

    int satellitesUsedCount;                    // Number of satellites used in solution (uSat)

    std::vector<GnssSvInfo> satellites;      // Complete list of visible satellites
};


struct PositionInfo {
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    double speed = 0.0;
    int mode = 0; // 1=No fix, 2=2D fix, 3=3D fix
};

class GpsdMonitor {
public:
    static GpsdMonitor& getInstance();

    void start();
    void stop();

    GnssLocation getGnssLocation();
    GnssSvStatus getGnssSvStatus();

private:
    GpsdMonitor();
    ~GpsdMonitor();

    void monitorLoop();
    void getGpsdServerConnectionInfo();
    void parseLine(const std::string& line);
    void processSatelliteInfo(nlohmann::json jsonRecord);
    void processVelocity(nlohmann::json jsonRecord);
    std::atomic<bool> mRunning;
    std::thread mThread;
    std::mutex mMutex;

    SkyInfo mSkyInfo;
    GnssSvStatus mSvStatus{};

    std::string mGpsdServerAddress = "192.168.240.1";
    int mGpsdServerPort =  2947;

    const uint16_t startLocationFlags = static_cast<uint16_t>(GnssLocationFlags::HAS_LAT_LONG | GnssLocationFlags::HAS_HORIZONTAL_ACCURACY);
    const GnssLocation GnssLocationStarter = {
        .gnssLocationFlags = startLocationFlags,
        .latitudeDegrees = 41.94394,
        .longitudeDegrees = -85.63249,
        .horizontalAccuracyMeters = 30.0,
        .timestamp = static_cast<int64_t>(time(NULL)) * 1000l,
    };

    GnssLocation mGnssLocation = GnssLocationStarter;
    time_t mGpsSatelliteTimeout = 0;
};
