#include "ratio.h"
#include "assets.h"
#include "client.h"

double RatioTable::get_ratio(const Pair &pair, const XTBAssets &assets, XTBClient &client) {
    if (pair.from == pair.to) return 1.0;
    std::shared_ptr<IFXRate> r;
    {
        std::mutex _mx;
        auto iter = _pairs.find(pair);
        if (iter == _pairs.end()) {
            auto p = assets.get_ratio(pair.from, pair.to, client);
            if (!p) return 0.0;
            iter = _pairs.emplace(pair, p).first;
        }
        r = iter->second;
    }
    r->wait();
    return r->get_rate();
}

bool RatioTable::CmpPair::operator ()(const Pair &a, const Pair &b) const {
    return a.from == b.from && a.to == b.to;
}

std::size_t RatioTable::HashPair::operator ()(const Pair &a) const {
    std::hash<std::string> hasher;
    return hasher(a.from) ^ hasher(a.to);
}

void RatioTable::clear() {
    std::mutex _mx;
    _pairs.clear();
}
