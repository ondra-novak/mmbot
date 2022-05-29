/**
 * @typedef {Element} NewType
 */

class TimeChart {

	/** Create new SVG element
	
	@param name name of element
	@param attrs attributes as assoc 
	@param childOf optional - if defeined, automaticaly append to specified node
	*/
	static svg_new(name, attrs, childOf) {
		var elem = document.createElementNS("http://www.w3.org/2000/svg", name);
		if (attrs) {for (var i in attrs) {
			elem.setAttributeNS(null,i, attrs[i]);
		}}
		if (childOf) childOf.appendChild(elem);
		return elem;
	}
	
	static svg_text(x,y,text,childOf) {
		let el = TimeChart.svg_new("text",{x:x.toFixed(2),y:y.toFixed(2)},childOf);
		el.appendChild(document.createTextNode(text));
	}

	/** Clear content of node */
	static svg_clear(node) {
		while (node.firstChild) {
			node.removeChild(node.firstChild);
		}
	}
	
	/** cfg = {
		width: <width>
		height: <height>
		padding: <padding>
		padding_x: <padding_x>
		padding_y: <padding_y>
		xaxis:  {
			step:<amount>
			width:<step in pixels>
			line: <class for vertical lines
			text: <class for text>
			label_step: <how often generate label>
			label_cb: <function to convert number to label>
		} 
		yaxis: {
			zero_line:<class for zero line>
			line:<class for horizontal line
			text:<class for text>
			label_cb: <function to convert number to label>
		}
	}
        	
	 */
	
	constructor(cfg) {
		this.cfg = cfg;
		this.svg = TimeChart.svg_new("svg",{"viewBox":"0 0 "+cfg.width+" "+cfg.height});
		this.curves = [];
		this.window = (cfg.xaxis.step/cfg.xaxis.width)*cfg.width;
		this.scroll_flag = 0;
		if (cfg.padding_x === undefined) cfg.padding_x = cfg.padding || 0;
		if (cfg.padding_y === undefined) cfg.padding_y = cfg.padding || 0;
	}
	
	
	/** cfg = {
		length: <count of vertices)
		source: <function which generates vertices [x,y,"marker_type","marker_class",<marker_scale>]
		autoscale: use points to calculate scale
		class: <draw class>
		type: "line","area","ohlc"
		ordered: <true/false - data are ordered - default true> 	
	} 
	*/
	add_curve(cfg) {
		if (cfg.length>0 && typeof cfg.source == "function") {
			if (cfg.ordered === undefined) cfg.ordered = true;
			if (cfg.type == "ohlc" && !cfg.ordered) throw new TypeError("TimeChart: OHLC chart must be ordered");
			if (!cfg.range) {
				if (cfg.ordered) {					
					const p1 = cfg.source(0);
					const p2 = cfg.source(cfg.length-1);
					cfg.range = [p1[0],p2[0]];				
				} else {
					let thisrng = [Number.MAX_VALUE, -Number.MAX_VALUE];
					for (let i = 0; i < cfg.length; ++i) {
						const p1 = cfg.source(i);
						if (thisrng[0] > p1[0]) thisrng[0] = p1[0];
						if (thisrng[1] < p1[1]) thisrng[1] = p1[0];
					}
					cfg.range = thisrng;
				}
			}
			this.curves.push(cfg);
		}
	}
	
	clear_curves() {
		this.curves = [];
	}
	

	
	calc_time_range() {
		let r =  this.curves.reduce((r,c)=>{
			if (c.range[0]<r[0]) r[0] = c.range[0];
			if (c.range[1]>r[1]) r[1] = c.range[1];
			return r;
		},[Number.MAX_VALUE, -Number.MAX_VALUE]);
		const p = this.cfg.padding_x/this.cfg.width * this.window; 
		r[0]-= p;
		r[1]+= p;
		return r;
	}
	
