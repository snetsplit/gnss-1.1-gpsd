#include "GpsdMonitor.h"
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sstream>
#include <regex>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <fstream>
#include <arpa/inet.h>
#include <fcntl.h>
#include <android/hardware/gnss/1.0/types.h>
#include <android/hardware/gnss/1.0/IGnssCallback.h>
#include <android-base/properties.h>


#include "nlohmann/json.hpp"


using json = nlohmann::json;
using GnssSvInfo = android::hardware::gnss::V1_0::IGnssCallback::GnssSvInfo;
using GnssSvStatus = android::hardware::gnss::V1_0::IGnssCallback::GnssSvStatus;
using GnssLocation = android::hardware::gnss::V1_0::GnssLocation;
using GnssLocationFlags = android::hardware::gnss::V1_0::GnssLocationFlags;
using GnssConstellationType = android::hardware::gnss::V1_0::GnssConstellationType;
using GnssSvFlags = android::hardware::gnss::V1_0::IGnssCallback::GnssSvFlags;


#define LOG_TAG "GpsdMonitor"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

GpsdMonitor& GpsdMonitor::getInstance() {
    static GpsdMonitor instance;
    return instance;
}

GpsdMonitor::GpsdMonitor() : mRunning(false) {}
GpsdMonitor::~GpsdMonitor() { stop(); }

void GpsdMonitor::start() {
        LOGI("GpsdMonitor is starting");
    if (mRunning.load()) return;
    mRunning = true;
    mThread = std::thread(&GpsdMonitor::monitorLoop, this);
}

void GpsdMonitor::stop() {
        LOGI("GpsdMonitor is stopping");
    if (!mRunning.load()) return;
    mRunning = false;
    if (mThread.joinable()) mThread.join();
}

GnssLocation GpsdMonitor::getGnssLocation() {
    std::lock_guard<std::mutex> lock(mMutex);
    return mGnssLocation;
}

GnssSvStatus GpsdMonitor::getGnssSvStatus() {
    std::lock_guard<std::mutex> lock(mMutex);
    return mSvStatus;
}

void GpsdMonitor::getGpsdServerConnectionInfo() {
    mGpsdServerAddress = android::base::GetProperty("persist.sys.gnss.gpsd.server", "192.168.240.1");
    mGpsdServerPort = android::base::GetIntProperty("persist.sys.gnss.gpsd.port", 2947);
}


