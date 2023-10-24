/*
 * spread.cpp
 *
 *  Created on: Aug 19, 2021
 *      Author: ondra
 */




#include "spread.h"
#include "series.h"

#include <cmath>
#include <memory>
#include <imtjson/value.h>

class DefaulSpread: public ISpreadFunction {
public:

	class State: public ISpreadState {
	public:
		StreamSMA sma;
		StreamSTDEV stdev;
		StreamBest<double, std::greater<double> > maxSpread;

		State(std::size_t sma_interval,std::size_t stdev_interval);
	    virtual ISpreadState *clone() const {
	        return new State(*this);
	    }
	};

	DefaulSpread(unsigned int sma, unsigned int stdev, double force_spread);

	virtual clone_ptr<ISpreadState> start() const ;
	virtual Result point(std::unique_ptr<ISpreadState> &state, double y) const;
	virtual ISpreadFunction *clone() const {
	    return new DefaulSpread(*this);
	}


protected:
	unsigned int sma;
	unsigned int stdev;
	double force_spread;

};

std::unique_ptr<ISpreadFunction> defaultSpreadFunction(double sma, double stdev, double force_spread) {
	return std::make_unique<DefaulSpread>(
	        std::max<unsigned int>(30,static_cast<unsigned int>(sma*60.0)),
            std::max<unsigned int>(30,static_cast<unsigned int>(stdev*60.0))
            ,force_spread);
}

std::unique_ptr<ISpreadFunction> defaultSpreadFunction_direct(unsigned int sma, unsigned int stdev, double force_spread) {
    return std::make_unique<DefaulSpread>(sma, stdev, force_spread);
}

#if 0
VisSpread::VisSpread(const std::unique_ptr<ISpreadFunction> &fn, const Config &cfg)
:fn(fn),state(fn->start()),dynmult(cfg.dynmult),sliding(cfg.sliding),freeze(cfg.freeze),mult(cfg.mult),order2(cfg.order2*0.01)
{


}
#endif

DefaulSpread::DefaulSpread(unsigned int sma, unsigned int stdev, double force_spread)
	:sma(sma),stdev(stdev),force_spread(force_spread)
{
}

clone_ptr<ISpreadState> DefaulSpread::start() const {
	return std::make_unique<State>(sma,stdev);
}

DefaulSpread::Result DefaulSpread::point(std::unique_ptr<ISpreadState> &state, double y) const {
	State &st = static_cast<State &>(*state);

	double avg = st.sma << y;
	if (force_spread) {
		return {true, force_spread, avg, 0};
	} else {
	    double rf = 0.01/std::sqrt(st.sma.size());
		double dv = st.stdev << (y - avg);
		return {true,  std::max(rf,std::log((avg+dv)/avg)), avg, 0};
	}
}

inline DefaulSpread::State::State(std::size_t sma_interval, std::size_t stdev_interval)
	:sma(sma_interval), stdev(stdev_interval),maxSpread(10)
{

}


class LegacySpreadGen: public ISpreadGen {
public:

    class MyState: public ISpreadGen::State {
    public:
        MyState(clone_ptr<ISpreadState> sp_state, DynMultControl::Config dyncfg)
                :sp_state(std::move(sp_state))
                ,dynmult(dyncfg) {}

        clone_ptr<ISpreadState> sp_state;
        DynMultControl dynmult;
        double offset = 0;
        double last_price = 0;
        std::optional<double> chigh;
        std::optional<double> clow;
        int frozen_side = 0;
        double frozen_spread = 0;
        bool was_exec = false;
        ISpreadFunction::Result result;

        virtual State *clone() const {
            return new MyState(*this);
        }
    };

    virtual void point(ISpreadGen::PState &state, double y, bool execution) const override {
        auto &st = static_cast<MyState &>(*state);
        if (execution) {
            if (st.last_price) {
                double center = sliding?st.result.center:0;
                double diff = y - st.last_price;
                st.offset = center;
                if (diff < 0) { // buy
                    st.dynmult.update(true, false);
                    st.frozen_spread = st.result.spread;
                    st.frozen_side = -1;
                    st.was_exec = true;
                } else { // sell
                    st.dynmult.update(false, true);
                    st.frozen_spread = st.result.spread;
                    st.frozen_side = 1;
                    st.was_exec = true;
                }
            }
            st.last_price = y;
        } else {
            st.result = fn->point(st.sp_state, y);
            if (!st.last_price) {
                st.last_price = y;
                st.offset = sliding?st.result.center:0;
            }
            if (st.was_exec) st.was_exec = false;
            else st.dynmult.update(false, false);
        }

    }
    LegacySpreadGen(LegacySpreadGenConfig cfg)
        :fn(defaultSpreadFunction_direct(cfg.sma, cfg.stdev,cfg.force_spread))
        ,dynmult(cfg.dynmult)
        ,max_hist(std::max(cfg.sma, cfg.stdev))
        ,freeze(cfg.freeze)
        ,sliding(cfg.sliding)
        ,mult(cfg.mult)
    {}

    virtual unsigned int get_required_history_length() const override {
        return max_hist;
    }
    virtual ISpreadGen::Result get_result(const ISpreadGen::PState &state,
            double equilibrium) const override {

        auto &st = static_cast<const MyState &>(*state);
        const auto &sp = st.result;

        if (!st.last_price) return {};

        double center = (sliding?(sp.center-st.offset):0) + equilibrium;

        double lspread = sp.spread;
        double hspread = sp.spread;
        if (freeze) {
            if (st.frozen_side<0) {
                lspread = std::min(st.frozen_spread, lspread);
            } else if (st.frozen_side>0) {
                hspread = std::min(st.frozen_spread, hspread);
            }
        }
        double low = center * std::exp(-lspread*mult*st.dynmult.getBuyMult());
        double high = center * std::exp(hspread*mult*st.dynmult.getSellMult());
        if (sliding && st.last_price) {
            double low_max = st.last_price*std::exp(-lspread*0.01);
            double high_min = st.last_price*std::exp(hspread*0.01);
            if (low > low_max) {
                high = low_max + (high-low);
                low = low_max;
            }
            if (high < high_min) {
                low = high_min - (high-low);
                high = high_min;

            }
            low = std::min(low_max, low);
            high = std::max(high_min, high);
        }
        return {low, high};
    }

    virtual PState start() const override {
        return PState(new MyState(fn->start(), dynmult));
    }

    virtual ISpreadGen *clone() const override {
        return new LegacySpreadGen(*this);
    }

    virtual SpreadStats get_stats(PState &state) const {
        auto &st = static_cast<const MyState &>(*state);
        return {
            st.result.spread * mult,
            st.dynmult.getBuyMult(),
            st.dynmult.getSellMult(),
        };
    }

protected:
    clone_ptr<ISpreadFunction> fn;
    DynMultControl::Config dynmult;
    unsigned int max_hist;
    bool freeze;
    bool sliding;
    double mult;

};

clone_ptr<ISpreadGen> legacySpreadGen(LegacySpreadGenConfig cfg) {
    return std::make_unique<LegacySpreadGen>(cfg);

}