	static find_curve_index(curve, x) {
		let l = 0;
		let h = curve.length;
/*		let pl = curve.source(l-1);
		let ph = curve.source(h);*/
		while (l<h) {
			let m = Math.floor((l+h)/2.0);
			const pm = curve.source(m);
			if (x<pm[0]) {
				h = m;
			} else if (x>pm[0]) {
				l = m+1;
			} else {
				return m;
			}
		}
		return l;
		
	}
	
	static find_range_ordered(curve, from, to) {
		let f = TimeChart.find_curve_index(curve,from);
		if (f == curve.length) return [f,f];
		if (f>0) --f;
		let t = TimeChart.find_curve_index(curve,to);
		if (t<curve.length) t++;
		return [f,t];
	}

	static updateThisRange(pm, thisrng) {
		const y = pm[1];
		if (Array.isArray(y)) {
			y.forEach(y=>{
				if (thisrng[0]>y) thisrng[0] = y;
				if (thisrng[1]<y) thisrng[1] = y;					
			})
		} else if (y!==null) {
			if (thisrng[0]>y) thisrng[0] = y;
			if (thisrng[1]<y) thisrng[1] = y;									
		}			
	}
	
	static find_scale_ordered(curve, range) {
		let thisrng = [Number.MAX_VALUE, -Number.MAX_VALUE];		
		for (let i = range[0]; i < range[1]; ++i) {
			const pm = curve.source(i);
			TimeChart.updateThisRange(pm,thisrng);
		}
		return thisrng;
	}

	static find_scale_unordered(curve, range) {
		let pm = curve.source(0);
		let thisrng = [Number.MAX_VALUE, -Number.MAX_VALUE];		
		for (let i = 1; i < curve.length; ++i) {
			let qm = curve.source(i);
			if ((qm[0]>=range[0] && qm[0]<=range[1]) || (pm[0]>=range[0] && pm[0]<=range[1])) {
				TimeChart.updateThisRange(pm,thisrng);			
			}
			pm = qm;
		}
		if ((pm[0]>=range[0] && pm[0]<=range[1])) {
				TimeChart.updateThisRange(pm,thisrng);			
		}
		return thisrng;
	}
	
	calc_scale(range) {
		let r = this.curves.reduce((r,c)=>{
			if (c.autoscale) {
				var tr;
				if (c.ordered) {
					let tri = TimeChart.find_range_ordered(c,range[0],range[1])
					tr = TimeChart.find_scale_ordered(c,tri);
				} else {
					tr = TimeChart.find_scale_unordered(c,range);
				}
				if (r[0]>tr[0]) r[0] = tr[0];
				if (r[1]<tr[1]) r[1] = tr[1];
			}			
			return r;
		},[Number.MAX_VALUE, -Number.MAX_VALUE]);
		const h = r[1]-r[0];
		const p = this.cfg.padding_y/this.cfg.height * h;
		r[0]-=p;
		r[1]+=p;
		return r; 
	}

	map_x(x) {
		return (x - this.trange[0])*this.cfg.width/(this.trange[1]-this.trange[0]);
	}
	map_y(y) {
		return this.cfg.height - (y - this.srange[0])*this.cfg.height/(this.srange[1]-this.srange[0]);
	}
	
	calc_time_range_window(pos) {
		let r = this.calc_time_range();
		let b = r[0]+pos;
		let e = b+this.window;
		if (e>r[1] && r[1]-r[0]<this.window) e=r[1];
		return [b,e];
	}
	