void GpsdMonitor::monitorLoop() {

//    const char* GPSD_DEV = "/dev/ttyGPSD";

    const std::string GPSD_DEV = android::base::GetProperty("persist.sys.gnss.gpsd.chardevice", "/dev/ttyGPSD");

    while (mRunning) {
        int fd = open(GPSD_DEV.c_str(), O_RDONLY | O_NOCTTY);
        if (fd < 0) {
            LOGE("Failed to open %s: %s", GPSD_DEV.c_str(), strerror(errno));
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        LOGI("Opened GPS device %s", GPSD_DEV.c_str());

        char buffer[1024];
        std::string partialLine;

        while (mRunning) {
            ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
            if (n <= 0) {
                if (n < 0) {
                    LOGE("Read error on %s: %s", GPSD_DEV.c_str(), strerror(errno));
                } else {
                    LOGE("GPS device closed (EOF), reopening...");
                }
                break;  // exit inner loop → reopen device
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

/*
void GpsdMonitor::monitorLoopOld() {
    getGpsdServerConnectionInfo();
    while (mRunning) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            LOGE("Failed to create socket");
            std::this_thread::sleep_for(std::chrono::seconds(5));
            getGpsdServerConnectionInfo() ;
            continue;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(mGpsdServerPort);

        if (inet_pton(AF_INET, mGpsdServerAddress.c_str(), &addr.sin_addr) != 1) {
            LOGE("Invalid GPSD server address: %s", mGpsdServerAddress.c_str());
            close(sock);
            getGpsdServerConnectionInfo() ;
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
}

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            LOGE("Failed to connect to GPSD on %s:%d, retrying in 2 seconds...", mGpsdServerAddress.c_str(), mGpsdServerPort);
            close(sock);
            getGpsdServerConnectionInfo() ;
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        LOGI("Connected to GPSD on %s:%d", mGpsdServerAddress.c_str(), mGpsdServerPort);

        // Enable JSON streaming from GPSD
        const char* WATCH_CMD = "?WATCH={\"enable\":true,\"json\":true}\n";
        send(sock, WATCH_CMD, strlen(WATCH_CMD), 0);

        char buffer[1024];
        std::string partialLine;

        while (mRunning) {
            ssize_t n = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (n <= 0) {
                LOGE("GPSD connection lost, reconnecting...");
                break;  // exit inner loop to reconnect
            }

            buffer[n] = '\0';
            partialLine += buffer;

            size_t pos;
            while ((pos = partialLine.find('\n')) != std::string::npos) {
                std::string line = partialLine.substr(0, pos);
                partialLine.erase(0, pos + 1);
                parseLine(line);
            }
        }

        close(sock);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}*/


void GpsdMonitor::parseLine(const std::string& line) {
    nlohmann::json jsonRecord = json::parse(line, nullptr, false);

    if(jsonRecord.is_discarded() || !jsonRecord.contains("class")) {
        LOGE("JSON parse is a failer, or lacks class: %s", line.c_str());
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


void GpsdMonitor::processSatelliteInfo(nlohmann::json jsonRecord) {
    std::lock_guard<std::mutex> lock(mMutex);

    if (jsonRecord["satellites"].size() < 1) {

        LOGI("woo=1");
        if (mGpsSatelliteTimeout == 0) {
        LOGI("woo=11");
            mGpsSatelliteTimeout = std::time(nullptr) + 30;
            return;
        } else if (std::time(nullptr) < mGpsSatelliteTimeout) {
        LOGI("woo=12");
            return;
        } else {
        LOGI("woo=13");
            mSvStatus = GnssSvStatus{};
            mSvStatus.numSvs = 0;
            mGpsSatelliteTimeout = 0;
            return;
        }
    } else {
        mGpsSatelliteTimeout = 0;
    }


    mSkyInfo.devicePath = jsonRecord.value("device", "");
    mSkyInfo.measurementTimeUtc = jsonRecord.value("time", "");
    mSkyInfo.longitudeDop = jsonRecord.value("xdop", 0.0);
    mSkyInfo.latitudeDop = jsonRecord.value("ydop", 0.0);
    mSkyInfo.verticalDop = jsonRecord.value("vdop", 0.0);
    mSkyInfo.timeDop = jsonRecord.value("tdop", 0.0);
    mSkyInfo.horizontalDop = jsonRecord.value("hdop", 0.0);
    mSkyInfo.threeDimensionalPositionDop  = jsonRecord.value("pdop", 0.0);
    mSkyInfo.geometricDop = jsonRecord.value("gdop", 0.0);
    mSkyInfo.satellitesUsedCount = jsonRecord.value("uSat", 0);


    mSvStatus = GnssSvStatus{};
    mSvStatus.numSvs = static_cast<int>(jsonRecord["satellites"].size());

    LOGI("mSvStatus.numSvs=%d", mSvStatus.numSvs);

    // Populate GnssSvInfo for each satellite
    for (int i = 0; i < mSvStatus.numSvs; ++i) {
        auto& satellite = jsonRecord["satellites"][i];
        GnssSvInfo info;
        //satelliteId
        info.svid = satellite.value("PRN", 0);
        switch (satellite.value("gnssid",69420)) {
            case 0: info.constellation = GnssConstellationType::GPS; break;
            case 1: info.constellation = GnssConstellationType::SBAS; break;
            case 2: info.constellation = GnssConstellationType::GALILEO; break;
            case 3: info.constellation = GnssConstellationType::BEIDOU; break;
            case 5: info.constellation = GnssConstellationType::QZSS; break;
            case 6: info.constellation = GnssConstellationType::GLONASS; break;
            default: info.constellation = GnssConstellationType::UNKNOWN;
        };
        info.azimuthDegrees = satellite.value("az", 0);
        info.elevationDegrees = satellite.value("el", 0);
        //carrier To Noise Density DbHz
        info.cN0Dbhz = satellite.value("ss", 0.0);      // dB-Hz
        info.svFlag = GnssSvFlags::NONE | (satellite.value("used", false) ? GnssSvFlags::USED_IN_FIX : GnssSvFlags::NONE);

        mSvStatus.gnssSvList[i] = info;
        LOGI("info.svid=%d", info.svid);
    }
}


int64_t parseTime(std::string gpsdTimeStr) {
    std::tm tm = {};
    std::istringstream ss(gpsdTimeStr);

    // parse UTC time
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    time_t t = timegm(&tm);  // seconds since epoch in UTC

    // optional: parse fractional seconds (nanoseconds)
    size_t dotPos = gpsdTimeStr.find('.');
    int64_t nanoseconds = 0;
    if (dotPos != std::string::npos) {
        std::string frac = gpsdTimeStr.substr(dotPos + 1, 9); // up to 9 digits
        while (frac.size() < 9) frac += '0';
        nanoseconds = std::stoll(frac);
    }

    return static_cast<int64_t>(t);
}

void GpsdMonitor::processVelocity(nlohmann::json jsonRecord){
    std::lock_guard<std::mutex> lock(mMutex);
    GnssLocation location = GnssLocationStarter;
    uint16_t flags = startLocationFlags;
    if (jsonRecord.contains("lat") && jsonRecord.contains("lon")) {

        location.latitudeDegrees  = jsonRecord.value("lat", 0.0);
        location.longitudeDegrees = jsonRecord.value("lon", 0.0);
        location.horizontalAccuracyMeters = jsonRecord.value("eph", 1.0);

    } else {
        return;
    }

    if (jsonRecord.contains("alt") && jsonRecord.contains("epv")) {

        flags = static_cast<uint16_t>( flags | GnssLocationFlags::HAS_ALTITUDE | GnssLocationFlags::HAS_VERTICAL_ACCURACY);

        location.verticalAccuracyMeters = jsonRecord.value("epv", 0.0);
        location.altitudeMeters = jsonRecord.value("alt", 0.0);
    }

    if (jsonRecord.contains("speed")) {
        flags |= GnssLocationFlags::HAS_SPEED;
        location.speedMetersPerSec = jsonRecord.value("speed",0.0);
    }

    if (jsonRecord.contains("track")) {
        flags |= GnssLocationFlags::HAS_BEARING;
        location.bearingDegrees =  jsonRecord.value("track", 0.0);
    }

    location.timestamp = static_cast<int64_t>(time(NULL)) * 1000l;
    location.gnssLocationFlags = flags;

    mGnssLocation = location;
}
