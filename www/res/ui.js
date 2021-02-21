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
			if (!range) {
				range = document.createElement("input");
				range.setAttribute("type","range");
				number = document.createElement("input");
				number.setAttribute("type","number");
				number.setAttribute("step",mult);
				var env1 = document.createElement("div");
				var env2 = document.createElement("div");
				var min = parseFloat(elem.dataset.min);
				var max = parseFloat(elem.dataset.max);
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
					range.value = val;
					elem.dispatchEvent(new Event("change"));
				});				
				env1.appendChild(range);
				env2.appendChild(number);
				elem.appendChild(env1);
				elem.appendChild(env2);
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
			elem.classList.toggle("checked", val);
		},
		function(elem) {
			return elem.classList.contains("checked");
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
