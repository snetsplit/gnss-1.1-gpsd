#ifndef ANDROID_HARDWARE_GNSS_V1_1_GNSS_H
#define ANDROID_HARDWARE_GNSS_V1_1_GNSS_H

#include <android/hardware/gnss/1.1/IGnss.h>
#include <cstdlib>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include "GnssConfiguration.h"


#include "nlohmann/json.hpp"


namespace android {
namespace hardware {
namespace gnss {
namespace V1_1 {
namespace implementation {

using ::android::hardware::hidl_array;
using ::android::hardware::hidl_memory;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;

using GnssConstellationType = V1_0::GnssConstellationType;
using GnssLocation = V1_0::GnssLocation;
using GnssSvInfo = V1_0::IGnssCallback::GnssSvInfo;
using GnssSvStatus = V1_0::IGnssCallback::GnssSvStatus;

/**
* Unlike the gnss/1.0/default implementation, which is a shim layer to the legacy gps.h, this
* default implementation serves as a mock implementation for emulators
*/

struct Gnss : public IGnss {
	Gnss();
	~Gnss();
	// Methods from ::android::hardware::gnss::V1_0::IGnss follow.
	Return<bool> setCallback(
		const sp<::android::hardware::gnss::V1_0::IGnssCallback>& callback) override;
	Return<bool> start() override;
	Return<bool> stop() override;
	Return<void> cleanup() override;
	Return<bool> injectTime(int64_t timeMs, int64_t timeReferenceMs,
							int32_t uncertaintyMs) override;
	Return<bool> injectLocation(double latitudeDegrees, double longitudeDegrees,
								float accuracyMeters) override;
	Return<void> deleteAidingData(
		::android::hardware::gnss::V1_0::IGnss::GnssAidingData aidingDataFlags) override;
	Return<bool> setPositionMode(
		::android::hardware::gnss::V1_0::IGnss::GnssPositionMode mode,
		::android::hardware::gnss::V1_0::IGnss::GnssPositionRecurrence recurrence,
		uint32_t minIntervalMs, uint32_t preferredAccuracyMeters,
		uint32_t preferredTimeMs) override;
	Return<sp<::android::hardware::gnss::V1_0::IAGnssRil>> getExtensionAGnssRil() override;
	Return<sp<::android::hardware::gnss::V1_0::IGnssGeofencing>> getExtensionGnssGeofencing()
		override;
	Return<sp<::android::hardware::gnss::V1_0::IAGnss>> getExtensionAGnss() override;
	Return<sp<::android::hardware::gnss::V1_0::IGnssNi>> getExtensionGnssNi() override;
	Return<sp<::android::hardware::gnss::V1_0::IGnssMeasurement>> getExtensionGnssMeasurement()
		override;
	Return<sp<::android::hardware::gnss::V1_0::IGnssNavigationMessage>>
	getExtensionGnssNavigationMessage() override;
	Return<sp<::android::hardware::gnss::V1_0::IGnssXtra>> getExtensionXtra() override;
	Return<sp<::android::hardware::gnss::V1_0::IGnssConfiguration>> getExtensionGnssConfiguration()
		override;
	Return<sp<::android::hardware::gnss::V1_0::IGnssDebug>> getExtensionGnssDebug() override;
	Return<sp<::android::hardware::gnss::V1_0::IGnssBatching>> getExtensionGnssBatching() override;

	// Methods from ::android::hardware::gnss::V1_1::IGnss follow.
	Return<bool> setCallback_1_1(
		const sp<::android::hardware::gnss::V1_1::IGnssCallback>& callback) override;
	Return<bool> setPositionMode_1_1(
		::android::hardware::gnss::V1_0::IGnss::GnssPositionMode mode,
		::android::hardware::gnss::V1_0::IGnss::GnssPositionRecurrence recurrence,
		uint32_t minIntervalMs, uint32_t preferredAccuracyMeters, uint32_t preferredTimeMs,
		bool lowPowerMode) override;
	Return<sp<::android::hardware::gnss::V1_1::IGnssConfiguration>>
	getExtensionGnssConfiguration_1_1() override;
	Return<sp<::android::hardware::gnss::V1_1::IGnssMeasurement>> getExtensionGnssMeasurement_1_1()
		override;
	Return<bool> injectBestLocation(
		const ::android::hardware::gnss::V1_0::GnssLocation& location) override;

	// Methods from ::android::hidl::base::V1_0::IBase follow.
private:

	std::string mGpsdServerAddress = "192.168.240.1";
	int mGpsdServerPort =  2947;

	std::atomic<bool> hasFix{false};
	const char* controlFifoPath = "/data/system/gpspipes/valve.pipe";

	const uint16_t startLocationFlags = static_cast<uint16_t>(V1_0::GnssLocationFlags::HAS_LAT_LONG | V1_0::GnssLocationFlags::HAS_HORIZONTAL_ACCURACY);
	const GnssLocation GnssLocationStarter = {
		.gnssLocationFlags = startLocationFlags,
		.latitudeDegrees = 41.94394,
		.longitudeDegrees = -85.63249,
		.horizontalAccuracyMeters = 30.0,
		.timestamp = static_cast<int64_t>(time(NULL)) * 1000l,
	};

	GnssLocation mGnssLocation = GnssLocationStarter;
	GnssSvStatus mSvStatus = GnssSvStatus{};
	time_t mGpsSatelliteTimeout = 0;

	Return<GnssSvStatus> getSvStatus() const;
	Return<GnssLocation> getGnssLocation() const;
	Return<void> reportLocation(const GnssLocation&) const;
	Return<void> reportSvStatus(const GnssSvStatus&) const;

	bool ensureFifoIsUsable(const char* FIFO_PATH);
	void monitorLoop();
	void getGpsdServerConnectionInfo();
	void parseLine(const std::string& line);
	void processSatelliteInfo(nlohmann::json& jsonRecord);
	void processVelocity(nlohmann::json& jsonRecord);
	void fifo_writer(const char* fifo_path, std::condition_variable* queue_cv, std::queue<std::string>* fifo_queue);
	void startControl();
	void updateControl();
	bool ensure_directory_exists(const char *path, mode_t mode);

	void processAltitude(GnssLocation& location, uint16_t& flags, nlohmann::json& jsonRecord);
	void processTrack(GnssLocation& location, uint16_t& flags, nlohmann::json& jsonRecord);
	void sendNmeaSentence(const std::string& nmea);
	void monitorNmea();
	nlohmann::json getJsonSubstring(const std::string& line);
	bool setMillisecondsSinceEpochFromIso8601(const std::string& iso8601, int64_t& milliseconds);

	std::condition_variable mFifoCommandQueueConditionalVariable;
	std::queue<std::string> mFifoCommandQueue;

	static sp<IGnssCallback> sGnssCallback;
	std::atomic<long> mMinIntervalMs;
	sp<GnssConfiguration> mGnssConfiguration;
	std::atomic<bool> mIsActive{false};
	std::atomic<bool> mRunning{true};
	std::atomic<bool> mFifoControl{false};
	std::atomic<bool> mFifoControlStarted{false};
	std::thread mMonitorLoopThread;
	std::thread mMonitorNmeaThread;
	std::thread fifo_writer_thread;
	mutable std::mutex mGpsdFifoMutex;
	mutable std::mutex mCommandFifoMutex;
	mutable std::mutex monitorloopMutex;
	std::condition_variable monitorLoop_condition_variable;
	std::queue<std::string> monitorLoopQueue;
};

}  // namespace implementation
}  // namespace V1_1
}  // namespace gnss
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_GNSS_V1_1_GNSS_H
