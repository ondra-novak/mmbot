
///declare namespace TemplateJS
var TemplateJS = function(){
	"use strict";

	///registers to an event for once fire - returns promise
	/**
	 * @param element element which to register
	 * @param name of the event similar to addEventListener
	 * @param arg arguments passed to the promise when event is fired
	 * 
	 * @return Promise object which is resolved once the event triggers
	 */
	function once(element, event, args) {

		return new Promise(function(ok) {
			
			function fire(z) {
				element.removeEventListener(event, fire, args);				
				ok(z);
			}			
			element.addEventListener(event, fire, args);
		});
	};
	
	///Creates a promise which is resolved after some tome
	/**
	 * @param time time in milliseconds (same as setTimeout)
	 * @param arg argument passed to the promise
	 * @return a Promise resolved after specified time
	 */
	function delay(time, arg) {
		return new Promise(function(ok) {
			setTimeout(function() {
				ok(arg);
			},time);
		});
	};
	

	
	///Creates a promise which is resolved once the specified element is added to the DOM
	/** @param elem element to monitor
	 *  @param arg arguments passed to the promise once the element is added to the DOM
	 *  @param timeout count of seconds to wait for render. Default is 10 seconds 
	 *  @return a Promise resolved once the element is rendered
	 *  
	 *  @note The function takes strong reference to the element. To avoid memory leak, there is a timeout
	 *  in which the element must be rendered (a.k.a. put to the DOM) otherwise, the Promise is rejected
	 *  
	 *  You can specify timeout by the timeout argument. Note that the timeout is not exactly in seconds. It
	 *  defines count of DOM changes rounds, where each round contains all changes made during 1 seconds
	 *  So if there is no activity in the DOM, the counter is stopped. 
	 *  This better accomodiate waiting to slow DOM changes and animations, which can cover an
	 *  animations which takes longer than 10 seconds to play especially, when whole animation is made by CSS
	 *  and no other DOM changes are made during the play
	 */
	function waitForRender(elem, arg, timeout){
		if (!timeout) timeout = 10;
		if (elem.isConnected) return Promise.resolve(arg);
		init_waitForRender();
		return new Promise(function(ok, err){
			
			waitForRender_list.push({
				elem:elem,
				fn:ok,
				err:err,
				arg:arg,
				time:Date.now(),
				timeouts: timeout,
				st:true
			});
		});
		
	};
	
	///Creates a promise which is resolved once the specified element is removed from the DOM
	/** @param elem element to monitor
	 *  @param arg argument
	 *  @param timeout specify timeout. If missing, function will never timeout
	 *  @return a Promise resolved once the element is removed
	 *  
	 *  @note function is useful to emulate destructor when the particular element
	 *  is removed from the DOM
	 */
	function waitForRemove(elem, arg, timeout) {
		if (timeout === undefined) timeout = null;
		if (!elem.isConnected) return Promise.resolve(arg);
		init_waitForRender();
		return new Promise(function(ok, err){
			
			waitForRender_list.push({
				elem:elem,
				fn:ok,
				err:err,
				arg:arg,
				time:Date.now(),
				timeouts: timeout,
				st:false
			});
		});
		
	}

	function init_waitForRender() {
		if (waitForRender_observer == null) {
			waitForRender_observer = new MutationObserver(waitForRender_callback);				
			waitForRender_observer.observe(document, 
				{attributes: false,
				 childList: true,
				  characterData: false,
				   subtree:true});
		}		
	}
	
	var waitForRender_list = [];
	var waitForRender_observer = null;
	var waitForRender_callback = function() {
		if (waitForRender_list.length == 0) {
			waitForRender_observer.disconnect();
			waitForRender_observer = null;
		} else {
			var tm = Date.now();
			waitForRender_list = waitForRender_list.reduce(function(acc,x){				
				if (x.elem.isConnected  == x.st) {
					x.fn(x.arg);
				} else if (x.timeouts !== null) {
					if (tm - x.time > 1000) {
						x.time = tm;
						if (--x.timeouts <= 0) {
							x.err(new Error("waitForRender/waitForRemove timeout"));
							return acc;
						} 
					} 
					acc.push(x);
				} else {
					acc.push(x);
				}
				return acc;
			},[]);
		}
	};


	
	
	function Animation(elem) {
		this.elem = elem;
		
		var computed = window.getComputedStyle(elem, null); 
		if (computed.animationDuration != "0" && computed.animationDuration != "0s") {
			this.type =  this.ANIMATION;
			this.dur = computed.animationDuration;
		} else if (computed.transitionDuration != "0" && computed.transitionDuration != "0s") {
			this.type = this.TRANSITION;
			this.dur = computed.transitionDuration;
		} else {
			this.type = this.NOANIM;
			this.dur = "0s";
		}	
		if (this.dur.endsWith("ms")) this.durms = parseFloat(this.dur);
		else if (this.dur.endsWith("s")) this.durms = parseFloat(this.dur)*1000;
		else if (this.dur.endsWith("m")) this.durms = parseFloat(this.dur)*60000;
		else this.durms = 1000;
	}
	 Animation.prototype.ANIMATION = 1;
	 Animation.prototype.TRANSITION = 2;
	 Animation.prototype.NOANIM = 0;
	
	 Animation.prototype.isAnimated = function() {
		return this.type != this.NOANIM;
	}
	 Animation.prototype.isTransition = function() {
		return this.type == this.TRANSITION;
	}
	 Animation.prototype.isAnimation = function() {
		return this.type == this.ANIMATION;
	}
	
	 Animation.prototype.restart = function() {
		var parent = this.elem.parentElement;
		var next = this.elem.nextSibling;
		parent.insertBefore(this.elem, next);		
	}
	
	 Animation.prototype.wait = function(arg) {
		var res;
		switch (this.type) {
			case this.ANIMATION: res = Promise.race([delay(this.durms),once(this.elem,"animationend")]);break;
			case this.TRANSITION: res = Promise.race([delay(this.durms),once(this.elem,"transitionend")]);break;
			default:
			case this.NOTHING:res = Promise.resolve();break;
		}
		if (arg !== undefined) {
			return res.then(function(){return arg;});
		} else {
			return res;
		}
	}

	///removes element from the DOM, but it plays "close" animation before removal
	/**
	 * @param element element to remove
	 * @param skip_anim remove element immediately, do not play animation (optional)
	 * @return function returns Promise which resolves once the element is removed
	 */
	function removeElement(element, skip_anim) {
		if (!element.isConnected) return Promise.resolve();
		if (element.dataset.closeAnim && !skip_anim) {			
		    var remopen = element.dataset.openAnim;
			if (remopen && !element.classList.contains(remopen)) {
					return removeElement(element,true);
			}			
			var closeAnim = element.dataset.closeAnim;
			return waitForDOMUpdate().then(function() {
				if (remopen) 
					element.classList.remove(remopen);
				element.classList.add(closeAnim);
				var anim = new Animation(element);
				if (anim.isAnimation()) 
					anim.restart();				
				return anim.wait();
			}).then(function() {
				return removeElement(element,true);			
			})
		} else {
			var event = new Event("remove");
			element.parentElement.removeChild(element);
			element.dispatchEvent(event);
			return Promise.resolve();
		}		
	}
	
	function waitForDOMUpdate() {
		return new Promise(function(ok) {
			window.requestAnimationFrame(function() {
				window.requestAnimationFrame(ok);
			});
		})
	}
	
	function addElement(parent, element, before) {
		if (before === undefined) before = null;
		if (element.dataset.closeAnim) {
			element.classList.remove(element.dataset.closeAnim);
		}
		element.classList.remove(element.dataset.openAnim);
		parent.insertBefore(element,before);
		window.getComputedStyle(element);
		if (element.dataset.openAnim) {
			waitForDOMUpdate().then(function() {
				element.classList.add(element.dataset.openAnim);				
			});
		}
	}
	
	function createElement(def) {
		if (typeof def == "string") {
			return document.createElement(def);
		} else if (typeof def == "object") {
			if ("tag" in def) {
				var elem = document.createElement(def.tag);
				var attrs = def.attrs || def.attributes;
				if (typeof attrs == "object") {
					for (var i in attrs) {
						elem.setAttribute(i,attrs[i]);
					}
				}
				if ("html" in def) {
					elem.innerHTML=def.html;
				} else if ("text" in def) {
					elem.appendChild(document.createTextNode(def.text));
				} else {
					var content = def.content || def.value || def.inner;
					if (content !== undefined) {
						elem.appendChild(loadTemplate(content));
					}
				}
				return elem;
			} else if ("text" in def) {
				return document.createTextNode(def.text);
			}
		}
		return document.createElement("div");
	}
	
	function loadTemplate(templateID) {
		var tempel;
		if (typeof templateID == "string") {
			tempel = document.getElementById(templateID);
			if (!tempel) {
				throw new Error("Template element doesn't exists: "+templateID);				
			}
		} else if (typeof templateID == "object") {
			if (templateID instanceof Element) {
				tempel = templateID;
			} else if (Array.isArray(templateID)) {
				return templateID.reduce(function(accum,item){
					var x = loadTemplate(item);
					if (accum === null) accum = x; else accum.appendChild(x);
					return accum;
				},document.createDocumentFragment());
			} else {
				return createElement(templateID);
			}
		}
		var cloned;
		if ("content" in tempel) {
			cloned = document.importNode(tempel.content,true);
		} else {
			cloned = document.createDocumentFragment();
			var x= tempel.firstChild;
			while (x) {
				cloned.appendChild(x.cloneNode(true));
				x = x.nextSibling;
			}
		}
		return cloned;
		
	}
	
		
	function View(elem) {
		if (typeof elem == "string") elem = document.getElementById(elem);
		this.root = elem;
		this.marked =[];
		this.groups =[];
		this.rebuildMap();
		//apply any animation now
		if (this.root.dataset && this.root.dataset.openAnim) {
			this.root.classList.add(this.root.dataset.openAnim);
		}
		
	};
	
		
	///Get root element of the view
	View.prototype.getRoot = function() {
		return this.root;
	}
	
	///Replace content of the view
	/**
	 * @param elem element which is put into the view. It can be also instance of View
	 */
	View.prototype.setContent = function(elem) {
		if (elem instanceof View) 
			return this.setContent(elem.getRoot());		
		this.clearContent();
		this.defaultAction = null;
		this.cancelAction = null;
		this.root.appendChild(elem);
		this.rebuildMap();
	};
	
	///Replace content of the view generated from the template
	/**
	 * @param templateRef ID of the template
	 */
	View.prototype.loadTemplate = function(templateRef) {
		this.setContent(loadTemplate(templateRef));
	}
		
	View.prototype.replace = function(view, skip_wait) {
		
		if (this.lock_replace) {
			view.lock_replace = this.lock_replace =  this.lock_replace.then(function(v) {
				delete view.lock_replace 
				return v.replace(view,skip_wait);
			});
			return this.lock_replace;
		}

		var nx = this.getRoot().nextSibling;
		var parent = this.getRoot().parentElement;
		var newelm = view.getRoot();
		
		view.modal_elem = this.modal_elem;
		delete this.modal_elem;
		
		if (!skip_wait) {
			var mark = document.createComment("#");
			parent.insertBefore(mark,nx);			
			view.lock_replace = this.close().then(function(){				
				addElement(parent,view.getRoot(), mark);
				parent.removeChild(mark);
				delete view.lock_replace;
				return view;
			});
			return view.lock_replace;
		} else {
			this.close();
			addElement(parent,view.getRoot(),nx);
			return Promise.resolve(view);
		}			
	}
	///Visibility state - whole view is hidden
	View.HIDDEN = 0;
	///Visibility state - whole view is visible
	View.VISIBLE = 1;
	///Visibility state - whole view is hidden, but still occupies area (transparent)
	View.TRANSPARENT=-1
	
	View.prototype.setVisibility = function(vis_state) {
		if (vis_state == View.VISIBLE) {
			this.root.hidden = false;
			this.root.style.visibility = "";
		} else if (vis_state == View.TRANSPARENT) {
			this.root.hidden = false;
			this.root.style.visibility = "hidden";			
		} else {
			this.root.hidden = true;
		}
	}
	
	View.prototype.show = function() {
		this.setVisibility(View.VISIBLE);
	}
	
	View.prototype.hide = function() {
		this.setVisibility(View.HIDDEN);
	}
	
	///Closes the view by unmapping it from the doom
	/** The view can be remapped through the setConent or open() 
	 * 
	 * @param skip_anim set true to skip any possible closing animation
	 *  
	 * @return function returns promise once the view is closed, this is useful especially when
	 * there is closing animation
	 * 
	 * */
	View.prototype.close = function(skip_anim) {
		return removeElement(this.root).then(function() {		
			if (this.modal_elem && this.modal_elem.isConnected) 
				this.modal_elem.parentElement.removeChild(this.modal_elem);			
		}.bind(this));
	}

	///Opens the view as toplevel window
	/** @note visual of toplevel window must be achieved through styles. 
	 * This function just only adds the view to root of page
	 * 
	 * @param elem (optional)if specified, the view is opened under specified element
	 * 
	 * @note function also installs focus handler allowing focus cycling by TAB key
	 */
	View.prototype.open = function(elem) {
		if (!elem) elem = document.body;
		addElement(elem,this.root);
		this._installFocusHandler();
	}


	///Opens the view as modal window
	/**
	 * Append lightbox which prevents accesing background of the window
	 * 
	 * @note function also installs focus handler allowing focus cycling by TAB key
	 */
	View.prototype.openModal = function() {
		if (this.modal_elem) return;
		var lb = this.modal_elem = document.createElement("light-box");
		if (View.lightbox_class) lb.classList.add(View.lightbox_class);
		else lb.setAttribute("style", "display:block;position:fixed;left:0;top:0;width:100vw;height:100vh;"+View.lightbox_style);
		document.body.appendChild(lb);
		this.open();
	//	this.setFirstTabElement()
	}
	
	View.clearContent = function(element) {
		var event = new Event("remove");
		var x =  element.firstChild
		while (x) {
			var y = x.nextSibling; 
			element.removeChild(x);
			x.dispatchEvent(event)
			x = y;
		}		
	}
	
	View.prototype.clearContent = function() {
		View.clearContent(this.root);
		this.byName = {};
	};
	
	///Creates view at element specified by its name
	/**@param name name of the element used as root of View
	 * 
	 * @note view is not registered as collection, so it is not accessible from the parent
	 * view though the findElements() function. However inner items are still visible directly
	 * on parent view.  
	 */
	View.prototype.createView = function(name) {
		var elem = this.findElements(name);
		if (!elem) throw new Error("Cannot find item "+name);		
		if (elem.length != 1) throw new Error("The element must be unique "+name);
		var view = new View(elem[0]);
		return view;
	};
	
	///Creates collection at given element
	/**
	 * @param selector which defines where collection is created. If there are multiple
	 * elements matching the selector, they are all registered as collection.
	 * @param name new name of collection. If the selector is also name of existing
	 * item, then this argument is ignored, because function replaces item by collection 
	 * 
	 * @note you don't need to call this function if you make collection by adding [] after
	 * the name 
	 */
	View.prototype.createCollection = function(selector, name) {
		var elems = this.findElements(selector);
		if (typeof selector == "string" && this.byName[selector]) name = selector;		
		var res = elems.reduce(function(sum, item){
			var x = new GroupManager(item, name);
			this.groups.push(x);
			sum.push(x);
			return sum;
		},[]); 
		this.byName[name] = res;
	};
	
	///Returns the name of class used for the mark() and unmark()
	/**
	 * If you need to use different name, you have to override this value
	 */
	View.prototype.markClass = "mark";	
	
	///Finds elements specified by selector or name
	/**
	 * @param selector can be either a string or an array. If the string is specified, then
	 * the sting can be either name of the element(group), which is specified by data-name or name
	 * or it can be a CSS selector if it starts by dot ('.'), hash ('#') or brace ('['). It
	 * can also start by $ to specify, that rest of the string is complete CSS selector, including
	 * a tag name ('$tagname'). If the selector is array, then only last item can be selector. Other
	 * items are names of collections as the function searches for the elements inside of 
	 * collections where the argument specifies a search path (['group-name','index1','index2','item'])
	 * 
	 * @note if `index1` is null, then all collections of given name are searched. if `index2` is
	 *  null, then result is all elements matching given selector for all items in the collection. This
	 *  is useful especially when item is name, because searching by CSS selector is faster if
	 *  achieveded directly from root
	 * 
	 *  
	 */
	View.prototype.findElements = function(selector) {
		if (typeof selector == "string") {
			if (selector) {
				var firstChar =selector.charAt(0);
				switch (firstChar) {
					case '.':
					case '[':			
					case '#': return Array.from(this.root.querySelectorAll(selector));
					case '$': return Array.from(this.root.querySelectorAll(selector.substr(1)));
					default: return selector in this.byName?this.byName[selector]:[];
				}
			} else {
				return [this.root];
			}
		} else if (Array.isArray(selector)) {
			if (selector.length==1) {
				return this.findElements(selector[0]);
			}
			if (selector.length) {
				var gg = this.byName[selector.shift()];
				if (gg) {
						var idx = selector.shift();
						if (idx === null) {
							return gg.reduce(function(sum,item){
								if (item.findElements)
									sum.push.apply(sum,item.findElements(selector));
								return sum;
							},[]);						
						} else {
							var g = gg[idx];
							if (g && g.findElements) {
								return g.findElements(selector);
							}
						}
					}
			}			
		} else if (typeof selector == "object" && selector instanceof Element) {
			return [selector];
		} 
		return [];
	}
	
	
	///Marks every element specified as CSS selector with a mark
	/**
	 * The mark class is stored in variable markClass. 
	 * This function is useful to mark elements for various purposes. For example if
	 * you need to highlight an error code, you can use selectors equal to error code. It
	 * will mark all elements that contain anything relate to that error code. Marked
	 * elements can be highlighted, or there can be hidden message which is exposed once
	 * it is marked
	 * 
	 */
	View.prototype.mark = function(selector) {
		var items = this.findElements(selector);
		var cnt = items.length;
		for (var i = 0; i < cnt; i++) {
			items[i].classList.add(this.markClass);
			this.marked.push(items[i]);
		}				
	};
			

	View.prototype.forEachElement = function(selector, fn, a, b) {
		var items = this.findElements(selector);
		items.forEach(fn, a, b);
	}

	
	///Removes all marks
	/** Useful to remove any highlight in the View
	 */
	View.prototype.unmark = function() {
		var cnt = this.marked.length;
		for (var i = 0; i < cnt; i++) {
			this.marked[i].classList.remove(this.markClass);
		}
		this.marked = [];
	};
	
	View.prototype.anyMarked = function() {
		return this.marked.length > 0;
	}
	///Installs keyboard handler for keys ESC and ENTER
	/**
	 * This function is called by setDefaultAction or setCancelAction, do not call directly
	 */
	View.prototype._installKbdHandler = function() {
		if (this.kbdHandler) return;
		this.kbdHandler = function(ev) {
			var x = ev.which || ev.keyCode;
			if (x == 13 && this.defaultAction && ev.target.tagName != "TEXTAREA" && ev.target.tagName != "BUTTON") {
				if (this.defaultAction(this)) {
					ev.preventDefault();
					ev.stopPropagation();
				}
			} else if (x == 27 && this.cancelAction) {
				if (this.cancelAction(this)) {
					ev.preventDefault();
					ev.stopPropagation();
				}			
			}		
		}.bind(this);
		this.root.addEventListener("keydown", this.kbdHandler);
	};
	
	///Sets function for default action
	/** Default action is action called when user presses ENTER. 
	 *
	 * @param fn a function called on default action. The function receives reference to
	 * the view as first argument. The function must return true to preven propagation
	 * of the event
	 * @param el_name optional, if set, corresponding element receives click event for default action
	 *                  (button OK in dialog)
	 * 
	 * The most common default action is to validate and sumbit entered data
	 */
	View.prototype.setDefaultAction = function(fn, el_name) {
		this.defaultAction = fn;
		this._installKbdHandler();
		if (el_name) {
			var data = {};
			data[el_name] = {"!click":fn};
			this.setData(data)
		}
	};

	///Sets function for cancel action
	/** Cancel action is action called when user presses ESC. 
	 *
	 * @param fn a function called on cancel action. The function receives reference to
	 * the view as first argument. The function must return true to preven propagation
	 * of the event

	 * @param el_name optional, if set, corresponding element receives click event for default action
	 *                  (button CANCEL in dialog)
	 * 
	 * The most common cancel action is to reset form or to exit current activity without 
	 * saving the data
	 */
	View.prototype.setCancelAction = function(fn, el_name) {
		this.cancelAction = fn;
		this._installKbdHandler();
		if (el_name) {
			var data = {};
			data[el_name] = {"!click":fn};
			this.setData(data)
		}
	};
	
	function walkDOM(el, fn) {
		var c = el.firstChild;
		while (c) {
			fn(c);
			walkDOM(c,fn);
			c = c.nextSibling;
		}
	}
	
	///Installs focus handler
	/** Function is called from setFirstTabElement, do not call directly */
	View.prototype._installFocusHandler = function(fn) {
		if (this.focus_top && this.focus_bottom) {
			if (this.focus_top.isConnected && this.focus_bottom.isConnected)
				this.focus_top.focus();
				return;
		}
		var focusHandler = function(where, ev) {
			setTimeout(function() {
				where.focus();
			},10);	
		};
		
		var highestTabIndex=null;
		var lowestTabIndex=null;
		var firstElement=null;
		var lastElement = null;
		walkDOM(this.root,function(x){
			if (typeof x.tabIndex == "number" && x.tabIndex != -1) {
				if (highestTabIndex===null) {
					highestTabIndex = lowestTabIndex = x.tabIndex;
					firstElement = x;
				} else {
					if (x.tabIndex >highestTabIndex) highestTabIndex = x.tabIndex;
					else if (x.tabIndex <lowestTabIndex) {
						lowestTabIndex= x.tabIndex;
						firstElement  = x;
					}
				}
				if (x.tabIndex == highestTabIndex) lastElement = x;
			}
		});
		
		if (firstElement && lastElement) {
			var le = document.createElement("focus-end");
			le.setAttribute("tabindex",highestTabIndex);
			le.style.display="inline-block";
			this.root.appendChild(le);
			le.addEventListener("focus", focusHandler.bind(this,firstElement));
	
			var fe = document.createElement("focus-begin");
			fe.setAttribute("tabindex",highestTabIndex);
			fe.style.display="inline-block";
			this.root.insertBefore(fe,this.root.firstChild);
			fe.addEventListener("focus", focusHandler.bind(this,lastElement));
			
			firstElement.focus();
		}				
		this.focus_top = firstElement;
		this.focus_bottom = lastElement;
	};
	
	///Sets first TAB element and installs focus handler (obsolete)
	/**
	 * @param el the first TAB element in the form, it also receives a focus. You should
	 * specify really first TAB, even if you need to move focus elsewhere. Just move the
	 * focus after setting the first TAB element.
	 * 
	 * The focus handler ensures that focus will not leave the current View by pressing TAB.
	 * key. Function provides of cycling of the focus on the View. The first TAB element 
	 * is need to have a home position defined.
	 */
	View.prototype.setFirstTabElement = function(el) {
		this._installFocusHandler();
	}
	
	function GroupManager(template_el,name) {
		this.baseEl = template_el;
		this.parent = template_el.parentNode;
		this.anchor = document.createComment("><");
		this.idmap={};
		this.result = [];
		this.curOrder =[];		
		this.parent.insertBefore(this.anchor, this.baseEl);
		this.parent.removeChild(this.baseEl);
		this.name = name;
		template_el.dataset.group=true;
		template_el.removeAttribute("data-name");
		template_el.removeAttribute("name");

	}
	
	GroupManager.prototype.isConnectedTo = function(elem) {
		return elem.contains(this.anchor);
	}
	
	GroupManager.prototype.begin = function() {
		this.result = [];
		this.newOrder = [];		
	}
	
	
	GroupManager.prototype.setValue = function(id, data) {			
		var x = this.idmap[id];
		if (!x) {
			var newel = this.baseEl.cloneNode(true);
			var newview = new View(newel);
			x = this.idmap[id] = newview;			
		} else {
			this.lastElem = x.getRoot();
		}
		this.newOrder.push(id);
		var t = data["@template"];
		if (t) {
			x.loadTemplate(t);
		}
		var res =  x.setData(data);
		if (res)
		   this.result.push(res);				 
	}
	
	GroupManager.prototype.findElements = function(selector) {
		var item = selector.shift();
		if (item === null) {
			var res = [];
			for (var x in this.idmap) {
				res.push.apply(res,this.idmap[x].findElements(selector));
			}
			return res;
		} else {			
			return this.idmap[item]?this.idmap[item].findElements(selector):[];
		}
	}
	
	GroupManager.prototype.finish = function() {
		var newidmap = {};		
		this.newOrder.forEach(function(x){
			if (this.idmap[x]) {
				newidmap[x] = this.idmap[x];
				delete this.idmap[x];
			} else {
				throw new Error("Duplicate row id: "+x);
			
			}		
		},this);
		var oldp = 0;
		var oldlen = this.curOrder.length;		
		var newp = 0;
		var newlen = this.newOrder.length;
		var ep = this.anchor.nextSibling;
		var movedid = {};
		while (oldp < oldlen) {
			var oldid = this.curOrder[oldp];
			var newid = this.newOrder[newp];
			if (oldid in this.idmap) {
				oldp++;
				ep = this.idmap[oldid].getRoot().nextSibling;
			} else if (oldid == newid) {
				oldp++;
				newp++;
				ep = newidmap[oldid].getRoot().nextSibling;
			} else if (!movedid[oldid]) {
				this.parent.insertBefore(newidmap[newid].getRoot(),ep);
				newp++;
				movedid[newid] = true;
			} else {
				oldp++;
			}
		}
		while (newp < newlen) {			
			var newid = this.newOrder[newp];
			this.parent.insertBefore(newidmap[newid].getRoot(),ep);
			newp++;			
		}
		for (var x in this.idmap) {
			try {
				this.idmap[x].close();
			} catch (e) {
				
			}
		}
		
		this.idmap = newidmap;
		this.curOrder = this.newOrder;		
		this.newOrder = [];
		return this.result;
		
	}
	
	GroupManager.prototype.readData = function() {
	
		var out = [];		
		for (var x in this.idmap) {
			var d = this.idmap[x].readData();
			d["@id"] = x;
			out.push(d);			
		}
		return out;
		
	}
	
	///enables items
	/**
	 * @param name name of item
	 * @param enable true/false whether item has to be enabled
	 */
	View.prototype.enableItem = function(name, enable) {
		var d = {};
		d[name] = {"disabled":enable?null:""};
		this.setData(d);
	}

	///show or hide item
	/**
	 * @param name name of item
	 * @param showCmd true/false to show or hide item, or you can use constants View.VISIBLE,View.HIDDEN and View.TRANSPARENT
	 */
	View.prototype.showItem = function(name, showCmd) {
		var d = {};
		if (typeof showCmd == "boolean") {
			this.showItem(name,showCmd?View.VISIBLE:View.HIDDEN);
		}else {			
			if (showCmd == View.VISIBLE) {
				d[name] = {".hidden":false,".style.visibility":""};
			} else if (showCmd == View.TRANSPARENT) {
				d[name] = {".hidden":false,".style.visibility":"hidden"};
			} else {
				d[name] = {".hidden":true};
			}
		}
		this.setData(d);
	}

	///sets an event procedure to the item
	/**
	 * @param name name of item
	 * @param event name of event procedure
	 * @param fn function. To remove event procedure, specify null
	 * 
	 * @note it is faster to set the event procedure through setData along with other items
	 */
	View.prototype.setItemEvent = function(name, event, fn) {
		var d = {}
		var evdef = {};
		evdef["!"+event] = fn;
		d[name] = evdef;
		this.setData(d);
		
	}

	View.prototype.setItemValue = function(name, value) {
		var d = {};
		d[name] = {value:value}
		this.setData(d);
	}

	View.prototype.loadItemTemplate = function(name, template_name) {
		var v = View.createFromTemplate(template_name);
		this.setItemValue(name, v);
		return v;
	}
	
	View.prototype.clearItem = function(name) {
		this.setItemValue(name, null);
	}

	///Rebuilds map of elements
	/**
	 * This function is called in various situations especialy, after content of the
	 * View has been changed. The function must be called manually to register
	 * any new field added by function outside of the View.
	 * 
	 * After the map is builtm, you can access the elements through the variable byName["name"],
	 * Please do not modify the map manually
	 */
	View.prototype.rebuildMap = function(rootel) {
		if (!rootel) rootel = this.root;
		this.byName = {};
		
		this.groups = this.groups.filter(function(x) {return x.isConnectedTo(rootel);});
		this.groups.forEach(function(x) {this.byName[x.name] = [x];},this);
		
		function checkSubgroup(el) {
			while (el && el != rootel) {
				if (el.dataset.group) return true;
				el = el.parentElement;
			}
			return false;
		}
		
		var elems = rootel.querySelectorAll("[data-name],[name]");
		var cnt = elems.length;
		var i;
		for (i = 0; i < cnt; i++) {
			var pl = elems[i];
			if (rootel.contains(pl) && !checkSubgroup(pl)) {
				var name = pl.name || pl.dataset.name || pl.getAttribute("name");
				name.split(" ").forEach(function(vname) {
					if (vname) {
						if (vname && vname.endsWith("[]")) {
							vname = vname.substr(0,name.length-2);
							var gm = new GroupManager(pl, vname);
							this.groups.push(gm);
							if (!Array.isArray(this.byName[vname])) this.byName[vname] = [];
							this.byName[vname].push(gm);
						} else{
							if (!Array.isArray(this.byName[vname])) this.byName[vname] = [];
							this.byName[vname].push(pl);
						}
					}
				},this);

				}
			}		
	}
	
	///Sets data in the view
	/**
	 * @param structured data. Promise can be used as value, the value is rendered when the promise
	 *  is resolved
	 *  
	 * @return Returns Promise which becomes resolved once ale items are set to they
	 * controls. The delay can happen, when one of the values is a Promise. 
	 */
	View.prototype.setData = function(data) {
		var me = this;
		var results = [];
		
		function checkSpecialValue(val, elem) {
			if (val instanceof Element) {
				View.clearContent(elem)
				elem.appendChild(val);
				return true;
			} else if (val instanceof View) {
				View.clearContent(elem)
				elem.appendChild(val.getRoot());
				return true;
			} else if (val instanceof Date && elem.type == "date") {
				elem.valueAsDate = val;
				return true;
			}

		}
		
		function isPromise(v) {
			return (typeof v == "object" && v instanceof Promise);
		}
		
		
		function processItem(itm, elemArr, value) {
					var out = [];
					elemArr.forEach(function(elem) {
						var val = value;
						var res /* = undefined*/;
						if (elem) {
							var eltype = elem.tagName;
							if (elem.dataset && elem.dataset.type) eltype = elem.dataset.type;			
							var customEl = eltype && View.customElements[eltype.toUpperCase()];							
							if (typeof val == "object" && val !== null) {
								if (checkSpecialValue(val,elem)) {
									return							
								} else if (!Array.isArray(val)) {									
									if (!customEl || !customEl.setAttrs || !customEl.setAttrs(elem,val)) {
										updateElementAttributes(elem,val);										
									}
									if (!("value" in val)) {
										return;
									}else {
										var val = val.value;
										if (typeof val == "object" && checkSpecialValue(val,elem)) return;
									}
								}
							}
							if (elem instanceof GroupManager) {
								var group = elem;
								group.begin();
								if (Array.isArray(val) ) {
									var i = 0;
									var cnt = val.length;
									for (i = 0; i < cnt; i++) {
										var id = val[i]["@id"] || i;
										group.setValue(id, val[i]);
									}
								}
								res = group.finish();
								out.push.apply(out,res);
							} else {
								function render_val(val) {
									if (customEl) {
										return  customEl.setValue(elem,val);
									} else {
										return updateBasicElement(elem, val);								
									}								
								}
								if (val !== undefined) {
									if (isPromise(val)) res = val.then(render_val);
									else res = render_val(val);
								}
								if (res)
									out.push(res);;
							}
						}
					});
					return out;
		
		}
		
		for (var itm in data) {
			var elemArr = this.findElements(itm);
			if (elemArr) {
				var val = data[itm];
				if (isPromise(val)) {
					results.push(val.then(processItem.bind(this,itm,elemArr)));
				} else {
					var r = processItem(itm,elemArr,val);
					results.push.apply(results,r);
				}
			}
		}
		return Promise.all(results);
	}
	
	var event_handlers = new WeakMap();
	
	function updateElementAttributes (elem,val) {
		for (var itm in val) {
			if (itm == "value") continue;
			if (itm == "classList" && typeof val[itm] == "object") {
				for (var x in val[itm]) {
					if (val[itm][x]) elem.classList.add(x);
					else elem.classList.remove(x);
				}
			} else if (itm.substr(0,1) == "!") {
				var name = itm.substr(1);
				var fn = val[itm];
				var eh = event_handlers.get(elem);
				if (!eh) eh = {};
				if (eh[name]) {
					var reg = eh[name];
					elem.removeEventListener(name,reg);					
				}
				eh[name] = fn;
				elem.addEventListener(name, fn);
				event_handlers.set(elem,eh);
			} else if (itm.substr(0,1) == ".") {				
				var name = itm.substr(1);
				var obj = elem;
				var nextobj;
				var idx;
				var subkey;
				while ((idx = name.indexOf(".")) != -1) {
					subkey = name.substr(0,idx);
					nextobj = obj[subkey];
					if (nextobj == undefined) {
						if (v !== undefined) nextobj = obj[subkey] = {};
						else return;
					}
					name = name.substr(idx+1);
					obj = nextobj;
				}
				var v = val[itm];
				if ( v === undefined) {
					delete obj[name];
				} else {
					obj[name] = v;
				}					
			} else if (val[itm]===null) {
				elem.removeAttribute(itm);
			} else {
				elem.setAttribute(itm, val[itm].toString())
			} 
		}
	}
	
	function updateInputElement(elem, val) {
		var type = elem.getAttribute("type");
		if (type == "checkbox" || type == "radio") {
			if (typeof (val) == "boolean") {
				elem.checked = !(!val);
			} else if (Array.isArray(val)) {
				elem.checked = val.indexOf(elem.value) != -1;
			} else if (typeof (val) == "string") {
				elem.checked = elem.value == val;
			} 
		} else if (type == "date" && typeof val == "object" && val instanceof Date) {
			elem.valueAsDate = val;
		} else {
			elem.value = val;
		}
	}
	
	
	function updateSelectElement(elem, val) {
		if (typeof val == "object") {
			var curVal = elem.value;
			View.clearContent(elem);
			if (Array.isArray(val)) {
				var i = 0;
				var l = val.length;
				while (i < l) {
					var opt = document.createElement("option");
					opt.appendChild(document.createTextNode(val[i].toString()));
					elem.appendChild(opt);
					i++;
				}
			} else {
				for (var itm in val) {
					var opt = document.createElement("option");
					opt.appendChild(document.createTextNode(val[itm].toString()));
					opt.setAttribute("value",itm);
					elem.appendChild(opt);				
				}
			}
			elem.value = curVal;
		} else {
			elem.value = val;
		}
	}
	
	function updateBasicElement (elem, val) {
		View.clearContent(elem);
		if (val !== null && val !== undefined) {
			elem.appendChild(document.createTextNode(val));
		}
	}

	///Reads data from the elements
	/**
	 * For each named element, the field is created in result Object. If there
	 * are multiple values for the name, they are put to the array.
	 * 
	 * Because many named elements are purposed to only display values and not enter
	 * values, you can mark such elements as data-readonly="1"
	 */
	View.prototype.readData = function(keys) {
		if (typeof keys == "undefined") {
			keys = Object.keys(this.byName);
		}
		var res = {};
		var me = this;
		keys.forEach(function(itm) {
			var elemArr = me.findElements(itm);
			elemArr.forEach(function(elem){			
				if (elem) {					
					if (elem instanceof GroupManager) {
						var x =  elem.readData();
						if (res[itm] === undefined) res[itm] = x;
						else x.forEach(function(c){res[itm].push(c);});
					} else if (!elem.dataset || !elem.dataset.readonly) {
						var val;
						var eltype = elem.tagName;
						if (elem.dataset.type) eltype = elem.dataset.type;
						var eltypeuper = eltype.toUpperCase();
						if (View.customElements[eltypeuper]) {
							val = View.customElements[eltypeuper].getValue(elem, res[itm]);
						} else {
							val = readBasicElement(elem,res[itm]);					
						}
						if (typeof val != "undefined") {
							res[itm] = val;
						}
					}
				}
			});
		});
		return res;
	}
	
	function readInputElement(elem, curVal) {
		var type = elem.getAttribute("type");
		if (type == "checkbox") {
			if (!elem.hasAttribute("value")) {
				return elem.checked;						
			} else {
				if (!Array.isArray(curVal)) {
					curVal = [];
				}
				if (elem.checked) {
					curVal.push(elem.value);
				}
				return curVal;
			}
		} else if (type == "radio") {
			if (elem.checked) return elem.value;
			else return curVal;
		} else if (type == "number") {
			return elem.valueAsNumber;		
		} else if (type == "date") {
			return	elem.valueAsDate;
		} else {
			return elem.value;
		}
	}
	function readSelectElement(elem) {
		return elem.value;	
	}
		
	function readBasicElement(elem) {
		var group = elem.template_js_group;
		if (group) {
			return group.readData();			
		} else {
			if (elem.contentEditable == "true" ) {
				if (elem.dataset.format == "html")
					return elem.innerHTML;
				else 
					return elem.innerText;
			}
		}
	}
	
	///Registers custrom element
	/**
	 * @param tagName name of the tag
	 * @param customElementObject new CustomElementEvents(setFunction(),getFunction())
	 */
	View.regCustomElement = function(tagName, customElementObject) {
		var upper = tagName.toUpperCase();
		View.customElements[upper] = customElementObject;
	}

	///Creates root View in current page
	/**
	 * @param visibility of the view. Because the default value is View.HIDDEN, if called
	 * without arguments the view will be hidden and must be shown by the function show()
	 */
	View.createPageRoot = function(visibility /* = View.HIDDEN */) {
		var elem = document.createElement(View.topLevelViewName);
		document.body.appendChild(elem)
		var view = new View(elem);
		view.setVisibility(visibility);
		return view;
	}
	
	View.topLevelViewName = "div";
	
	///Creates view from template
	/**
	 * @param id of template. The template must by a single-root template or extra tag will be created
	 *  If you need to create from multi-root template, you need to specify definition of parent element
	 *  @param def parent element definition, it could be single tag name, or object, which 
	 *  specifies "tag" as tagname and "attrs" which contains key=value attributes
	 *  
	 *  @return newly created view
	 */
	View.fromTemplate = function(id, def) {
		var t = loadTemplate(id);		
		if (t.nodeType == Node.DOCUMENT_FRAGMENT_NODE) {
			var x = t.firstElementChild;
			if (x != null && x.nextElementSibling == null) {
				t = x;
			} else {
				var el = createElement(def);
				el.appendChild(t);
				t = el;
			}
		}
		return new View(t);
	}

	View.createFromTemplate = View.fromTemplate;
	
	View.createEmpty = function(tagName, attrs) {
		if (tagName === undefined) tagName = "div";
		var elem = document.createElement(tagName);
		if (attrs) {
			for (var v in attrs) {
				elem.setAttribute(v, attrs[v]);
			}
		}
		return new View(elem);			
	}
	
	function CustomElementEvents(setval,getval,setattrs) {
		this.setValue = setval;
		this.getValue = getval;
		this.setAttrs = setattrs;
		
	}

	View.customElements = {
			"INPUT":{
				"setValue":updateInputElement,
				"getValue":readInputElement,
			},
			"TEXTAREA":{
				"setValue":updateInputElement,
				"getValue":readInputElement,
			},
			"SELECT":{
				"setValue":updateSelectElement,
				"getValue":readSelectElement,
			},
			"IMG":{
				"setValue":function(elem,val) {
					elem.setAttribute("src",val);
				},
				"getValue":function(elem) {
					elem.getAttribute("src");
				}
			},
			"IFRAME":{
				"setValue":function(elem,val) {
					elem.setAttribute("src",val);
				},
				"getValue":function(elem) {
					elem.getAttribute("src");
				}
			}
	};

	///Lightbox style, mostly color and opacity
	View.lightbox_style = "background-color:black;opacity:0.25";
	///Lightbox class, if defined, style is ignored
	View.lightbox_class = "";

	
	
	return {
		"View":View,
		"loadTemplate":loadTemplate,
		"CustomElement":CustomElementEvents,
		"once":once,
		"delay":delay,
		"Animation":Animation,
		"removeElement":removeElement,
		"addElement":addElement,
		"waitForRender":waitForRender,
		"waitForRemove":waitForRemove,
		"waitForDOMUpdate":waitForDOMUpdate
	};
	
}();


