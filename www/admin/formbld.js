"use strict";
class FormBuilder {
	constructor(formdesc, langobj, langcat, category) {
		this.formdesc = category?formdesc.filter(x=>x.category == category):formdesc;
		this.langobj = langobj;
		this.langcat = langcat;
		
		var refs = {};
		this.rule_fields = {};
		
		function add_ref(itm) {
			Object.keys(itm).forEach(x=>{
				if (x && x[0] == '$') add_ref(itm[x]);
				else refs[x] = 1;
			});			
		}
		
		formdesc.forEach(itm=>{
			var has_rules = false;
			if (itm.hideif) {add_ref(itm.hideif); has_rules = true;}
			if (itm.showif) {add_ref(itm.showif); has_rules = true;}
			if (itm.enableif) {add_ref(itm.enableif); has_rules = true;}
			if (itm.disableif) {add_ref(itm.disableif); has_rules = true;}
			if (has_rules) this.rule_fields[itm.name] = itm;
		});

		this.scan_fields = Object.keys(refs);		
		this.rule_fields_keys = Object.keys(this.rule_fields)
	}
	
	create_form(dialog) {
		var ispwd = this.formdesc.find(x=>x.type == "password");		
		var root = document.createElement(ispwd?"form":"x-form");
		var dlg;
		if (dialog) {
			dlg = document.createElement("x-dialog");
			var title = this.langobj.set_node_text(
						document.createElement("x-dlg-title"),
						this.langcat, dialog,dialog);
			dlg.appendChild(title);
			dlg.appendChild(root);
			var popup = document.createElement("x-popup");
			popup.appendChild(dlg);
			dlg = popup;
		} else {
			dlg = root;
		}
		
		var button_bar = null;
		
		var dlg_rules = this.scan_fields.length?()=>{
			this.process_rules(form);
		}:()=>{};
		
		this.formdesc.forEach(x=>{
			var el;
			if (x.bottom) {
				if (!button_bar) button_bar = document.createElement("x-button-bar");
				el = this.create_standalone_element(x);
				button_bar.appendChild(el);
			} else {
				el = this.create_form_el(x, dlg_rules);
				root.appendChild(el);
			}			
		});

		if (button_bar) root.appendChild(button_bar);
		var form = new TemplateJS.View(dlg);		
		var init = this.formdesc.reduce((a,b)=>{
			if (b.default != undefined) a[b.name] = b.default;
			return a;
		},{})
		form.setData(init);
		dlg_rules();
		return form;
	}

	create_standalone_element(def) {
		var el;
		var tmp;
		switch (def.type) {
			
			default: 
			case "label":break;
			case "string": el = document.createElement("INPUT");
						   el.setAttribute("type","text");
						   break;
			case "password": el = document.createElement("INPUT");
						   el.setAttribute("type","password");
						   break;
			case "number": el = document.createElement("INPUT");
						   el.setAttribute("type","number");
						   if (def.step) el.setAttribute("step",def.step);
						   if (def.min != undefined) el.setAttribute("min",def.min);
						   if (def.max != undefined) el.setAttribute("max",def.max);
						   break;
			case "textarea": el = document.createElement("TEXTAREA");
							 el.setAttribute("rows",def.rows?def.rows:5);
							break;
			case "enum":  el = document.createElement("SELECT");
						  el.setAttribute("size",def.size?def.size:1);
						  if (Array.isArray(def.options)) {
								def.options.forEach(x=>{					
									let opt = document.createElement("OPTION");
									opt.setAttribute("value",x);
									this.langobj.set_node_text(opt, this.langcat, x,x);
									el.appendChild(opt);
								});
						  } else if (typeof def.options =="object") {
								Object.keys(def.options).forEach(x=>{
									let opt = document.createElement("OPTION");
									opt.setAttribute("value",def.options[x]);
									this.langobj.set_node_text(opt, this.langcat, def.options[x],x);
									el.appendChild(opt);									
								});
						  }
							
							
						
						  break;
			case "checkbox": el = document.createElement("X-CHECKBOX");
							 this.langobj.set_node_text(el, this.langcat, def.name+":checkbox","");
							 break;
            case "trigger": el = document.createElement("X-TRIGGER");
                             this.langobj.set_node_text(el, this.langcat, def.name+":trigger","");
                             break;
			case "slider": el = document.createElement("X-SLIDER");
							 el.dataset.min = def.min;
							 el.dataset.max = def.max;
							 el.dataset.mult = def.step?def.step:1;
							 el.dataset.fixed = Math.pow(10,-def.decimals);
							break;
			case "button": el = document.createElement("button");
  						   this.langobj.set_node_text(el, this.langcat, def.name,def.name);
						   el.setAttribute("type","button");
							  break;			 							 
			case "imgbutton": tmp = document.createElement("img");
							  tmp.setAttribute("src",def.src);
							  tmp.setAttribute("alt", this.langobj.get_text(this.langcat,def.alt,def.alt));
							  el = document.createElement("button");
							  el.appendChild(tmp);							  
							  el.setAttribute("type","button");
							  break;			 			
						
			
			
		};

		if (el) {
			el.dataset.name = def.name;
			if (def.disableif) el.disabled = true;
		}
		if (def.attrs) {
			Object.keys(def.attrs).forEach(name=>el.setAttribute(name, def.attrs[name]));
		}

		return el;
		
	}
	
