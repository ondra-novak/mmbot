"use strict";

TemplateJS.View.regCustomElement("X-SLIDER", new TemplateJS.CustomElement(
		function(elem,val) {
			var range = elem.querySelector("input[type=range]");
			var number = elem.querySelector("input[type=number]");
			var mult = parseFloat(elem.dataset.mult);
			var fixed = parseInt(elem.dataset.fixed)
			var toFixed = function(v) {
				if (!isNaN(fixed)) return parseFloat(v).toFixed(fixed);
				else return v;
			}
            var min = parseFloat(elem.dataset.min);
            var max = parseFloat(elem.dataset.max);
			if (!range) {
				range = document.createElement("input");
				range.setAttribute("type","range");
				number = document.createElement("input");
				number.setAttribute("type","number");
				number.setAttribute("step",mult);
				var env1 = document.createElement("div");
				var env2 = document.createElement("div");
				var rmin = Math.floor(min/mult);
				var rmax = Math.floor(max/mult);
				range.setAttribute("min",rmin);
				range.setAttribute("max",rmax);
				range.addEventListener("input",function() {
					var v = parseInt(this.value);
					var val = v * mult;
					number.value = toFixed(val);
					elem.dispatchEvent(new Event("change"));
				});
				number.addEventListener("change", function() {
					var v = parseFloat(this.value);
					var val = v / mult;
					var curmin = parseFloat(range.min);
					var curmax = parseFloat(range.max);
                    if (elem.dataset.extend == "max" && v > curmax) {
                        range.setAttribute("max", val);
                    }
                    if (elem.dataset.extend == "min" && v < curmin) {
                        range.setAttribute("min", val);
                    }
					range.value = val;
					elem.dispatchEvent(new Event("change"));
				});				
				env1.appendChild(range);
				env2.appendChild(number);
				elem.appendChild(env1);
				elem.appendChild(env2);
			}
			if (elem.dataset.extend == "max" && val > (0.75*max+0.25*min)) {
                range.setAttribute("max", ((val - min) * 1.5 + min)/mult);
            }
            if (elem.dataset.extend == "min" && val < (0.25*max+0.75*min)) {
                range.setAttribute("min", ((val - max) * 1.5 + max)/mult);
            }
            range.value = val / mult;
            number.value = toFixed(val);
		},
		function(elem) {
			var number = elem.querySelector("input[type=number]");
			if (number) return parseFloat(number.valueAsNumber);
			else return 0;
			
		},
		function(elem,attrs) {
			
		}
));


TemplateJS.View.regCustomElement("X-CHECKBOX", new TemplateJS.CustomElement(
		function(elem,val) {
			var z = elem.querySelector("[data-name=inner");
			if (z == null) {
				z = document.createElement("span");
				z.setAttribute("data-name","inner");				
				elem.insertBefore(z, elem.firstChild);
				var w = document.createElement("span");
				z.appendChild(w);
				
				elem.addEventListener("click", function() {
					elem.classList.toggle("checked");
					elem.dispatchEvent(new Event("change"));
				});
			}
			var chk = elem.dataset.value !== undefined?(
					typeof val == "string"?val == elem.dataset.value:
					Array.isArray(val)?val.indexOf(elem.dataset.value) != -1:!!val):!!val; 			
			elem.classList.toggle("checked", chk);
		},
		function(elem) {
			var chk =  elem.classList.contains("checked");
			if (elem.dataset.value !== undefined) return chk?elem.dataset.value:undefined;
			else return chk;
			
		}
		
));

TemplateJS.View.regCustomElement("X-SWITCH", new TemplateJS.CustomElement(
		function(elem, val) {
			if (elem.dataset.separator) {
				var arr = elem.dataset.value.split(elem.dataset.separator);
				elem.hidden = arr.indexOf(val) == -1;
			} else {
				elem.hidden = (elem.dataset.value!=val);
			}
		},
		function(elem) {
			return undefined;
		}
));

TemplateJS.View.regCustomElement("X-PROGRESS", new TemplateJS.CustomElement(
		function(elem, val) {
			var inner = elem.firstChild;
			if (!inner) {
				inner = document.createElement("X-BAR");
				inner.style.display="block";
				inner.style.marginLeft="0";
				inner.style.marginRight="auto";
				inner.style.height="100%";
				elem.appendChild(inner);
			}
			if (typeof val == "number") {
				inner.style.width=val+"%";
				inner.textContent=val.toFixed(1)+"%"
				elem.classList.remove("message");
			} else {
				inner.style.width="100%";
				inner.textContent=val;
				elem.classList.add("message");
			}
		},
		function() {
			return undefined;
		}
));
