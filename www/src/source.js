//@namespace mmbot

class Source {
    constructor() {
        this.waitPromise = null;
        this._waitFn = function() { };
        this.broadcast({});
    }
    listen() {
        return this.waitPromise;
    }
    broadcast(obj) {
        var ntf = this._waitFn;
        this.waitPromise = new Promise(function(ok) { this._waitFn = ok; }.bind(this));
        obj.next = this.waitPromise;
        ntf(obj);
    }
};



mmbot.Source = Source;
