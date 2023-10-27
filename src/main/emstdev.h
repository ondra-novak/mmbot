#ifndef SRC_MAIN_EMSTDEV_H_
#define SRC_MAIN_EMSTDEV_H_

#include "ema.h"

#include <math.h>
class EMStDev {
public:

    constexpr EMStDev(unsigned int period)
        :_mean(period), _variance(period) {}
    constexpr EMStDev(unsigned int mean_period, unsigned int variance_period)
        :_mean(mean_period), _variance(variance_period) {}

    constexpr EMStDev(const EMA &mean, const EMA &variance)
        :_mean(mean),_variance(variance) {}

    constexpr EMStDev operator+(double v) const {
        EMA new_mean = _mean+v;
        EMA new_var = _variance + pow2(v - new_mean());
        return EMStDev(new_mean, new_var);
    }
    constexpr EMStDev &operator+=(double v)  {
        _mean += v;
        _variance += pow2(v - _mean());
        return *this;
    }

    double get_mean() const {
        return _mean();
    }
    double get_stdev() const {
        return std::sqrt(_variance());
    }

    void set_initial(double value, double variace) {
        _mean.set_initial(value);
        _variance.set_initial(pow2(variace));
    }
    double operator()(double x) const {
        return get_mean() + get_stdev() * x;
    }

protected:
    EMA _mean;
    EMA _variance;

    static constexpr double pow2(double x) {
        return x*x;
    }
};


#endif /* SRC_MAIN_EMSTDEV_H_ */
