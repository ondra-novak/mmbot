/*
 * hist_data_storage.h
 *
 *  Created on: 20. 3. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_HIST_DATA_STORAGE_H_
#define SRC_MAIN_HIST_DATA_STORAGE_H_

#include <filesystem>
#include <vector>
#include <fstream>

template<typename T>
class AbstractHistDataStorage {
public:

	virtual ~AbstractHistDataStorage() {}


	virtual std::vector<T> read(std::size_t limit) = 0;
	virtual void store(const T &value) = 0;
	virtual void erase() = 0;
};

template<typename T> using PHistStorage = std::unique_ptr<AbstractHistDataStorage<T> >;


template<typename T>
class AbstractHIstDataStorageFactory {
public:
	virtual ~AbstractHIstDataStorageFactory() {}

	virtual PHistStorage<T> create(const std::string_view &name) = 0;
};

template<typename T> using PHistStorageFactory = std::shared_ptr<AbstractHIstDataStorageFactory<T> >;


template<typename T>
class PODFileHistDataStorage: public AbstractHistDataStorage<T> {
public:

	PODFileHistDataStorage(const std::string &fname);

	virtual std::vector<T> read(std::size_t limit) ;
	virtual void store(const T &value);
	virtual void erase();


protected:
	std::string fname;
	std::fstream bkfl;

};

template<typename T>
PODFileHistDataStorage<T>::PODFileHistDataStorage(const std::string &fname):fname(fname) {
	bkfl.open(fname, std::ios::in| std::ios::out| std::ios::app);
	if (!bkfl) throw std::runtime_error(std::string("Failed to open: ").append(fname));
}

template<typename T>
std::vector<T> PODFileHistDataStorage<T>::read(std::size_t limit) {
	bkfl.seekg(0, std::ios_base::end);
	auto ofs = bkfl.tellg();
	auto sz = ofs / sizeof(T);
	std::vector<T> buff;
	if (sz < limit) {
		limit = sz;
	}
	bkfl.seekg(ofs-limit*sizeof(T), std::ios_base::beg);
	buff.resize(limit);
	bkfl.read(reinterpret_cast<char *>(buff.data()),sizeof(T)*limit);
	if (!bkfl) throw std::runtime_error(std::string("Failed to read: ").append(fname));
	return buff;
}

template<typename T>
void PODFileHistDataStorage<T>::store(const T &value) {
	bkfl.write(reinterpret_cast<const char *>(&value), sizeof(T));
	if (!bkfl) throw std::runtime_error(std::string("Failed to write: ").append(fname));
	bkfl.flush();
}


template<typename T>
inline void PODFileHistDataStorage<T>::erase() {
	bkfl.close();
	std::filesystem::remove(fname);

}

#endif /* SRC_MAIN_HIST_DATA_STORAGE_H_ */
