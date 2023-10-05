#pragma once
#ifndef SRC_BROKERS_XTB_RATIO_H_
#define SRC_BROKERS_XTB_RATIO_H_
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>



class IFXRate {
public:
    virtual double get_rate() const = 0;
    virtual bool is_ready() const = 0;
    virtual void wait() const = 0;
    virtual ~IFXRate() = default;
};


class XTBAssets;
class XTBClient;


class RatioTable {
public:

    struct Pair {
        std::string from;
        std::string to;
    };

    double get_ratio(const Pair &pair, const XTBAssets &assets, XTBClient &client);
    void clear();

protected:

    struct CmpPair {
        bool operator()(const Pair &a, const Pair &b) const;
    };
    struct HashPair {
        std::size_t operator()(const Pair &a) const;
    };

    std::mutex _mx;
    std::unordered_map<Pair, std::shared_ptr<IFXRate>, HashPair, CmpPair> _pairs;

};



#endif /* SRC_BROKERS_XTB_RATIO_H_ */
