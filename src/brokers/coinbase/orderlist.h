#include "../ws_support.h"
#include "../../main/istockapi.h"

#include <future>
#include <optional>

class OrderList {
public:
    
    
    
    struct Order: IStockApi::Order {    
        std::string product_id;
    };

    void add(Order order);
    void remove(const std::string_view id);
    void clear();
   
    template<typename Fn>
    void by_product(std::string_view product, Fn &&fn) const {
        for (const auto &order: _orders) {
            if (order.product_id == product) {
                fn(order);
            }
        }
    }
    
    template<typename Fn>
    bool update(std::string_view id, Fn &&fn) {
        auto iter = _index.find(id);
        if (iter == _index.end()) return false;
        fn(_orders[iter->second]);
        return true;
    }
    
    bool process_events(WsInstance::EventType event, json::Value data);

    bool is_ready() const;
    std::future<bool> get_ready();
    
    virtual Order fetch_order(const std::string_view &id) = 0;
    
    
    
protected:
    
    std::unordered_map<std::string_view, std::size_t> _index;
    std::vector<Order> _orders;
    bool _orders_ready = false;
    std::optional<std::promise<bool> > _orders_wait;
    
    bool process_data(json::Value data);

};
