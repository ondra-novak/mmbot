var CACHE = 'cache-update-and-refresh';
//serial wqdsklqoi234254

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
		      './res/mode_night.png',
		      './res/mode_day.png',
		      './res/chart.js',
		      './res/zvonek.png',
		      'https://fonts.googleapis.com/css?family=Ruda&display=swap',
		      'https://fonts.gstatic.com/s/ruda/v23/k3kKo8YQJOpFgHQ1mQ5VkEbUKaJFsh_50qk.woff2',
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
	  if (evt.request.url.indexOf("/api/login") != -1) return;
	  if (evt.request.url.indexOf("/api/data") != -1) return;
	  if (evt.request.url.indexOf("/api/ohlc") != -1) return;
      if (evt.request.url.indexOf("/api/user") != -1) return;
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

self.addEventListener('notificationclick', function(event) {
      console.log('On notification click: ', event.notification.tag);
  event.notification.close();
  event.waitUntil(clients.matchAll({
    type: "window"
  }).then(function(clientList) {
    for (var i = 0; i < clientList.length; i++) {
      var client = clientList[i];
      if ('focus' in client)
        return client.focus();
    }
    if (clients.openWindow)
      return clients.openWindow('index.html');
  }));

});