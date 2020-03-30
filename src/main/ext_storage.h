/*
 * ext_storage.h
 *
 *  Created on: 16. 12. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_EXT_STORAGE_H_
#define SRC_MAIN_EXT_STORAGE_H_
#include <imtjson/value.h>
#include "abstractExtern.h"
#include "istorage.h"

class ExtStorage: public IStorageFactory {
public:

	ExtStorage(const std::string_view & workingDir, const std::string_view & name, const std::string_view & cmdline, int timeout);

	virtual PStorage create(std::string name) const;

	~ExtStorage();

protected:

	class Proxy: public AbstractExtern, public ondra_shared::RefCntObj {
	public:
		using AbstractExtern::AbstractExtern;

		void store(const std::string &name, const json::Value &data);
		json::Value load(const std::string &name);
		void erase(const std::string &name);
		virtual ~Proxy();

	};

	class Handle: public IStorage {
	public:
		Handle(std::string name,ondra_shared::RefCntPtr<Proxy> proxy);
		~Handle();

		virtual void store(json::Value data) override;
		virtual json::Value load()  override;
		virtual void erase()  override;
	protected:
		std::string name;
		ondra_shared::RefCntPtr<Proxy> proxy;

	};

	ondra_shared::RefCntPtr<Proxy> proxy;

};

#endif /* SRC_MAIN_EXT_STORAGE_H_ */
