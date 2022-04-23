"use strict";
class Lang {
	
    constructor() {
		this.strtbl = {};
    };

    load(id) {
		return fetch("lang/"+id+".json").then(x=>x.json()).then(a=>{
			this.strtbl = a;
			this.translate_node(document.body);
		});
    };

	async get_list() {
		var req =await fetch("lang/index.json");
		return await req.json();
	}

	translate_node(node, def_cat) {
		var lst = node.querySelectorAll("[data-strid]");
		Array.prototype.forEach.call(lst,el=>{
			var id = el.dataset.strid;
			var dot = id.indexOf(".")
			var cat;
			if (dot != -1) {
				cat = id.substr(0,dot);
				id = id.substr(dot+1);
			} else if (def_cat) {
				cat = def_cat;
				el.dataset.strid=cat+"."+id;
			} else {
				return;
			}
			let txt = this.get_text_impl(cat, id);
			if (txt !== undefined) {
				if (!Array.isArray(txt)) txt = [txt]; 			
				this.walk_nodes(el,  txt);					
			}
		});
	};
	
	set_node_text(node, cat, id, def) {
		node.dataset.strid = cat+"."+id;
		node.innerText = this.get_text(cat, id, def);
		return node;
	}
	
	walk_nodes(nd, txt) {
			var chld = nd.firstChild;
			while (chld != null && txt.length) {
				if (chld.nodeType == 3) {
					if (chld.textContent.trim().length) {
						chld.textContent = txt[0];
						txt.shift();
					}
				} else {
					this.walk_nodes(chld, txt);					
				}
				chld = chld.nextSibling;
			}			
	};
	
	get_text_impl(cat, id, def) {
		let tbla = this.strtbl[cat];
		if (tbla) {
			let txt = tbla[id]; 
			if (txt) {
				return txt;						
			}
		}
		return def;
	};
	
	get_text(cat, id, def) {
		var txt = this.get_text_impl(cat, id,def);
		if (typeof txt != "string" && txt !== undefined && txt.toString) txt = txt.toString();
		return txt;
	}
	
};


