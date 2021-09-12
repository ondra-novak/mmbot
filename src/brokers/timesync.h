/*
 * timesync.h
 *
 *  Created on: 12. 9. 2021
 *      Author: ondra
 */

#ifndef SRC_BROKERS_TIMESYNC_H_
#define SRC_BROKERS_TIMESYNC_H_
#include <chrono>

///Handles time synchronization with exchange's server - caller must provide own synchronization procedure, this only calculates time
class TimeSync {
public:

	///Initialize class, will require synchronization
	/**
	 * @param sync_interval interval in minutes how often the time should be synced.
	 *
	 *
	 * @note time must be synced first, otherwise the function returns local time
	 */
	TimeSync(unsigned int sync_interval):sync_interval(sync_interval) {}

	///Retrieves duration from last synchronization
	auto getLastSyncDuration() const {
		auto now = std::chrono::system_clock::now();
		return now-syncPoint;
	}

	///Retrieve current exchange's time in milliseconds (it is calculated from last synchronization and local time)
	std::uint64_t getCurTime() const {
		return std::chrono::duration_cast<std::chrono::milliseconds>(getCurTimePoint().time_since_epoch()).count();

	}
	///Retrieve current exchange's time as timepoint (it is calculated from last synchronization and local time)
	std::chrono::system_clock::time_point getCurTimePoint() const {
		return syncTime+getLastSyncDuration();
	}


	///Synchronizes the time
	/**
	 * @param time exchange's current time in milliseconds
	 */
	void setCurTime(std::uint64_t time) {
		syncPoint = std::chrono::system_clock::now();
		syncTime = std::chrono::system_clock::time_point()+std::chrono::milliseconds(time);

	}

	///determines, whether it is time to sync time
	/**
	 * @retval false no syncing is needed
	 * @retval true time must be synced (call setCurTime)
	 */
	bool needSync() const {
		return getLastSyncDuration() > std::chrono::minutes(sync_interval);
	}

protected:
	unsigned int sync_interval;
	std::chrono::system_clock::time_point syncPoint;
	std::chrono::system_clock::time_point syncTime;
};



#endif /* SRC_BROKERS_TIMESYNC_H_ */
