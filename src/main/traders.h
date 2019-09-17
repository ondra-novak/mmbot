/*
 * trades.h
 *
 *  Created on: 17. 9. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_TRADERS_H_
#define SRC_MAIN_TRADERS_H_


using StatsSvc = Stats2Report;

class NamedMTrader: public MTrader {
public:
	NamedMTrader(IStockSelector &sel, StoragePtr &&storage, PStatSvc statsvc, Config cfg, std::string &&name)
			:MTrader(sel, std::move(storage), std::move(statsvc), cfg), ident(std::move(name)) {
	}

	bool perform() {
		using namespace ondra_shared;
		LogObject lg(ident);
		LogObject::Swap swap(lg);
		try {
			return MTrader::perform();
		} catch (std::exception &e) {
			logError("$1", e.what());
			return false;
		}
	}

	std::string ident;

};

class StockSelector: public IStockSelector{
public:
	using PStockApi = std::unique_ptr<IStockApi>;
	using StockMarketMap =  ondra_shared::linear_map<std::string, PStockApi, std::less<>>;

	StockMarketMap stock_markets;

	void loadStockMarkets(const ondra_shared::IniConfig::Section &ini, bool test) {
		std::vector<StockMarketMap::value_type> data;
		for (auto &&def: ini) {
			ondra_shared::StrViewA name = def.first;
			ondra_shared::StrViewA cmdline = def.second.getString();
			ondra_shared::StrViewA workDir = def.second.getCurPath();
			data.push_back(StockMarketMap::value_type(name,std::make_unique<ExtStockApi>(workDir, name, cmdline)));
		}
		StockMarketMap map(std::move(data));
		stock_markets.swap(map);
	}
	virtual IStockApi *getStock(const std::string_view &stockName) const {
		auto f = stock_markets.find(stockName);
		if (f == stock_markets.cend()) return nullptr;
		return f->second.get();
	}
	void addStockMarket(ondra_shared::StrViewA name, PStockApi &&market) {
		stock_markets.insert(std::pair(name,std::move(market)));
	}

	virtual void forEachStock(EnumFn fn)  const {
		for(auto &&x: stock_markets) {
			fn(x.first, *x.second);
		}
	}
	void clear() {
		stock_markets.clear();
	}
};


class Traders {
public:
    ondra_shared::linear_map<StrViewA,std::unique_ptr<NamedMTrader> > traders;
    StockSelector stockSelector;

		class ActionQueue: public ondra_shared::RefCntObj {
		public:
		ActionQueue(const ondra_shared::Scheduler &sch):sch(sch) {}

		template<typename Fn>
		void push(Fn &&fn) {
			bool e = dsp.empty();
			std::move(fn) >> dsp;
			if (e) goon();
		}

		void exec() {
			if (!dsp.empty()) {
				dsp.pump();
				goon();
			}
		}

		void goon() {
			sch.after(std::chrono::seconds(1)) >> [me = ondra_shared::RefCntPtr<ActionQueue>(this)]{
					me->exec();
			};
		}

		protected:
		ondra_shared::Dispatcher dsp;
		ondra_shared::Scheduler sch;
		};

		ondra_shared::RefCntPtr<ActionQueue> aq;

	Traders() {
	}

	void clear() {
		traders.clear();
		stockSelector.clear();
	}

	void init(ondra_shared::Scheduler sch,
			const ondra_shared::IniConfig::Section &ini, bool test) {
		aq = new ActionQueue(sch) ;
		stockSelector.loadStockMarkets(ini, test);
	}

	void addTrader(const ondra_shared::IniConfig &ini,
			ondra_shared::StrViewA n,
			StorageFactory &sf,
			Report &rpt,
			bool force_dry_run,
			int spread_calc_interval,
			Stats2Report::SharedPool pool) {
		using namespace ondra_shared;

		LogObject lg(n);
		LogObject::Swap swp(lg);
		try {
			MTrader::Config mcfg = MTrader::load(ini[n], force_dry_run);
			logProgress("Started trader $1 (for $2)", n, mcfg.pairsymb);
			auto t = std::make_unique<NamedMTrader>(stockSelector, sf.create(n),
					std::make_unique<StatsSvc>([aq = this->aq](auto &&fn) {
							aq->push(std::move(fn));
					}, n, rpt, spread_calc_interval, pool),
					mcfg, n);
			traders.insert(std::pair(t->ident, std::move(t)));
		} catch (const std::exception &e) {
			logFatal("Error: $1", e.what());
			throw std::runtime_error(std::string("Unable to initialize trader: ").append(n).append(" - ").append(e.what()));
		}

	}

	void loadTraders(const ondra_shared::IniConfig &ini,
			ondra_shared::StrViewA names,
			StorageFactory &sf,
			Report &rpt,
			bool force_dry_run,
			int spread_calc_interval,
			Stats2Report::SharedPool pool) {
		std::vector<StrViewA> nv;

		auto nspl = names.split(" ");
		while (!!nspl) {
			StrViewA x = nspl();
			if (!x.empty()) nv.push_back(x);
		}

		for (auto n: nv) {
			addTrader(ini, n, sf, rpt, force_dry_run, spread_calc_interval, pool);
		}
	}

	bool runTraders() {
		stockSelector.forEachStock([](json::StrViewA, IStockApi&api) {
			api.reset();
		});

		bool hit = false;
		for (auto &&t : traders) {
			bool h = t.second->perform();
			hit |= h;
		}
		return hit;
	}

	NamedMTrader *find(StrViewA id) const {
		auto iter = traders.find(id);
		if (iter == traders.end()) return nullptr;
		else return iter->second.get();
	}
};




#endif /* SRC_MAIN_TRADERS_H_ */