	create_form_el(def, dlg_rules_fn) {	
		
		var itm_blk = document.createElement("x-form-item");
		itm_blk.dataset.name="whole_row::"+def.name;
		var label = document.createElement("x-form-label");
		itm_blk.appendChild(label);
		this.langobj.set_node_text(label,this.langcat,def.name, def.label || def.name);

		
		if (def.type == "errmsg") {
			itm_blk.setAttribute("class","error");
			itm_blk.dataset.name = def.name;
		} else {
			var el = this.create_standalone_element(def);
			if (el) {
				el.addEventListener(def.event?def.event:"change",dlg_rules_fn);
				itm_blk.appendChild(el);
			}
		}

	
		var tooltip_id = def.name + ":tooltip";
		var tt = this.langobj.get_text(this.langcat, tooltip_id);
		if (tt) itm_blk.setAttribute("title",tt);
		
		return itm_blk;
		
	}
	
	add_depend(def, target) {
		var keys = Object.keys(def);
		keys.forEach(k=>{
			if (k != "" && k[0] == "$") {
				this.add_depend(def[k], target);
			} else {
				var x;
				if (!this.resolve_fields[k]) x = this.resolve_fields[k] = [];
				else x = this.resolve_fields[k];
				x.push(target);
			}
		});
	}
	
	process_rules(form) {
		var data = form.readData(this.scan_fields);
		this.rule_fields_keys.forEach(x=>{
			var dx = this.rule_fields[x];			
			if (dx.hideif) {
				form.showItem("whole_row::"+dx.name, !this.evaluate_and(dx.hideif, data));			
			}
			if (dx.showif) {
				form.showItem("whole_row::"+dx.name, this.evaluate_and(dx.showif, data));
			}
			if (dx.disableif) {
				form.enableItem(dx.name, !this.evaluate_and(dx.disableif, data));
			}
			if (dx.enableif) {
				form.enableItem(dx.name, this.evaluate_and(dx.enableif, data));
			}				
		});		
	}
	
	evaluate_and(def, data) {
		return Object.keys(def).reduce((c,n)=>{
			if (c) {
				if (n=="$or") c = this.evaluate_or(def[n]);
				else if (n == "$and") c = this.evaluate_and(def[n]);
				else if (n == "$not") c = !this.evaluate_and(def[n]);
				else c = this.evaluate_value(def[n], data[n], data); 
				
			}			
			return c;
		},true);
	}
	
	evaluate_or(def, data) {
		return Object.keys(def).reduce((c,n)=>{
			if (!c) {
				if (n=="$or") c = this.evaluate_or(def[n]);
				else if (n == "$and") c = this.evaluate_and(def[n]);
				else if (n == "$not") c = !this.evaluate_or(def[n]);
				else c = this.evaluate_value(def[n], data[n], data); 
				
			}			
			return c;
		},false);
	}
	
	evaluate_value(def, value, data) {
		if (typeof(def) == "object" && !Array.isArray(def)) {
			return Object.keys(def).reduce((c,n)=>{
				if (c) {
					var val = def[n];
					if (typeof val == "object" && val["$name"]) {
						val = data[val["$name"]];
					}
					if (n=="<") c = value < val;
					else if (n==">") c = value > val;
					else if (n=="<=") c = value <= val;
					else if (n==">=") c = value >= val;
					else if (n=="!=") c = value != val;
					else if (n=="==") c = value == val;
					else if (n=="in") c = Array.isArray(val) && val.find(z=>z == value) != undefined;
					else c=false;  
					
				}			
				return c;
			},true);			
		} else if (typeof(def) == "function") {
			return def(value);
		} else if (typeof(def) == "boolean" && value instanceof TriggerValue) {
			return def == value.b;		
		} else {
			return def == value;
		}
	}
}

