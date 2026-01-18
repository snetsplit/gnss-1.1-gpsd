#define LOG_TAG "Gnss"

#include <android-base/properties.h>
#include <android/hardware/gnss/1.0/types.h>
#include <atomic>
#include <condition_variable>
#include "Constants.h"
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include "Gnss.h"
#include "GnssDebug.h"
#include "GnssMeasurement.h"
#include <iostream>
#include <log/log.h>
#include <mutex>
#include <queue>
#include <string>
#include <sys/stat.h>
#include "Utils.h"
#include <unistd.h>

#include <cctype>
#include <charconv>


namespace android {
namespace hardware {
namespace gnss {
namespace V1_1 {
namespace implementation {

using ::android::hardware::gnss::common::Utils;
using GnssSvFlags = IGnssCallback::GnssSvFlags;
using namespace ::android::hardware::gnss::common;

const uint32_t MIN_INTERVAL_MILLIS = 100;
sp<::android::hardware::gnss::V1_1::IGnssCallback> Gnss::sGnssCallback = nullptr;

Gnss::Gnss()
	: mMinIntervalMs(1000),
	mGnssConfiguration{new GnssConfiguration()}
{

	unlink("/data/system/gpspipes/");
	mMonitorLoopThread = std::thread(&Gnss::monitorLoop, this);
	mMonitorNmeaThread = std::thread(&Gnss::monitorNmea, this);
	startControl();
}

Gnss::~Gnss() {
	stop();
	mRunning.store(false);
	if (mMonitorLoopThread.joinable()) {
		mMonitorLoopThread.join();
	}
}

// Methods from ::android::hardware::gnss::V1_0::IGnss follow.
Return<bool> Gnss::setCallback(const sp<::android::hardware::gnss::V1_0::IGnssCallback>&) {
	// Mock handles only new callback (see setCallback1_1) coming from Android P+
	return false;
}

Return<bool> Gnss::start() {
	if (mIsActive.load()) {
		ALOGW("Gnss has started. Restarting...");
		stop();
	}

	ALOGE("Fifo reader storting, mFifoControl=%d", mFifoControl?1:0);
	ensure_directory_exists("/data/system/gpspipes",0777);

	mIsActive.store(true);

	updateControl();

	return true;
}

Return<bool> Gnss::stop() {
	ALOGE("Fifo reader stopped, mFifoControl=%d", mFifoControl?1:0);
	mIsActive.store(false);

	updateControl();

	return true;
}

void Gnss::updateControl() {

	if(!mFifoControl.load()) {
		return;
	}

	{
		std::lock_guard<std::mutex> lock(mCommandFifoMutex);
		if(mIsActive.load()) {
			mFifoCommandQueue.push("start\0");
		} else {
			mFifoCommandQueue.push("stop\0");
		}
	}
	mFifoCommandQueueConditionalVariable.notify_one();
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

Return<bool> Gnss::setPositionMode(::android::hardware::gnss::V1_0::IGnss::GnssPositionMode, ::android::hardware::gnss::V1_0::IGnss::GnssPositionRecurrence, uint32_t, uint32_t, uint32_t) {
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

Return<sp<::android::hardware::gnss::V1_0::IGnssNavigationMessage>> Gnss::getExtensionGnssNavigationMessage() {
	// TODO implement
	return ::android::sp<::android::hardware::gnss::V1_0::IGnssNavigationMessage>{};
}

Return<sp<::android::hardware::gnss::V1_0::IGnssXtra>> Gnss::getExtensionXtra() {
	// TODO implement
	return ::android::sp<::android::hardware::gnss::V1_0::IGnssXtra>{};
}

Return<sp<::android::hardware::gnss::V1_0::IGnssConfiguration>> Gnss::getExtensionGnssConfiguration() {
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

// Methods from ::android::hardware::gnss::V1_1::IGnss follow.
Return<bool> Gnss::setCallback_1_1(
	const sp<::android::hardware::gnss::V1_1::IGnssCallback>& callback) {
	if (callback == nullptr) {
		ALOGE("%s: Null callback ignored", __func__);
		return false;
	}

	sGnssCallback = callback;

	uint32_t capabilities = 0x0;
	auto ret = sGnssCallback->gnssSetCapabilitesCb(capabilities);
	if (!ret.isOk()) {
		ALOGE("%s: Unable to invoke callback", __func__);
	}

	IGnssCallback::GnssSystemInfo gnssInfo = {.yearOfHw = 2026};

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

Return<bool> Gnss::setPositionMode_1_1(::android::hardware::gnss::V1_0::IGnss::GnssPositionMode, ::android::hardware::gnss::V1_0::IGnss::GnssPositionRecurrence, uint32_t minIntervalMs, uint32_t, uint32_t, bool) {
	mMinIntervalMs = (minIntervalMs < MIN_INTERVAL_MILLIS) ? MIN_INTERVAL_MILLIS : minIntervalMs;
	return true;
}

Return<sp<::android::hardware::gnss::V1_1::IGnssConfiguration>> Gnss::getExtensionGnssConfiguration_1_1() {
	return mGnssConfiguration;
}

Return<sp<::android::hardware::gnss::V1_1::IGnssMeasurement>> Gnss::getExtensionGnssMeasurement_1_1() {
	// TODO implement
	return new GnssMeasurement();
}

Return<bool> Gnss::injectBestLocation(const GnssLocation&) {
	return true;
}


Return<void> Gnss::reportLocation(const GnssLocation& location) const {
	if (sGnssCallback == nullptr) {
		ALOGE("%s: sGnssCallback is null.", __func__);
		return Void();
	}
	sGnssCallback->gnssLocationCb(location);
	return Void();
}

Return<void> Gnss::reportSvStatus(const GnssSvStatus& svStatus) const {
	if (sGnssCallback == nullptr) {
		ALOGE("%s: sGnssCallback is null.", __func__);
		return Void();
	}
	sGnssCallback->gnssSvStatusCb(svStatus);
	return Void();
}

bool Gnss::ensureFifoIsUsable(const char* FIFO_PATH) {
	ensure_directory_exists("/data/system/gpspipes",0777);
	struct stat st;
	if (lstat(FIFO_PATH, &st) != 0) {
		if (errno == ENOENT) {
			ALOGW("FIFO %s does not exist, creating it", FIFO_PATH);
			if ((mkfifo(FIFO_PATH, 0777) != 0) || (chmod(FIFO_PATH, 0777) != 0)) {
				ALOGE("mkfifo(%s) failed: %s", FIFO_PATH, strerror(errno));
				return true;
			}
		} else {
			ALOGE("stat(%s) failed: %s", FIFO_PATH, strerror(errno));
			return true;
		}
	} else if (!S_ISFIFO(st.st_mode)) {
		ALOGE("%s exists but is not a FIFO, cleaning up mess", FIFO_PATH);
		if ((unlink(FIFO_PATH) != 0) || (mkfifo(FIFO_PATH, 0777) != 0) || (chmod(FIFO_PATH, 0777) != 0)) {
			ALOGE("Fifo clean failed for %s: %s", FIFO_PATH, strerror(errno));
			return true;
		}
	}

	return false;
}

void Gnss::monitorLoop() {
	const std::string FIFO_PATH_STRING = "/data/system/gpspipes/json.pipe";
	const char* FIFO_PATH = FIFO_PATH_STRING.c_str();


	ALOGW("Using GPS FIFO %s from prop \"persist.sys.gnss.gpsd.pipe\"", FIFO_PATH);

	while (mRunning.load()) {
		if(ensureFifoIsUsable(FIFO_PATH)) {
			std::this_thread::sleep_for(std::chrono::seconds(10));
			continue;
		}

		ALOGE("Fifo path valid");
		int fifoFileDescriptor = open(FIFO_PATH, O_RDONLY);
		if (fifoFileDescriptor < 0) {
			ALOGE("Failed to open FIFO %s: %s", FIFO_PATH, strerror(errno));
			std::this_thread::sleep_for(std::chrono::seconds(2));
			continue;
		}
		ALOGE("Fifo open");
		ALOGE("Fifo mRunning=%d", mRunning.load());


		char charBuffer[4096];
		std::string partialLine;
		ssize_t numberOfBytesToRead = 1;
		size_t newLinePosition = 0;
		ALOGE("Fifo Buffer intialized ");

		while (mRunning.load()) {
			numberOfBytesToRead = read(fifoFileDescriptor, charBuffer, sizeof(charBuffer) - 1);
			ALOGE("Fifo Buffer byte size %zd", numberOfBytesToRead);
			if (numberOfBytesToRead < 0) {
				ALOGE("Fifo reopening, %s: %s", FIFO_PATH, strerror(errno));
				break;
			} else if (0 == numberOfBytesToRead) {
				ALOGE("Fifo buffer empty, %s: %s", FIFO_PATH, strerror(errno));
				break;
			}


			charBuffer[numberOfBytesToRead] = '\0';
			partialLine.append(charBuffer, numberOfBytesToRead);
			newLinePosition = partialLine.find('\n');
			while (std::string::npos != newLinePosition) {
				std::string line = partialLine.substr(0, newLinePosition);
				partialLine.erase(0, newLinePosition + 1);
				newLinePosition = partialLine.find('\n');
				parseLine(line);
				ALOGE("Fifo line %s", line.c_str());
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(mMinIntervalMs));
		}

		std::this_thread::sleep_for(std::chrono::seconds(10));
	}
}


void Gnss::monitorNmea() {
	const std::string FIFO_PATH_STRING = "/data/system/gpspipes/nmea.pipe";
	const char* FIFO_PATH = FIFO_PATH_STRING.c_str();
	int64_t timestampMs = 0;



	while (mRunning.load()) {
		if(ensureFifoIsUsable(FIFO_PATH)) {
			std::this_thread::sleep_for(std::chrono::seconds(10));
			continue;
		}

		ALOGE("Fifo path valid");
		int fifoFileDescriptor = open(FIFO_PATH, O_RDONLY);
		if (fifoFileDescriptor < 0) {
			ALOGE("Failed to open FIFO %s: %s", FIFO_PATH, strerror(errno));
			std::this_thread::sleep_for(std::chrono::seconds(2));
			continue;
		}
		ALOGE("Fifo open");
		ALOGE("Fifo mRunning=%d", mRunning.load());


		char charBuffer[4096];
		std::string partialLine;
		ssize_t numberOfBytesToRead = 1;
		size_t newLinePosition = 0;
		ALOGE("Fifo Buffer intialized ");

		while (mRunning.load()) {
			numberOfBytesToRead = read(fifoFileDescriptor, charBuffer, sizeof(charBuffer) - 1);
			ALOGE("Fifo Buffer byte size %zd", numberOfBytesToRead);
			if (numberOfBytesToRead < 0) {
				ALOGE("Fifo reopening, %s: %s", FIFO_PATH, strerror(errno));
				break;
			} else if (0 == numberOfBytesToRead) {
				ALOGE("Fifo buffer empty, %s: %s", FIFO_PATH, strerror(errno));
				break;
			}


			charBuffer[numberOfBytesToRead] = '\0';
			partialLine.append(charBuffer, numberOfBytesToRead);
			newLinePosition = partialLine.find('\n');
			while (std::string::npos != newLinePosition) {
				std::string line = partialLine.substr(0, newLinePosition);
				partialLine.erase(0, newLinePosition + 1);
				newLinePosition = partialLine.find('\n');
				timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
				sGnssCallback->gnssNmeaCb(timestampMs, line);
				ALOGE("Fifo line %s", line.c_str());
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(mMinIntervalMs));
		}

		std::this_thread::sleep_for(std::chrono::seconds(10));
	}
}

nlohmann::json Gnss::getJsonSubstring(const std::string& line) {
	size_t begin = line.find('{');
	if (begin == std::string::npos) {
		return nlohmann::json::object();
	}

	size_t end = line.rfind('}');
	if (end == std::string::npos || end <= begin) {
		return nlohmann::json::object();
	}

	return nlohmann::json::parse(line.begin() + begin, line.begin() + end + 1);
}

bool Gnss::setMillisecondsSinceEpochFromIso8601(const std::string& iso8601, int64_t& epoch_milliseconds) {
	std::tm tm = {};
	int milliseconds = 0;

	// Parse date and time up to seconds
	std::istringstream string_stream(iso8601);
	string_stream >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
	if (string_stream.fail()) {
		return true;
	}

	// Extract milliseconds if present (e.g. ".123")
	size_t decimal_index = iso8601.find('.');
	if (decimal_index != std::string::npos && decimal_index + 4 <= iso8601.size()) {
		const std::string millis_str = iso8601.substr(decimal_index + 1, 3);
		for (char c : millis_str) {
			if (!isdigit(c)) {
				return true;
			}
		}
		milliseconds = std::stoi(millis_str);
	}

	// Convert tm (UTC) to time_t seconds since epoch
	time_t seconds = timegm(&tm);
	if (seconds == -1) {
		return true;
	}

	epoch_milliseconds = (static_cast<int64_t>(seconds) * 1000) + milliseconds;
	return false;
}


void Gnss::parseLine(const std::string& line) {
	std::lock_guard<std::mutex> lock(mGpsdFifoMutex);
	nlohmann::json jsonRecord = getJsonSubstring(line);

	if(jsonRecord.is_discarded() || !jsonRecord.contains("class")) {
		ALOGE("Fifo JSON parse is a failure, or lacks class: %s", line.c_str());
		return;
	}

	if (jsonRecord["class"] == "SKY") {
		processSatelliteInfo(jsonRecord);
	} else if (jsonRecord["class"] == "TPV") {
		processVelocity(jsonRecord);
	} else if (jsonRecord["class"] == "update") {
		updateControl();
	} else if (jsonRecord["class"] == "POLL") {
		int skyCount = jsonRecord["sky"].size();
		for (size_t index = 0; index < skyCount; ++index) {
			processVelocity(jsonRecord["sky"][index]);
		}

		int tpvCount = jsonRecord["tpv"].size();
		for (size_t index = 0; index < tpvCount; ++index) {
			processVelocity(jsonRecord["tpv"][index]);
		}
	} else {
		ALOGE("Fifo Unknown class. \n\tString: %s\n\tJson:%s", line.c_str(),jsonRecord.dump().c_str());
	}
}


void Gnss::startControl() {
	if (mFifoControlStarted.load()) {
		ALOGW("Control FIFO already active, ignoring startControl()");
		return;
	}

	mFifoControlStarted = true;

	ALOGE("Fifo Starting control: %s", controlFifoPath);

	fifo_writer_thread = std::thread([this] {
		fifo_writer(controlFifoPath, &mFifoCommandQueueConditionalVariable, &mFifoCommandQueue);
	});

	ALOGE("Fifo control started: %s", controlFifoPath);
}

void Gnss::processSatelliteInfo(nlohmann::json& jsonRecord) {
	if (jsonRecord.contains("satellites")) {
		ALOGE("Fifo satellites: %s", jsonRecord["satellites"].dump(2).c_str());
	}

	GnssSvStatus svStatus = GnssSvStatus{};
	if (!jsonRecord.contains("satellites") || !jsonRecord["satellites"].is_array() || jsonRecord["satellites"].empty()) {
		ALOGW("No satellite data available");
		return;
	}

	uint32_t index = 0;

	for (const auto& satellite : jsonRecord["satellites"]) {
		if (index >= 64) {
			ALOGW("Too many satellites, truncating list at %d", 64);
			break;
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
			default: gnssSvInfo.constellation = GnssConstellationType::UNKNOWN; break;
		}

		gnssSvInfo.azimuthDegrees   = satellite.value("az", 0.0f);
		gnssSvInfo.elevationDegrees = satellite.value("el", 0.0f);

		// Signal strength (mandatory)
		gnssSvInfo.cN0Dbhz = satellite.value("ss", 0.0f);


		// Used in fix (only if fix exists)
		if (hasFix && satellite.value("used", false)) {
			flags |= GnssSvFlags::USED_IN_FIX;
		}

		gnssSvInfo.svFlag = flags;
		svStatus.gnssSvList[index++] = gnssSvInfo;
	}

for (uint32_t i = index; i < 64; ++i) {
		svStatus.gnssSvList[i] = GnssSvInfo{};  // zero out remaining entries
	}

	svStatus.numSvs = index;
	this->reportSvStatus(svStatus);
	mSvStatus = svStatus;
}

void Gnss::processVelocity(nlohmann::json& jsonRecord){
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

	processAltitude(location, flags, jsonRecord);

	if (jsonRecord.contains("speed")) {
		flags |= V1_0::GnssLocationFlags::HAS_SPEED;
		location.speedMetersPerSec = jsonRecord.value("speed", 0.0);
	}

	if (jsonRecord.contains("eps")) {
		flags |= V1_0::GnssLocationFlags::HAS_SPEED_ACCURACY;
		location.speedAccuracyMetersPerSecond = jsonRecord.value("eps", 0.5);
	}

	processTrack(location, flags, jsonRecord);

	if (jsonRecord.contains("time") && setMillisecondsSinceEpochFromIso8601(jsonRecord["time"], location.timestamp)) {
		location.timestamp = static_cast<int64_t>(time(NULL)) * 1000LL;
	}


	ALOGW("Fifo timestamp %ld", location.timestamp);

	location.gnssLocationFlags = flags;
	this->reportLocation(location);
	mGnssLocation = location;
}

void Gnss::processAltitude(GnssLocation& location, uint16_t& flags, nlohmann::json& jsonRecord) {
	//per gpsd_json alt is unreliable so prefer HAE, then MSL then, if nothing else, alt is better than nothing hopefully.
	//Android can get flaky if a value for altitude is supplied but not an accuracy value.
	//Potential inputs such as Geoclue don't supply one. So muddle an accuracy value if needed.
	if (jsonRecord.contains("altHAE")) {
		location.altitudeMeters = jsonRecord.value("altHAE", 0.0);
		flags = static_cast<uint16_t>( flags | V1_0::GnssLocationFlags::HAS_ALTITUDE | V1_0::GnssLocationFlags::HAS_VERTICAL_ACCURACY);
		location.verticalAccuracyMeters = jsonRecord.value("epv", 2 * location.horizontalAccuracyMeters);
	} else if (jsonRecord.contains("altMSL")) {
		location.altitudeMeters = jsonRecord.value("altMSL", 0.0);
		flags = static_cast<uint16_t>( flags | V1_0::GnssLocationFlags::HAS_ALTITUDE | V1_0::GnssLocationFlags::HAS_VERTICAL_ACCURACY);
		location.verticalAccuracyMeters = jsonRecord.value("epv", 2 * location.horizontalAccuracyMeters);
	} else if (jsonRecord.contains("alt")) {
		location.altitudeMeters = jsonRecord.value("alt", 0.0);
		flags = static_cast<uint16_t>( flags | V1_0::GnssLocationFlags::HAS_ALTITUDE | V1_0::GnssLocationFlags::HAS_VERTICAL_ACCURACY);
		location.verticalAccuracyMeters = jsonRecord.value("epv", 2 * location.horizontalAccuracyMeters);
	}
}

void Gnss::processTrack(GnssLocation& location, uint16_t& flags, nlohmann::json& jsonRecord) {
	bool hasMagTrack = jsonRecord.contains("magtrack");

	//check to see if a magnetic track (compass reading) exists, and use it if speed or location accuracy would make it more accurate
	if (jsonRecord.contains("track") && (!hasMagTrack || ((0.25f * location.horizontalAccuracyMeters) < jsonRecord.value("speed",-1.0)))) {
		location.bearingDegrees = jsonRecord.value("track", 0.0);
		flags |= V1_0::GnssLocationFlags::HAS_BEARING;

		if (jsonRecord.contains("epd")) {
			location.bearingAccuracyDegrees = jsonRecord.value("epd", 0.0);
			flags |= V1_0::GnssLocationFlags::HAS_BEARING_ACCURACY;
		}
	} else if (hasMagTrack) {
		location.bearingDegrees = jsonRecord.value("magtrack", 0.0) + jsonRecord.value("magvar", 0.0);

		location.bearingDegrees = std::fmod(location.bearingDegrees, 360.0);
		if (location.bearingDegrees < 0.0) {
			location.bearingDegrees += 360.0;
		}


		while (location.bearingDegrees < 0.0) {
			location.bearingDegrees += 360.0;
		}

		while (location.bearingDegrees >= 360.0) {
			location.bearingDegrees -= 360.0;
		}

		location.bearingAccuracyDegrees = 1.8;
		flags |= V1_0::GnssLocationFlags::HAS_BEARING | V1_0::GnssLocationFlags::HAS_BEARING_ACCURACY;
	}
}

void Gnss::fifo_writer(const char* fifo_path, std::condition_variable* queue_cv, std::queue<std::string>* fifo_queue) {
	ALOGE("Fifo writer started");
	ensureFifoIsUsable(fifo_path);
	int fifo_fd = open(fifo_path, O_WRONLY);
	write(fifo_fd, "init\0", 6);

	ALOGE("Fifo command reader detected");
	mFifoControl = true;
		updateControl();

	while (mRunning.load()) {
		ALOGE("Fifo loop 1");
		std::unique_lock<std::mutex> lock(mCommandFifoMutex);
		queue_cv->wait(lock, [&] {
			return !fifo_queue->empty() || !mRunning.load();
		});

		while (mRunning.load() && !fifo_queue->empty()) {
			ALOGE("Fifo loop 2");
			std::string line = fifo_queue->front();
			fifo_queue->pop();

			lock.unlock();
			std::string out = line;
			ssize_t rc = write(fifo_fd, out.data(), out.size());

			if (rc == -1 && errno == EPIPE) {
				close(fifo_fd);
				ensureFifoIsUsable(fifo_path);
				fifo_fd = open(fifo_path, O_WRONLY);
			}

			lock.lock();
		}
	}

	close(fifo_fd);
}

bool Gnss::ensure_directory_exists(const char *path, mode_t mode) {
	struct stat st;

	if (stat(path, &st) == 0) {
		if (S_ISDIR(st.st_mode)) {
			return true;  // already exists
		}

		std::cerr << "Fifo directory exists but isn't a directory: " << path << "\n";
		return false;
	}

	if (errno != ENOENT) {
		std::cerr << "Fifo directory exists but isn't a directory: " << path << " error: " << strerror(errno) << "\n";
		return false;
	}

	// Directory does not exist — try to create it
	if ((mkdir(path, mode) == 0)  && (chmod(path, mode) == 0)) {
		return true;
	}

	// Handle race: someone else created it
	if (errno == EEXIST) {
		if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
			return true;
		}
	}

	ALOGE("Fifo mkdir failed: %s error: %s", path, strerror(errno));
	return false;
}

}  // namespace implementation
}  // namespace V1_1
}  // namespace gnss
}  // namespace hardware
}  // namespace android
