var CACHE = 'cache-update-and-refresh';
//serial 23opwkpo120

self.addEventListener('install', function(evt) {
	  console.log('The service worker is being installed.');
	  
	  evt.waitUntil(caches.open(CACHE).then(function (cache) {
		    cache.addAll([
		      './',
		      './index.html',
		      './res/style.css',
		      './res/code.js',
		      './res/common.js',
		      './res/calculator.svg',
		      './res/donate.svg',
		      './res/donate_sml.svg',
		      './res/logo.png',
		      './res/setup.png',
		      './res/chart.css',
		      './res/icon64.png',
		      'https://fonts.googleapis.com/css?family=Ruda&display=swap',
		      'https://fonts.gstatic.com/s/ruda/v10/k3kfo8YQJOpFqnYdaObJ.woff2'
		    ].map(function(url){
			return new Request(url, {credentials: 'same-origin'});
			}));
		  }));
	  self.skipWaiting();
});


self.addEventListener('activate', function(evt) {
});


self.addEventListener('fetch', function(evt) {
	  // Let the browser do its default thing
	  // for non-GET requests.
	  if (evt.request.method != 'GET') return;
	  if (evt.request.url.indexOf("?relogin=1") != -1) return;
	  if (evt.request.url.indexOf("report.json") != -1) return;
	  if (evt.request.url.indexOf("/admin/") != -1) return;

	  var p = fromCache(evt.request);
	  var q = p.then(function(x) {return x;}, function() {
		  return fetch(evt.request)
	  });
	  
	  evt.respondWith(q);
	  
	  evt.waitUntil(p.then(function() {
			    return update(evt.request).then(refresh);
	  },function(){
		  //nothing
	  }));
});






			  
function fromCache(request) {
	  return caches.open(CACHE).then(function (cache) {
	    return cache.match(request).then(function(x){
	    	return x || Promise.reject();
	    });
	  });
	}


function update(request) {
	  return caches.open(CACHE).then(function (cache) {
	    return fetch(request).then(function (response) {
	      if (response.status == 200) {
	    	  return cache.put(new Request(request.url, {credentials: 'same-origin'}), response.clone()).then(function () {
	    		  return response;
	    	  });
	      } else {
	    	  return response;
	      }
	    });
	  });
	}


function refresh(response) {
	  return self.clients.matchAll().then(function (clients) {
	    clients.forEach(function (client) {
	    	var message = {
	    	        type: 'refresh',
	    	        url: response.url,eTag: response.headers.get('ETag')
	        };
	    	client.postMessage(JSON.stringify(message));
	    });
	  });
}