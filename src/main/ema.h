#ifndef SRC_MAIN_EMA_H_
#define SRC_MAIN_EMA_H_


class EMA {
public:

    constexpr EMA(unsigned int period, double value = 0)
        : _value(value)
        , _multiplier(2.0/(period+1)) {}

    constexpr EMA(const EMA &src, double value)
        : _value(value)
        , _multiplier(src._multiplier) {}

    constexpr EMA operator+(double v) const {
        return EMA(*this, calc_new_value(v));
    }
    constexpr EMA &operator+=(double v) {
        _value = calc_new_value(v);
        return *this;
    }
    constexpr EMA(const EMA &) = default;

    constexpr double operator()() const {return _value;}

    void set_initial(double value) {_value = value;}

protected:
    double _value = 0;
    double _multiplier;

    constexpr double calc_new_value(double v) const {
        return (v - _value) * _multiplier + _value;
    }

};



#endif /* SRC_MAIN_EMA_H_ */
