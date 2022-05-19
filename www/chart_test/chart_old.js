class TimeChart {	
	
	
	constructor(height, mins_in_pixel) {
		this.height = height;
		this.mils_width = 1/(60*1000*mins_in_pixel);
		this.elem = this.new_svg_el("svg");
		this.vrange = null;
		this.hrange = null;		
		this.date_step = Math.ceil((120/this.mils_width)/(24*60*60*1000));
		this.unregScoll = null;				
	}
	
	get_svg() {		
		this.elem.setAttribute("viewBox","0 0 "+this.map_x_to_pixel(this.hrange[1])+" "+this.height);
		this.elem.setAttribute("height",this.height);
		return this.elem;
	}
	
	clear() {
		while (this.elem.firstChild) this.elem.removeChild(this.elem.firstChild);
		this.vrange = null;
		this.hrange = null;
		this.vaxis = null;
		
	}
	
	
	update_range(data, cb_map) {
		if (data.length) {
			let v = cb_map(data[0]);
			let vl = cb_map(data[data.length-1]);
			if (this.vrange === null) {
				this.vrange = [v[1],v[1]];				
			} 
			if (this.hrange === null) {
				this.hrange = [v[0],v[0]];				
			} 
			if (this.hrange[0] > v[0]) this.hrange[0] = v[0];
			if (this.hrange[1] < vl[0]) this.hrange[1] = vl[0];			
			
			this.vrange = data.reduce((a,b)=>{
				let v = cb_map(b);
				if (a[0] > v[1]) a[0] = v[1];
				if (a[1] < v[1]) a[1] = v[1];
				return a;				
			},this.vrange);
		}
	}
	
	update_range_hline(y) {
		if (!this.range) this.range = [y,y];
		else {
			if (this.range[0]>y) this.range[0] = y;
			if (this.range[1]<y) this.range[1] = y;
		}
	}
	
	apply_margin(margin) {
		let h = this.vrange[1]-this.vrange[0];
		if (h <= 0) h = 1;
		let z = (margin*h)/(this.height-2*margin);  //(mr)/(h-2m)
		let t = margin/this.mils_width;
		this.vrange[0]-=z;
		this.vrange[1]+=z;
		this.hrange[0]-=t;
		this.hrange[1]+=t;   		
	}	

	/*
	format = {
		x:{text: class, line:class},
		y:{text: class, line:class},
		zero:{text: class, line: class}
	}
	*/
	
	map_y_to_pixel(y) {
	    return this.height-(y-this.vrange[0])/(this.vrange[1]-this.vrange[0])*this.height;
	}
	
	map_x_to_pixel(x) {
		return (x-this.hrange[0])*this.mils_width;
	}
	
	plot_axis(format) {		
		const daymilis = 24*60*60*1000;
		const days = Math.floor((this.hrange[1]-this.hrange[0])/daymilis)+1;
		const bday = Math.floor(this.hrange[0]/daymilis);
		const eday = Math.ceil(this.hrange[1]/daymilis);
		let date = new Date();		
		let gtext = this.new_svg_el("g",{"class":format.x.text},this.elem);
		let pathstr="";
		let dpos = 0;
		for (let d = bday; d <= eday; ++d) {
			let dx = d * daymilis;
			let x = this.map_x_to_pixel(dx);
			pathstr=pathstr+"M"+x+" 0 l0 "+this.height+" ";
			if (dpos == 0) {
				date.setTime(dx);
				let datestr = date.toLocaleDateString();
				this.new_svg_el("text",{x:x+2,y:this.height},gtext).appendChild(document.createTextNode(datestr));
			}
			dpos = (dpos + 1) % this.date_step;
		}
		this.new_svg_el("path",{d:pathstr,class:format.x.line},this.elem);	
		
		const rowstep = Math.pow(10,Math.floor(Math.log10((this.vrange[1]-this.vrange[0])/3)));
		const rowbeg= Math.floor(this.vrange[0]/rowstep);
		const rowend= Math.floor(this.vrange[1]/rowstep);	
		const width=this.map_x_to_pixel(this.hrange[1]);
	
		pathstr = "";
		gtext = this.new_svg_el("g",{"class":format.y.text},this.elem);
		this.vaxis = gtext;
		for (var i = rowbeg; i <=rowend; i++) {
			var v = i*rowstep;
			var maj = Math.abs(v)<rowstep/2;
			var y = this.map_y_to_pixel(v);
			let txt = adjNum(i*rowstep);
			if (maj) {
				const f = format.zero
				new_svg_el("line",{x1:0,y1:y,x2:width,y2:y,class:f.line},this.elem);
				new_svg_el("text",{x:2,y:y-2,class:f.text},this.elem).appendChild(document.createTextNode(txt));
			} else {
				pathstr=pathstr+"M0 "+y+" l"+width+" 0 ";
				new_svg_el("text",{x:2,y:y-2,class:format.y.text},gtext).appendChild(document.createTextNode(txt))
			}
		}
		if (pathstr) {
			this.new_svg_el("path",{d:pathstr,class:format.y.line},this.elem);
		}	
		
	}	
	
	plot_curve(data, cb_map, line_class) {
		let pathstr = "";
		if (data.length) {
			let v = cb_map(data[0]);
			pathstr = data.reduce((path,pt)=>{
				let v = cb_map(pt);
				path = path+" L"+this.map_x_to_pixel(v[0])+" "+this.map_y_to_pixel(v[1]);
				return path;
			},"M"+this.map_x_to_pixel(v[0])+" "+this.map_y_to_pixel(v[1]));
			this.new_svg_el("path",{d:pathstr,class:line_class},this.elem);			
		}
	}
	
	///cb_map = returns [x,y,fig,class,size] = figures: x, o, A, +, []
	plot_vertices(data, cb_map) {
		let groups={};
		data.forEach(pt=>{
			const ptm = cb_map(pt);
			const cls = ptm[3] || "";
			const fig = ptm[2] || "o";
			const sz =  ptm[4] || 1;			
			if (!groups[cls]) groups[cls] = this.new_svg_el("g",{class:cls},this.elem);
			let e =groups[cls];
			const x = this.map_x_to_pixel(ptm[0]);
			const y = this.map_y_to_pixel(ptm[1]);
			switch(fig) {
				case 'o': this.new_svg_el("circle",{cx:x,cy:y,r:4*sz},e);break;
				case 'x': this.new_svg_el("path",{d:
					"M "+(x-4*sz)+" "+(y-4*sz)+" L"+(x+4*sz)+" "+(y+4*sz)
				   +"M "+(x+4*sz)+" "+(y-4*sz)+" L"+(x-4*sz)+" "+(y+4*sz)},e);break;
				case 'V': this.new_svg_el("path",{d:
					"M "+(x-4*sz)+" "+(y-4*sz)+" L"+(x)+" "+(y+4*sz)
				   +"L "+(x+4*sz)+" "+(y-4*sz)+" Z"},e);break;
				case '^':
				case 'A': this.new_svg_el("path",{d:
					"M "+(x-4*sz)+" "+(y+4*sz)+" L"+(x)+" "+(y-4*sz)
				   +"L "+(x+4*sz)+" "+(y+4*sz)+" Z"},e);break;
				case '+': this.new_svg_el("path",{d:
					"M "+(x)+" "+(y-4*sz)+" L"+(x)+" "+(y+4*sz)
				   +"M "+(x+4*sz)+" "+(y)+" L"+(x-4*sz)+" "+(y)},e);break;
				case '[]': this.new_svg_el("path",{d:
					"M "+(x-4*sz)+" "+(y-4*sz)+" L"+(x+4*sz)+" "+(y-4*sz)
				   +"L "+(x+4*sz)+" "+(y+4*sz)+" L"+(x-4*sz)+" "+(y+4*sz)+" Z"},e);break;
			}; 
		});
	}
	
	new_svg_el(name, attrs, childOf) {
		var elem = document.createElementNS("http://www.w3.org/2000/svg", name);
		if (attrs) {for (var i in attrs) {
			elem.setAttributeNS(null,i, attrs[i]);
		}}
		if (childOf) childOf.appendChild(elem);
		return elem;
	}
	
	update_vaxis_pos() {
		const el = TimeChart.getScrollParent(this.elem);
		if (!el) return;
		const rect = el == document?{x:0,y:0}:el.getBoundingClientRect();
	  	const pt = DOMPoint.fromPoint({x:rect.x, y:rect.y});
		const svgP = pt.matrixTransform(this.elem.getScreenCTM().inverse());
		if (this.vaxis) {
			this.vaxis.setAttribute("transform","translate("+svgP.x+" 0)");
		}
	}

	static getScrollParent(node) {
	  if (node == null) {
	    return null;
	  }	
	  if (node.scrollWidth > node.clientWidth) {
		if (node == document.body) node = document;
	    return node;
	  } else {
	    return TimeChart.getScrollParent(node.parentNode);
	  }
	}
	
	handle_scroll() {
		let el = TimeChart.getScrollParent(this.elem);
		if (el === null) return false;
		const scroller = ()=>{
			if (this.elem.isConnected)	this.update_vaxis_pos();
			else this.unregScoll();
		}
		el.addEventListener("scroll",scroller);
		this.unregScoll = ()=>{
			el.removeEventListener("scroll",scroller);
			this.unregScoll = null;
		}
	}
	
	map_pixel_to_xy(clientX, clientY) {
		const pt = DOMPoint.fromPoint({x:clientX, y:clientY});
		const svgP = pt.matrixTransform(this.elem.getScreenCTM().inverse());
		return [
			svgP.x/this.mils_width+this.hrange[0],
			(this.height- svgP.y)/this.height*(this.vrange[1]-this.vrange[0])+this.vrange[0]
		]	
	}
}	
	