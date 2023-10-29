#include "bollinger_spread.h"
#include <imtjson/value.h>

#include "sgn.h"
BollingerSpread::BollingerSpread(unsigned int mean_points,
        unsigned int stdev_points, std::vector<double> curves,
        bool zero_line)
:_mean_points(mean_points)
,_stdev_points(stdev_points)
,_curves(std::move(curves))
,_zero_line(zero_line)
{


}

ISpreadGen::PState BollingerSpread::start() const {
    std::vector<double> c;
    if (_curves.empty()) {
        c.push_back(1.0);
        c.push_back(-1.0);
    } else {
        for (double d : _curves) {
            if (d > 0) {
                c.push_back(d);
                c.push_back(-d);
            }
        }
    }

    if (_zero_line) c.push_back(0.0);
    std::sort(c.begin(), c.end());
    return PState(new State(c, EMStDev(_mean_points, _stdev_points)));
}

ISpreadGen* BollingerSpread::clone() const {
    return new BollingerSpread(*this);
}

ISpreadGen::State* BollingerSpread::State::clone() const {
    return new State(*this);
}

BollingerSpread::State& BollingerSpread::get_state(PState &st) {
    return static_cast<State &>(*st);
}

ISpreadGen::Result BollingerSpread::get_result(const ISpreadGen::PState &state, double equilibrium) const {
    const State &st = get_state(state);
    Result r;
    if (!st._inited) return r;

    {
        auto iter = st._buy_curve;
        while (!st.below(iter)) {
            auto b = st._stdev(*iter);
            if (b < equilibrium) {
                r.buy = b;
                break;
            }
            --iter;
        }
    }

    {
        auto iter = st._sell_curve;
        while (!st.above(iter)) {
            auto s = st._stdev(*iter);
            if (s > equilibrium) {
                r.sell = s;
                break;
            }
            ++iter;
        }
    }
    return r;
}


void BollingerSpread::point(ISpreadGen::PState &state, double y,
        bool execution) const {
    State &st = get_state(state);
    if (st._inited) {
        if (execution) {

            if (!st.below(st._buy_curve)) {
                double buy_ref = st._stdev(*st._buy_curve);
                while (y < buy_ref || similar(y, buy_ref)) {
                    st._disabled_curve = st._buy_curve;
                    --st._buy_curve;
                    if (st.below(st._buy_curve)) break;
                    buy_ref = st._stdev(*st._buy_curve);
                }
                st._sell_curve = st._disabled_curve + 1;
            }
            else if (!st.above(st._sell_curve)) {
                double sell_ref = st._stdev(*st._sell_curve);
                while (y > sell_ref || similar(y, sell_ref)) {
                    st._disabled_curve = st._sell_curve;
                    ++st._sell_curve;
                    if (st.above(st._sell_curve)) break;
                    sell_ref = st._stdev(*st._sell_curve);
                }
                st._sell_curve = st._disabled_curve - 1;
            }


        } else {
            st._stdev += y;
            auto nxb = st.next_buy(st._buy_curve);
            if (st._stdev(*nxb) < y) st._buy_curve = nxb;

            auto nxs = st.next_sell(st._sell_curve);
            if (st._stdev(*nxs) > y) st._sell_curve = nxs;

        }
    } else {
        st._stdev.set_initial(y, 0.01*y);
        auto init = std::lower_bound(st._curves.begin(), st._curves.end(), 0.0);
        if (init == st._curves.end()) {
            st._disabled_curve = &st._curves.back();
        } else {
            st._disabled_curve = &(*init);
        }
        st._buy_curve = &st._curves.front();
        st._sell_curve = &st._curves.back();
        st._inited = true;
    }

/*
    *st.debug << (execution?"X":"-") << "," << *st._disabled_curve << "," << y << ",";
    if (st.at_end(st._buy_curve)) *st.debug << "---"; else *st.debug << *st._buy_curve;
    *st.debug << ",";
    if (st.at_end(st._sell_curve)) *st.debug << "---"; else *st.debug << *st._sell_curve;
    for (auto x: st._curves) *st.debug << "," << st._stdev(x);
    *st.debug << std::endl;
*/
}

unsigned int BollingerSpread::get_required_history_length() const {
    return std::max(_mean_points, _stdev_points);
}

SpreadStats BollingerSpread::get_stats(ISpreadGen::PState &state, double equilibrium) const {
    const State &st = get_state(state);
    SpreadStats out;
    out.spread = st._stdev.get_stdev()/st._stdev.get_mean();
    Result res =get_result(state,  equilibrium);
    if (res.buy.has_value()) {
        out.mult_buy = *st._buy_curve;
    } else {
        out.mult_buy = std::numeric_limits<double>::infinity();
    }
    if (res.sell.has_value()) {
        out.mult_sell= *st._sell_curve;
    } else {
        out.mult_sell = std::numeric_limits<double>::infinity();
    }
    return out;
}

const BollingerSpread::State& BollingerSpread::get_state(const PState &st) {
    return static_cast<const State &>(*st);
}


clone_ptr<ISpreadGen> bollingerSpreadGen(json::Value v) {
    double i = v["interval"].getNumber();
    double d = v["deviation"].getNumber()*0.01;
    double dev_calc = i * std::pow(10,d);
    unsigned int mean_points = std::max(5U, static_cast<unsigned int>(i * 60));
    unsigned int dev_points = std::max(5U, static_cast<unsigned int>(dev_calc * 60));
    json::Value curves = v["curves"];
    std::vector<double> c;
    std::transform(curves.begin(), curves.end(), std::back_inserter(c), [](json::Value v){
        return v.getNumber();
    });
    bool zl = v["zero_curve"].getBool();
    return clone_ptr<ISpreadGen>(new BollingerSpread(mean_points, dev_points, c, zl));


}


BollingerSpread::State::Iter BollingerSpread::State::next_buy(Iter x) const {
    if (x == &_curves.back()) return x;
    auto a = x;
    ++a;
    if (a == _disabled_curve) {
        if (a == &_curves.back()) return x;
        ++a;
    }
    return a;
}

BollingerSpread::State::Iter BollingerSpread::State::next_sell(Iter x) const {
    if (x == &_curves.front()) return x;
    auto a = x;
    --a;
    if (a == _disabled_curve) {
        if (a == &_curves.front()) return x;
        --a;
    }
    return a;
}

bool BollingerSpread::State::below(Iter x) const {
    return x < &_curves.front();
}

bool BollingerSpread::State::above(Iter x) const {
    return x > &_curves.back();
}
