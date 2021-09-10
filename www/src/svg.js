//@namespace mmbot

class SVG {
	constructor(root) {
		this._root = root;
		this._stack = [];
	};
	
	new_element(name, attrs, childOf) {
		var elem = document.createElementNS("http://www.w3.org/2000/svg", name);
		if (attrs) {
			for (var i in attrs) {
				if (attrs[i] !== undefined) {
					elem.setAttributeNS(null,i, attrs[i]);
				}
			}
		}
		if (childOf) childOf.appendChild(elem);
		return elem;		
	};
	
	create(width, height, xofs, yofs) {
		var x = xofs || 0;
		var y = yofs || 0;
		var vbx = x+" "+y+" "+width+" "+height;
		this._root = this.new_element("svg",{viewBox:vbx});
		this._stack = [];
	};

	clear() {
		while (this._root.firstChild) this._root.removeChild(this._root.firstChild);7
	}
	
	get() {
		return this._root;
	};
	
	pop() {
		let t = this._stack.pop();
		if (t) this._root = t;    
	};
	
	line(x1, y1, x2, y2) {
		this.new_element("line",{x1:x1,y1:y1,x2:x2,y2:y2},this._root);
	};
	circle(x, y,r) {
		this.new_element("circle",{cx:x,cy:y,r:r},this._root);
	};
	ellipse(x, y,rx,ry) {
		this.new_element("ellipse",{cx:x,cy:y,rx:rx,ry:ry},this._root);
	};
	rect(x, y,w,h,rx,ry) {
		this.new_element("rect",{x:x,y:y,width:w,height:h,rx:rx,ry:ry},this._root);
	};
	gradrect(x, y,w,h,rx,ry,vert) {
		let id = "svg"+(Math.random() + 1).toString(36).substring(7);
		let d = this.new_element("defs",{},this._root);
		let g = this.new_element("linearGradient",{id:id,x1:0,y1:0,x2:vert?"100%":0,y2:vert?0:"100%"},d);
		this.new_element("stop",{offset:"0%"},g);
		this.new_element("stop",{offset:"100%"},g);
		this.new_element("rect",{x:x,y:y,width:w,height:h,rx:rx,ry:ry,fill:"url(#"+id+")"},this._root);
	};
	polyline(points, className) {
		this.new_element("polyline",{points:points.map(function(x){
			return x.join(' ')
		}).join(','),class:className},this._root);
	};
	polygon(points, className) {
		this.new_element("polygon",{points:points.map(function(x){
			return x.join(' ')
		}).join(','),class:className},this._root);
	};
	push_group(className) {
		let e = this.new_element("g",{class:className},this._root);
		this._stack.push(this._root);
		this._root = e;
		return e;
	};	
	text(x,y,text,className) {
		let e = this.new_element("text",{x:x,y:y,class:className},this._root);
		this.set_text(e,text);
		return e;
	};
	text_space(x,y,text,len) {
		let r = (Math.random() + 1).toString(36).substring(7);
		this.new_element("path",{id:"path_"+r,d:"M"+x+","+y+" h"+len,stroke:"none",fill:"none"},this._root);
		let t = this.new_element("text",{},this._root);
		let e = this.new_element("textPath",{href:"#path_"+r},t);
		this.set_text(e,text);
		return e;
	};
	
	image(x,y,width,height,href,aspect) {
		return this.new_element("image",{x:x,y:y,width:width,height:height,href:href,preserveAspectRatio:aspect},this._root);ss		
	}
	push_viewport(x,y,width,height,vbx_x, vbx_y,vbx_width,vbx_height) {
		if (vbx_x === undefined) vbx_x = 0;
		if (vbx_y === undefined) vbx_y = 0;
		if (vbx_width === undefined) vbx_width = width;
		if (vbx_height === undefined) vbx_height = height;
		var vbx = vbx_x+" "+vbx_y+" "+vbx_width+" "+vbx_height;
		let e = this.new_element("svg", {
			x:x,y:y,width:width,height:height,viewBox:vbx
		},this._root);
		this._stack.push(this._root);
		this._root = e;
		return e;
	}

    set_text(e, text) {
		while (e.firstChild) e.removeChild(e.firstChild);
		if (Array.isArray(text)) {
			text.forEach(function(z){
				let n = this.new_element("tspan",{},e);				
				n.appendChild(document.createTextNode(z));				
			},this);
		} else {
			e.appendChild(document.createTextNode(text));
		}	
    }

};


mmbot.SVG = SVG;
