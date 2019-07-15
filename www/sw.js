var CACHE = 'cache-update-and-refresh';

self.addEventListener('install', function(evt) {
	  console.log('The service worker is being installed.');
	  
	  evt.waitUntil(caches.open(CACHE).then(function (cache) {
		    cache.addAll([
		      './index.html',
		      './style.css',
		      './code.js',
		      'https://fonts.googleapis.com/css?family=Ruda&display=swap',
		      'https://fonts.gstatic.com/s/ruda/v10/k3kfo8YQJOpFqnYdaObJ.woff2'
		    ]);
		  }));
});


self.addEventListener('activate', function(evt) {
	  console.log('EMPTY SERVICE WORKER ACTIVATE');	  
});


self.addEventListener('fetch', function(evt) {
	  console.log('The service worker is serving the asset.');

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
	      return cache.put(request, response.clone()).then(function () {
	        return response;
	      });
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