	draw(pos) {	
		let trange = this.calc_time_range_window(pos);;
		let srange = this.calc_scale(trange);
		if (srange[0]>=srange[1]) return false;
		TimeChart.svg_clear(this.svg);
		this.trange = trange;
		this.srange = srange;
		let axis = TimeChart.svg_new("g",{},this.svg);
		let curves = TimeChart.svg_new("g",{},this.svg);
		let mrks = TimeChart.svg_new("g",{},this.svg);
		let labels = TimeChart.svg_new("g",{},this.svg);
		
		
		(()=>{
			const step = this.cfg.xaxis.step;
			const beg = Math.floor(this.trange[0]/step);
			const end = Math.ceil(this.trange[1]/step);
			let textgrp = TimeChart.svg_new("g",{class:this.cfg.xaxis.text});
			let path = [];
			for (let i = beg; i<end;i++) {
				const v = i*step;
				const x = this.map_x(v);
				path.push("M",x,0,"V",this.cfg.height);
				if ((i % this.cfg.xaxis.label_step) == 0) {				
					TimeChart.svg_text(x,this.cfg.height,this.cfg.xaxis.label_cb(v),textgrp);
				}
			}
			TimeChart.svg_new("path",{d:path.join(" "),class:this.cfg.xaxis.line},axis);
			labels.appendChild(textgrp);
		})();

		(()=>{
			let rowstep = Math.pow(10,Math.floor(Math.log10((this.srange[1]-this.srange[0])/1)));
			let rowbeg= Math.floor(this.srange[0]/rowstep);
			let rowend= Math.floor(this.srange[1]/rowstep);	
			while (rowend - rowbeg < 4) {
				rowstep = rowstep/2;
				rowbeg= Math.floor(this.srange[0]/rowstep);
				rowend= Math.floor(this.srange[1]/rowstep);	
			}			
			let textgrp = TimeChart.svg_new("g",{class:this.cfg.yaxis.text});
			let zeropath = [];
			let path = [];
			for (let i = rowbeg; i <=rowend; i++) {
				let v = i*rowstep;
				let maj = Math.abs(v)<rowstep/2;
				let y = this.map_y(v);
				let p;
				if (maj && this.cfg.yaxis.zero_line) {
					p = zeropath;
				} else {
					p = path;
				}
				p.push("M",0,y,"H",this.cfg.width);
				TimeChart.svg_text(0,y,this.cfg.yaxis.label_cb(v),textgrp);				
			}
			TimeChart.svg_new("path",{d:path.join(" "),class:this.cfg.yaxis.line},axis);
			if (zeropath.length) {
				TimeChart.svg_new("path",{d:zeropath.join(" "),class:this.cfg.yaxis.zero_line},axis);
			}

			labels.appendChild(textgrp);
		
		})();
		
		let markers = {};
		
		const put_marker = (x,y,v)=>{
			let mrk = markers[v[3]];
			if (!mrk) mrk = markers[v[3]] = [];
			let s = v[4]*4;
			switch (v[2]) {
				case 'o': 
					mrk.push("M",x-s,y,"a",s,s,"0 0 0",s*2,0,"a",s,s,"0 0 0",-s*2,0);
					break;
				case 'X':
				case 'x':
					mrk.push("M",x-s,y-s,"l",2*s,2*s);
					mrk.push("M",x-s,y+s,"l",2*s,-2*s);
					break;
				case 'V':
				case 'v':
					mrk.push("M",x-s,y-s,"h",2*s);
					mrk.push("l",-s,2*s,"Z");
					break;
				case '^':
				case 'A': 
					mrk.push("M",x-s,y+s,"h",2*s);
					mrk.push("l",-s,-2*s,"Z");
					break;
				case '+': 
					mrk.push("M",x-s,y,"h",2*s);
					mrk.push("M",x,y-s,"v",2*s);
					break;				
				case '[]':
					mrk.push("M",x-s,y-s);
					mrk.push("h",2*s);
					mrk.push("v",2*s);
					mrk.push("h",-2*s);
					mrk.push("Z");
					break;
			}
		};
		
		this.curves.forEach(c=>{
			if (c.type == "ohlc") {
				let r = TimeChart.find_range_ordered(c,this.trange[0],this.trange[1]);
				let bull_path = [];
				let bear_path = [];
				let bull_wick_path = [];
				let bear_wick_path = [];
				let lastx = this.map_x(c.source(r[0])[0]);					
				for (let i = r[0]+1;i < r[1]; i++) {
					let v = c.source(i);
					let x = this.map_x(v[0]);
					if (v[1] === null) {
						lastx = x;
					} else {
						const ohlc = v[1];
						const yo = this.map_y(ohlc[0]);
						const yh = this.map_y(ohlc[1]);
						const yl = this.map_y(ohlc[2]);
						const yc = this.map_y(ohlc[3]);
						const ym = Math.max(yo,yc);
						const yn = Math.min(yo,yc);
						const mx = (lastx+x)*0.5;
						const dx = x-lastx;
						let path = yo>yc?bull_path:bear_path;
						let wick = yo>yc?bull_wick_path:bear_wick_path;
						path.push("M",lastx,ym,"h",dx,"v",yn-ym,"h",-dx,"Z");
						wick.push("M",mx,yn,"v",(yh-yn),"M",mx,ym,"v",(yl-ym));						
						lastx = x;
					}
				}													
				TimeChart.svg_new("path",{d:bull_path.join(" "),class:c.class+" bull"},curves);
				TimeChart.svg_new("path",{d:bear_path.join(" "),class:c.class+" bear"},curves);
				TimeChart.svg_new("path",{d:bull_wick_path.join(" "),class:c.class+" bull wick"},curves);
				TimeChart.svg_new("path",{d:bear_wick_path.join(" "),class:c.class+" bear wick"},curves);
			} else {
				let path = [];
				if (c.ordered) {
					let r = TimeChart.find_range_ordered(c,this.trange[0],this.trange[1]);
					let nx_pt = "M";
					if (c.type == "area") {
						let rpath = [];					
						const flushrpath = ()=>{
							if (rpath.length) {
								while (rpath.length) {
									path.push(rpath.pop());
								}
								path.push("Z");
							}
						}
						for (let i = r[0];i < r[1]; i++) {
							let v = c.source(i);
							if (v[1]===null) {
								nx_pt = "M";
								flushrpath();
							} else {
								let x = this.map_x(v[0]);
								let y1 = this.map_y(v[1][0])
								let y2 = this.map_y(v[1][1])
								path.push(nx_pt,x,y1);
								put_marker(x,y1,v);
								nx_pt = "L";
								rpath.push(y2,x,nx_pt);
								put_marker(x,y2,v);
	
							}				
						}
						flushrpath();
					} else {
						for (let i = r[0];i < r[1]; i++) {
							let v = c.source(i);
							if (v[1]===null) {
								nx_pt = "M";
							} else {
								let x = this.map_x(v[0]);
								let y = this.map_y(v[1])
								path.push(nx_pt,x,y);
								put_marker(x,y,v);
								nx_pt = "L";
							}				
						}
					}
				} else {
					if (c.type == "area") {
						let rpath = [];					
						const flushrpath = ()=>{
							if (rpath.length) {
								while (rpath.length) {
									path.push(rpath.pop());
								}
								path.push("Z");
							}
						}
						let v = c.source(0);
						let mx = this.map_x(v[0]);
						let my1 = this.map_y(v[1][0]);
						let my2 = this.map_y(v[1][1]);
						let vs = false;				
						let mv = true;
						for (let i = 0; i < c.length; ++i) {
							v = c.source(i);
							if (v[1] !== null) {
								if (v[0]>=this.trange[0] && v[0] <= this.trange[1] && mv) {
									if (!vs) {
										path.push("M",mx,my1);
										rpath.push(my2,mx,"L");	
										put_marker(mx,my1,v);					
										put_marker(mx,my2,v);
									}
									vs = true;
									mx = this.map_x(v[0]);
									my1 = this.map_y(v[1][0]);
									my2 = this.map_y(v[1][1]);
									path.push("L",mx,my1);
									rpath.push(my2,mx,"L");	
									put_marker(mx,my1,v);					
									put_marker(mx,my2,v);
								} else {
									mx = this.map_x(v[0]);
									my1 = this.map_y(v[1][0]);
									my2 = this.map_y(v[1][1]);
									if (vs) {
										path.push("L",mx,my1);
										rpath.push(my2,mx,"L");	
										put_marker(mx,my1,v);					
										put_marker(mx,my2,v);
									}
									vs = false;
									flushrpath();
								}
								mv = true;
							} else {
								vs = false;
								mv = false;
								flushrpath();
							}
						}
						flushrpath();
					} else {
						let v = c.source(0);
						let mx = this.map_x(v[0]);
						let my = this.map_y(v[1]);
						let vs = false;				
						let mv = true;
						for (let i = 0; i < c.length; ++i) {
							v = c.source(i);
							if (v[1] !== null) {
								if (v[0]>=this.trange[0] && v[0] <= this.trange[1] && mv) {
									if (!vs) {
										path.push("M",mx,my);	
										put_marker(mx,my,v);
									}
									vs = true;
									mx = this.map_x(v[0]);
									my = this.map_y(v[1]);
									path.push("L",mx,my);
									put_marker(mx,my,v);					
								} else {
									mx = this.map_x(v[0]);
									my = this.map_y(v[1]);
									if (vs) {
										path.push("L",mx,my);								
									}
									vs = false;
								}
								mv = true;
							} else {
								vs = false;
								mv = false;
							}
						}
					}
				}
				TimeChart.svg_new("path",{d:path.join(" "),class:c.class},curves);
			}
		});	
		for (let n in markers) {
			TimeChart.svg_new("path",{d:markers[n].join(" "),class:n},mrks);
		}
		return true;		
	}
	
