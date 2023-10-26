#ifndef SRC_MAIN_BOLLINGER_SPREAD_H_
#define SRC_MAIN_BOLLINGER_SPREAD_H_

#include "emstdev.h"
#include "spread.h"

#include <vector>
class BollingerSpread: public ISpreadGen {
public:

    BollingerSpread(unsigned int mean_points, unsigned int stdev_points, std::vector<double> curves, bool zero_line);
    virtual SpreadStats get_stats(ISpreadGen::PState &state, double equilibrium) const override;
    virtual ISpreadGen::Result get_result(const ISpreadGen::PState &state,
            double equilibrium) const override;
    virtual void point(ISpreadGen::PState &state, double y,
            bool execution) const override;
    virtual unsigned int get_required_history_length() const override;
    virtual ISpreadGen::PState start() const override;
    virtual ISpreadGen* clone() const override;


protected:
    unsigned int _mean_points;
    unsigned int _stdev_points;
    std::vector<double> _curves;
    bool _zero_line;

    class State: public ISpreadGen::State {
    public:

        std::vector<double> _curves;
        EMStDev _stdev;
        unsigned int _active_line = 0;
        bool _inited = false;

        State(std::vector<double> c, EMStDev stdev):_curves(c), _stdev(stdev) {}
        virtual ISpreadGen::State* clone() const override;
    };

    static State &get_state(PState &st);
    static const State &get_state(const PState &st);
};

clone_ptr<ISpreadGen> bollingerSpreadGen(json::Value v);






#endif /* SRC_MAIN_BOLLINGER_SPREAD_H_ */
