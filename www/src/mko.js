//@namespace mmbot

class MKO {
	constructor(fn, tm) {
		this.fn = fn;
		this.tm = tm;
		this.tid = null;
		this.promise = Promise.resolve();
	}	
	event(x) {
		if (this.tid) {
			clearTimeout(this.tid);
		} else {
			this.promise = new Promise(ok=>{this.ok = ok}) ;
		}			
		this.tid = setTimeout(()=>{
			this.tid = null;
			this.fn(x);			
			this.ok();
		},this.tm);
	}	
	wait() {
		return this.promise;
	}
};

mmbot.MKO = MKO;