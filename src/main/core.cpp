/*
 * core.cpp
 *
 *  Created on: 7. 4. 2022
 *      Author: ondra
 */

#include "core.h"

#include "extdailyperfmod.h"
#include "stock_selector.h"
#include "hist_data_storage.h"
#include "ext_storage.h"

static PStorageFactory initLocalStorage(const ondra_shared::IniConfig::Section &section) {
	auto storagePath = section.mandatory["storage_path"].getPath();
	auto storageBinary = section["storage_binary"].getBool(true);
	auto storageVersions = section["storage_versions"].getUInt(5);

	return PStorageFactory(new StorageFactory(storagePath,storageVersions,storageBinary?Storage::binjson:Storage::json));

}

static PStorageFactory initExtStorage(const ondra_shared::IniConfig::Section &section) {
	auto storageBroker = section["storage_broker"];
	auto brk_timeout = section["timeout"].getInt(10000);

	return PStorageFactory(new ExtStorage(storageBroker.getCurPath(), "storage_broker", storageBroker.getString(), brk_timeout));

}

static PStorageFactory initStorage(const ondra_shared::IniConfig::Section &section) {
	auto storageBroker = section["storage_broker"];
	auto bl = section["backup_locally"].getBool(false);

	if (!storageBroker.defined()) {
		return initLocalStorage(section);
	} else {
		if (bl) {
			return PStorageFactory(new BackedStorageFactory(initExtStorage(section), initLocalStorage(section)));
		} else {
			return PStorageFactory(initExtStorage(section));
		}
	}
}

template<typename T>
class LocalHIstDataStorageFactory: public AbstractHIstDataStorageFactory<T> {
public:
	LocalHIstDataStorageFactory(const std::string_view &path, const std::string_view &suffix)
		:path(path),suffix(suffix) {}
	virtual PHistStorage<T> create(const std::string_view &name);
protected:
	std::filesystem::path path;
	std::string suffix;
};

template<typename T>
inline PHistStorage<T> LocalHIstDataStorageFactory<T>::create(const std::string_view &name) {
	auto fname = path/name;
	fname.replace_extension(suffix);
	return PHistStorage<T>(new PODFileHistDataStorage<T>(fname));

}

static PHistStorageFactory<HistMinuteDataItem> initLocalHistStorage(const ondra_shared::IniConfig::Section &section) {
	auto storagePath = section.mandatory["storage_path"].getPath();

	return PHistStorageFactory<HistMinuteDataItem>(
		std::make_shared<LocalHIstDataStorageFactory<HistMinuteDataItem> >(storagePath, ".minute")
	);

}


static auto initBrokerList(const ondra_shared::IniConfig::Section &section) {
	auto stk = std::make_shared<StockSelector>();
	stk->loadBrokers(section);
	return stk;
}

static auto initPerformanceModule(const ondra_shared::IniConfig::Section &section) {
	auto reportBroker = section.mandatory["broker"];
	auto brk_timeout = section["timeout"].getInt(10000);
	auto incl_simulators = section["simulators"].getBool(false);

	return std::make_shared<ExtDailyPerfMod>(reportBroker.getCurPath(), "report_broker", reportBroker.getString(), !incl_simulators, brk_timeout);
}

static auto initWorker(const ondra_shared::IniConfig::Section &section) {
	int threads = std::max<int>(1,section["threads"].getUInt(1));
	return ondra_shared::Worker::create(threads);
}

BotCore::BotCore(const ondra_shared::IniConfig &cfg)
:sch(sch.create())
,wrk(initWorker(cfg["traders"]))
,brokerList(initBrokerList(cfg["brokers"]))
,storageFactory(initStorage(cfg["storage"]))
,histStorageFactory(initLocalHistStorage(cfg["storage"]))
,balanceCache(PBalanceMap::make())
,extBalance(PBalanceMap::make())
,rptStorage(new MemStorage) 				//create unsafe pointer, next line makes it safe
,rpt(PReport::make(PStorage(rptStorage)))
,perfmod(initPerformanceModule(cfg["report"]))
,authService(PAuthService::make())
,utlz(PUtilization::make())
,broker_config(json::object)
,news_tm(json::undefined)
,traders(PTraders::make(brokerList, storageFactory, histStorageFactory, rpt,perfmod, balanceCache, extBalance))
,cfg_storage(storageFactory->create("config.json"))
,in_progress(0)
{

}

void BotCore::setConfig(json::Value cfg) {
	try {
		applyConfig(cfg);
		cfg_storage->store(cfg);
	} catch (...) {
		applyConfig(cfg_storage->load());
		throw;
	}
}

BotCore::~BotCore() {
	traders.lock()->stop_cycle();
	sch.clear();
	in_progress.wait();
}

void BotCore::run(bool suspended) {
	applyConfig(cfg_storage->load());
	if (!suspended) init_cycle(std::chrono::steady_clock::now());
	sch.each(std::chrono::seconds(45)) >> [&]{
		rpt.lock()->pingStreams();
	};
}

void BotCore::applyConfig(json::Value cfg) {
	auto asvc = authService.lock();
	asvc->init_from_JSON(cfg);
	extBalance.lock()->load(cfg["ext_assets"]);
	broker_config = cfg["brokers"].getValueOrDefault(json::object);
	brokerList->enum_brokers([&](std::string_view name, PStockApi api){
		json::Value b = broker_config[name];
		if (b.defined()) {
			IBrokerControl *eapi = dynamic_cast<IBrokerControl *>(api.get());
			if (eapi) {
				eapi->restoreSettings(b);
			}
		}
	});
	rpt.lock()->setInterval(cfg["report_interval"].getValueOrDefault(30L*86400000L));
	news_tm = cfg["news_tm"];
	if (!news_tm.defined()) {
		news_tm = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}
	auto trs = traders.lock();
	trs->begin_add();
	for (json::Value v: cfg["traders"]) {
		trs->add_trader(v.getKey(), v);
	}
	trs->commit_add();

}

void BotCore::init_cycle(std::chrono::steady_clock::time_point next_start) {
	auto start = std::max(std::chrono::steady_clock::now(), next_start);
	sch.at(start) >> [this,start]{
		traders.lock()->run_cycle(this->wrk, utlz, [this, start](bool cont){
			if (cont) {
				rpt.lock()->genReport();
				init_cycle(start+std::chrono::minutes(1));
			}
		});
	};
}