	map_pixel_to_xy(clientX, clientY) {
		const pt = DOMPoint.fromPoint({x:clientX, y:clientY});
		const svgP = pt.matrixTransform(this.svg.getScreenCTM().inverse());
		return [svgP.x/this.cfg.width*(this.trange[1]-this.trange[0])+this.trange[0],
			    (this.cfg.height - svgP.y)/this.cfg.height*(this.srange[1]-this.srange[0])+this.srange[0]];					
	}
	
	get_scrollbar_range() {
		let w;
		if (this.svg.isConnected) {
			w = this.svg.clientWidth;
		} else {
			w = this.cfg.width;
		}
		let s = w/this.window;
		let r = this.calc_time_range();
		let rr = (r[1] - r[0])*s;
		return [rr,w];		
	}
	
	scrollbar_to_pos(pos) {
		let w;
		if (this.svg.isConnected) {
			w = this.svg.clientWidth;
		} else {
			w = this.cfg.width;
		}
		let s = w/this.window;
		let rr = pos/s;
		return rr;
	}
	
	get_pos() {
		const rng = this.calc_time_range();		
		const curpos = this.trange[0]-rng[0];
		return curpos;		
	}

	///stops any pending rezidual scrolling 
	stop_scroll() {
		this.scroll_flag=(this.scroll_flag+1) % (256*65536);
		console.log(this.scroll_flag);
	}
	
