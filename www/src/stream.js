//@require source.js
//@namespace mmbot

class DataStream extends mmbot.Source {
    constructor() {
        super();

        var src = new EventSource("./data", { withCredentials: true });
        src.onmessage = function(msg) {
            var jmsg = JSON.parse(msg.data);
            if (typeof jmsg == "string") {
                switch (jmsg) {
                    case "refresh": this.broadcast({ type: "refresh", state: true }); break;
                    case "end_refresh": this.broadcast({ type: "refresh", state: false }); break;
                    case "ping": this.broadcast({ type: "ping" }); break;
                };
            } else if (jmsg.type) {
                var fn = this["update_" + jmsg.type];
                if (fn)
                    fn.call(this, jmsg);
                delete jmsg.data;
                this.broadcast(jmsg);
            }
        }.bind(this);
        this.traders = {};
        this.log = [];
    }
    getTrader(symbol) {
        var x = this.traders[symbol];
        if (!x)
            x = this.traders[symbol] = {};
        return x;
    }
    update_order(msg) {
        var t = this.getTrader(msg.symbol);
        if (!t.orders)
            t.orders = {};
        t.orders[msg.dir < 0 ? "sell" : "buy"] = msg.data;
    }
    update_misc(msg) {
        var t = this.getTrader(msg.symbol);
		t.prev_misc = t.misc || msg.data;
        t.misc = msg.data;
    }
    update_info(msg) {
        var t = this.getTrader(msg.symbol);
        t.info = msg.data;
    }
    update_trade(msg) {
        var t = this.getTrader(msg.symbol);
        if (!t.trades)
            t.trades = {};
        t.trades[msg.id] = msg.data;
    }
    update_price(msg) {
        var t = this.getTrader(msg.symbol);
        t.price = msg.data;
    }
    update_error(msg) {
        var t = this.getTrader(msg.symbol);
        t.error = msg.data;
    }
    update_config(msg) {
        this.config = msg.data;
    }
    update_performance(msg) {
        this.performance = msg.data;
    }
    update_version(msg) {
        this.version = msg.data;
    }
    update_log(msg) {
        this.log.push(msg.data);
    }
};


mmbot.DataStream = DataStream;

