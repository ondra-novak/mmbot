#include "bollinger_spread.h"
#include <imtjson/value.h>

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

    unsigned int idx = st._active_line+1;
    while (idx < st._curves.size()) {
        double y = st._stdev(st._curves[idx]);
        if (y > equilibrium) {
            r.sell = y;
            break;
        }
        ++idx;
    }
    idx = st._active_line;
    while (idx > 0) {
        --idx;
        double y = st._stdev(st._curves[idx]);
        if (y < equilibrium) {
            r.buy = y;
            break;
        }
    }
    return r;
}

void BollingerSpread::point(ISpreadGen::PState &state, double y,
        bool execution) const {
    State &st = get_state(state);
    if (st._inited) {
        if (execution) {

            double yref = st._stdev(st._curves[st._active_line]);
            if (y < yref) {
                if (st._active_line>0) {
                    --st._active_line;
                    while (st._active_line>0) {
                        yref = st._stdev(st._curves[st._active_line-1]);
                        if (y >= yref) {
                            break;
                        }
                        --st._active_line;
                    }
                }
            } else if (y > yref) {
                if (st._active_line+1 < st._curves.size()) {
                    ++st._active_line;
                    while (st._active_line+1 < st._curves.size()) {
                        yref = st._stdev(st._curves[st._active_line+1]);
                        if (y < yref) {
                            break;
                        }
                        ++st._active_line;
                    }
                }
            }


        } else {
            st._stdev += y;
        }
    } else {
        st._stdev.set_initial(y, 0.01*y);
        auto init = std::lower_bound(st._curves.begin(), st._curves.end(), 0.0);
        if (init == st._curves.end()) {
            st._active_line = st._curves.size()-1;
        } else {
            st._active_line = std::distance(st._curves.begin(), init);
        }
        st._inited = true;
    }

}

unsigned int BollingerSpread::get_required_history_length() const {
    return std::max(_mean_points, _stdev_points);
}

SpreadStats BollingerSpread::get_stats(ISpreadGen::PState &state, double equilibrium) const {
    const State &st = get_state(state);
    SpreadStats out;
    out.spread = st._stdev.get_stdev()/st._stdev.get_mean();
    double y = st._stdev(st._curves[st._active_line]);
    Result res =get_result(state,  equilibrium);
    if (res.buy.has_value()) {
        out.mult_buy = (y - *res.buy) / out.spread;
    } else {
        out.mult_buy = std::numeric_limits<double>::infinity();
    }
    if (res.sell.has_value()) {
        out.mult_sell= (*res.sell - y) / out.spread;
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
    bool zl = v["zero_line"].getBool();
    return clone_ptr<ISpreadGen>(new BollingerSpread(mean_points, dev_points, c, zl));


}