	scroll_begin(state,clientX) {
		if (!state.stop_inertia) {
			state.stop_inertia = ()=> {
				if (state.scroll_inertia) {
					clearInterval(state.scroll_inertia);
					delete state.scroll_inertia; 						
				}		
			}
		}
		state.scroll_clientX = clientX;
		state.stop_inertia();
		state.flag = this.scroll_flag;
		if (state.cb) state.cb(this.get_pos());
	}
	scroll_move(state,clientX) {
		const rng = this.calc_time_range();
		const curpos = this.trange[0]-rng[0];
		if (state.scroll_clientX !== undefined) {
			const distance = this.cfg.scroll_trig_distance || 20;
			if (Math.abs(clientX - state.scroll_clientX)>distance) {
				const pt = this.map_pixel_to_xy(state.scroll_clientX, 0);
				document.body.style.cursor = this.svg.style.cursor = "ew-resize";
				state.scroll_pos = curpos;
				state.scroll_x = pt[0]-this.trange[0];
				state.scroll_inertia = setInterval(()=>{
					if (!this.svg.isConnected || this.scroll_flag != state.flag) {
						state.stop_inertia();
						return;
					}
					if (state.scroll_inertia_measure) {
						const pos = this.get_pos();
						state.scroll_inertia_lastpos = (state.scroll_inertia_lastpos+pos)*0.5;
						state.scroll_inertia_curpos = pos; 
					} else {
						const speed = state.scroll_inertia_curpos - state.scroll_inertia_lastpos;
						const newpos = state.scroll_inertia_curpos + speed*0.96;
						const maxpos = rng[1]-rng[0]-this.window;
						const finpos = Math.max(0,Math.min(maxpos,newpos));
						if (Math.abs(finpos - state.scroll_inertia_curpos)<0.1*this.window/this.cfg.width) {
							state.stop_inertia();							
						} else {
							state.scroll_inertia_lastpos = state.scroll_inertia_curpos;
							state.scroll_inertia_curpos = finpos;
							this.draw(finpos);
							if (state.cb) state.cb(finpos); 
						}
					}
				},25);
				state.scroll_inertia_measure = true;
				state.scroll_inertia_lastpos = state.scroll_inertia_curpos = curpos;
				delete state.scroll_clientX;		
			}
		}
		if (state.scroll_x !== undefined) {
			const pt = this.map_pixel_to_xy(clientX,0);
			const diff = pt[0] - state.scroll_x-this.trange[0];
			const maxpos = rng[1]-rng[0]-this.window;
			const finpos = Math.max(0,Math.min(maxpos,state.scroll_pos - diff));			
			this.draw(finpos);
			if (state.cb) state.cb(finpos);			
		}
	}
	scroll_end(state,clientX) {		
		this.scroll_move(state,clientX);
		if (state.scroll_clientX !== undefined) {
			delete state.scroll_clientX;
		}
		if (state.scroll_x !== undefined) {
			delete state.scroll_x;
			delete state.scroll_pos;
			state.scroll_inertia_measure = false;
		}
		document.body.style.cursor = this.svg.style.cursor = "";
	}
	
