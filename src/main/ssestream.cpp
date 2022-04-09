/*
 * ssestream.cpp
 *
 *  Created on: 9. 4. 2022
 *      Author: ondra
 */

#include <imtjson/serializer.h>
#include "ssestream.h"

SSEStream::SSEStream(userver::PHttpServerRequest &&req)
		:req(std::move(req)), closed(false),flushing(false) {
}

void SSEStream::flush() {
	ondra_shared::RefCntPtr<SSEStream> me(this); //<create reference to this object
	flushing = true; //<we are going to flush
	stream.flush() >> [me](bool result) {
		me->flushComplete(result);
	};
}

void SSEStream::init() {
	std::lock_guard _(mx);
	//initialize response
	req->setContentType("text/event-stream");  //<correct content type
	req->set("Cache-Control", "no-cache");		//<we don't need cache
	req->set("Connection","close");				//<don't reuse connection
	req->set("X-Accel-Buffering","no");			//<disable NGINX buffering
	req->setStatus(200);						//<set status 200 OK
	stream = req->send();						//<send response - get stream to body
	flush();									//<flush http response
	monitor();									//initialize monitoring
}
bool SSEStream::on_event(const Report::StreamData &sdata) {
	//we need to lock to avoid async clashing
	std::lock_guard _(mx);
	//if stream is closed, report to the caller
	if (closed) return false;
	//if data is not command (so it is event)
	if (!sdata.command) {
		//find header in hash map
		auto itr = hash_map.find(sdata.hdr_hash);
		if (itr != hash_map.end()) {
			//if found and has exact data hash, don't send this event (duplicated)
			if (itr->second == sdata.data_hash) return true;
			//update data hash
			itr->second = sdata.data_hash;
		} else {
			//remember header
			hash_map.emplace(sdata.hdr_hash, sdata.data_hash);
		}
	} else {
		//this special command means request to flush hash_map
		if (sdata.event == Report::ev_clear_cache.event) {
			//cleared
			hash_map.clear();
			//ok
			return true;
		}
	}
	//now - the state if the stream can be "flushing" as it is async operation
	if (flushing) {
		//in this case, store event to the buffer exactly as it will be send
		//data: {json} <enter><enter>
		buffer.append("data: ");
		sdata.event.serialize([&](char c){buffer.push_back(c);});
		buffer.append("\r\n\r\n");
	} else {
		//state is not flushing, we can push data directly to the stream
		//data: {json} <enter><enter>
		stream.writeNB("data: ");
		sdata.event.serialize([&](char c){stream.putCharNB(c);});
		stream.writeNB("\r\n\r\n");
		flush();
	}
	//done and it is ok
	return true;
}
void SSEStream::flushComplete(bool result) {
	//lock the state
	std::lock_guard _(mx);
	//if result is true, we can continue
	if (result) {
		//if buffer is empty, nothing to flush
		if (buffer.empty()) {
			//flushing state is cleared
			flushing = false;
		} else {
			//otherwise, write content of the buffer to the stream
			stream.writeNB(buffer);
			//clear the buffer
			buffer.clear();
			//flush
			flush();
		}
	} else {
		//in case of error, close this stream (client will reconnect)
		closed = true;
	}
}
///monitors opened connection - waits for closing
void SSEStream::monitor() {
	ondra_shared::RefCntPtr<SSEStream> me(this);
	//read original stream - there should be no input unless close
	req->getStream().read() >> [me](const std::string_view &res) {
		//only when sse is not closed (otherwise finish operation without futher action
		if (!me->closed) {
			//if input is empty - it should be always empty, but whatever
			if (res.empty()) {
				//if stream timeouted - it is ok, restart reading
				if (me->stream.timeouted()) {
					//clear timeout
					me->stream.clearTimeout();
					//read again
					me->monitor();
					//exit
					return;
				}
			}
		}
		//mark stream closed
		me->closed = true;
		//release pointer reference
	};
}


