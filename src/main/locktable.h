/*
 * locktable.h
 *
 *  Created on: 11. 4. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_LOCKTABLE_H_
#define SRC_MAIN_LOCKTABLE_H_

#include <map>
#include <shared_mutex>

///Non intrusive locking
/** Allows to lock resources that cannot be locked directly. You can name resource by its pointer

 * @tparam T type of resource (recommended const T)
 */
template<typename T>
class LockTable {
public:

	using TPtr = T *;

	class ExclusiveLock {
	public:
		ExclusiveLock(LockTable *owner, TPtr ptr);
		ExclusiveLock(ExclusiveLock &&other);
		ExclusiveLock(const ExclusiveLock &other) = delete;
		ExclusiveLock &operator=(const ExclusiveLock &other) = delete;
		~ExclusiveLock();

	protected:
		LockTable *owner;
		TPtr ptr;
	};

	class SharedLock {
	public:
		SharedLock(LockTable *owner, TPtr ptr);
		SharedLock(SharedLock &&other);
		SharedLock(const SharedLock &other) = delete;
		SharedLock &operator=(const SharedLock &other) = delete;
		~SharedLock();

	protected:
		LockTable *owner;
		TPtr ptr;
	};

	///Lock resource exclusive. You need to keep instance of returned class during the lock
	/**
	 * @param ptr pointer to resource to lock
	 * @return lock guard
	 */
	ExclusiveLock lock(TPtr ptr);
	///Lock resource shared. You need to keep instance of returned class during the lock
	/**
	 * @param ptr pointer to resource to lock
	 * @return lock guard
	 */
	SharedLock lock_shared(TPtr ptr);

protected:
	struct LockInfo {
		std::shared_timed_mutex lk;
		std::size_t acqcnt = 0;
	};

	using Map = std::map<TPtr, LockInfo>;
	Map map;
	std::mutex mx;

	template<typename Fn> void acquire_lock(TPtr ptr, Fn &&lock_fn);
	template<typename Fn> void release_lock(TPtr ptr, Fn &&unlock_fn);
};

template<typename T>
LockTable<T>::ExclusiveLock::ExclusiveLock(LockTable *owner, TPtr ptr)
:owner(owner),ptr(ptr)
{
	owner->acquire_lock(ptr, [](auto &lk){lk.lock();});
}

template<typename T>
LockTable<T>::ExclusiveLock::ExclusiveLock(ExclusiveLock &&other)
:owner(other.owner),ptr(other.ptr)
{
	other.owner = nullptr;
}

template<typename T>
LockTable<T>::ExclusiveLock::~ExclusiveLock() {
	if (owner) {
		owner->release_lock(ptr, [](auto &lk){lk.unlock();});
	}
}

template<typename T>
LockTable<T>::SharedLock::SharedLock(LockTable *owner, TPtr ptr)
:owner(owner),ptr(ptr)
{
	owner->acquire_lock(ptr, [](auto &lk){lk.lock_shared();});
}


template<typename T>
LockTable<T>::SharedLock::SharedLock(SharedLock &&other)
:owner(other.owner),ptr(other.ptr)
{
	other.owner = nullptr;
}

template<typename T>
LockTable<T>::SharedLock::~SharedLock() {
	owner->acquire_lock(ptr, [](auto &lk){lk.unlock_shared();});
}

template<typename T>
typename LockTable<T>::ExclusiveLock LockTable<T>::lock(TPtr ptr) {
	return ExclusiveLock(this, ptr);
}

template<typename T>
typename LockTable<T>::SharedLock LockTable<T>::lock_shared(TPtr ptr) {
	return SharedLock(this,ptr);
}

template<typename T>
template<typename Fn>
inline void LockTable<T>::release_lock(TPtr ptr, Fn &&unlock_fn) {
	{
		std::lock_guard _(mx);
		LockInfo &nfo = map[ptr];
		unlock_fn(nfo.lk);
		if (--nfo.acqcnt <= 0) {
			map.erase(ptr);
		}
	}
}

template<typename T>
template<typename Fn>
inline void LockTable<T>::acquire_lock(TPtr ptr, Fn &&lock_fn) {
	std::shared_timed_mutex *tmx;
		{
			std::lock_guard _(mx);
			LockInfo &nfo = map[ptr];
			++nfo.acqcnt;
			tmx = &nfo.lk;
		}
	lock_fn(*tmx);
}

#endif /* SRC_MAIN_LOCKTABLE_H_ */