	install_scroll_handlers(slave_charts) {
		let state = {};
		let msmove, msup, tchmove, tchend;
		if (!slave_charts) slave_charts = [];
		
		state.cb = (x)=> {
			slave_charts.forEach(y=>{
				y.stop_scroll();
				y.draw(x);					
			});
		};
	
		this.svg.addEventListener("mousedown",(ev)=>{
			if (ev.button == 0) {					
				this.scroll_begin(state,ev.clientX);
				msmove = (e)=>{
					var rect = this.svg.getBoundingClientRect();
	      			var x = e.clientX - rect.left; 
					this.scroll_move(state,x);
					e.preventDefault();      			
				}
				msup = (e)=>{
					if (e.button == 0) {
						var rect = this.svg.getBoundingClientRect();
		      			var x = e.clientX - rect.left; 
						this.scroll_end(state,x);
						document.removeEventListener("mousemove",msmove);
						document.removeEventListener("mouseup",msup);
						e.preventDefault();
					}				      							
				}
			}
			document.addEventListener("mousemove",msmove);
			document.addEventListener("mouseup",msup);
			ev.preventDefault();
		});
	
		this.svg.addEventListener("touchstart",(ev)=>{
			if (ev.changedTouches[0]) {
				this.scroll_begin(state,ev.changedTouches[0].clientX);
				tchmove = (e)=>{
					var rect = this.svg.getBoundingClientRect();
	      			var x = e.changedTouches[0].clientX - rect.left; 
					this.scroll_move(state,x);    			
				}
				tchend = (e)=>{
					if (e.changedTouches[0]) {
						var rect = this.svg.getBoundingClientRect();
		      			var x = e.changedTouches[0].clientX - rect.left; 
						this.scroll_end(state,x);
						document.removeEventListener("touchmove",tchmove);
						document.removeEventListener("touchend",tchend);
						e.preventDefault();
					}				      							
				}
			}
			document.addEventListener("touchmove",tchmove);
			document.addEventListener("touchend",tchend);			
			ev.preventDefault();
	});


	}
	
};