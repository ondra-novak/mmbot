/*
 * core.cpp
 *
 *  Created on: 7. 4. 2022
 *      Author: ondra
 */

#include "core.h"

#include "btstore.h"
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

static PBacktestStorage initBacktestStorage(const ondra_shared::IniConfig::Section &section) {
	auto cache_size = section["backtest_cache_size"].getUInt(8);
	auto in_memory = section["in_memory"].getBool(false);
	return PBacktestStorage::make(cache_size, in_memory);

}

static PBacktestBroker initBacktestBroker(const ondra_shared::IniConfig::Section &section) {
	auto path = section["history_source"];
	if (!path.defined() || path.getString().empty()) return nullptr;
	return std::make_unique<BacktestBroker>(path.getCurPath(), "backtest_broker", path.getPath(), 120000);
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
,cfg_storage(storageFactory->create("config.storage"))
,backtest_storage(initBacktestStorage(cfg["backtest"]))
,backtest_broker(initBacktestBroker(cfg["backtest"]))
,cfg_history(storageFactory->create("history.storage"))
,hist_size(cfg["storage"]["hist_max_revs"].getUInt(100))
,in_progres(1)
{

}

void BotCore::storeConfig(json::Value cfg) {
	cfg_storage->store(cfg);
}

void BotCore::setConfig(json::Value cfg) {
	try {
		applyConfig(cfg);
		storeConfig(cfg);
	} catch (...) {
		applyConfig(cfg_storage->load());
		throw;
	}
}

BotCore::~BotCore() {
	traders.lock()->stop_cycle();
	sch.clear();
	wrk.flush();
	in_progres.wait();
}

void BotCore::run(bool suspended) {
	applyConfig(cfg_storage->load());
	if (suspended) {
		sch.each(std::chrono:: minutes(1)) >> [&]{
			rpt.lock()->genReport();
		};
		sch.after(std::chrono::seconds(1)) >> [&]{
			rpt.lock()->genReport();
		};
	} else{
		init_cycle(std::chrono::steady_clock::now());
	}

	sch.each(std::chrono::seconds(45)) >> [&]{
		rpt.lock()->pingStreams();
	};
}

void BotCore::applyConfig(json::Value cfg) {
	auto asvc = authService.lock();
	asvc->init_from_JSON(cfg);
	extBalance.lock()->load(cfg["wallet"]);
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
	rpt.lock()->calcWindowOnly(cfg["report_window_only"].getBool());
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

json::Value BotCore::get_config() const {
	json::Value out = cfg_storage->load();
	if (!out.hasValue()) out = json::object;
	return out;
}

void BotCore::init_cycle(std::chrono::steady_clock::time_point next_start) {
	auto start = std::max(std::chrono::steady_clock::now(), next_start);
	sch.at(start) >> [this,start]{
		in_progres.lock();
		traders.lock()->run_cycle(this->wrk, utlz, [this, start](bool cont){
			if (cont) {
				rpt.lock()->genReport();
				init_cycle(start+std::chrono::minutes(1));
			}
			in_progres.unlock();
		});
	};
}
